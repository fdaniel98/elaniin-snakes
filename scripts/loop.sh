#!/usr/bin/env bash
# Ejecuta la parte determinista de una iteracion del loop: corre los comandos de la
# clase, guarda el log crudo y deja un esqueleto de artefacto con las metricas que se
# pueden medir con un comando.
#
#   ./scripts/loop.sh <n> <slug> [clase]
#
# La parte NO determinista -invocar al subagente de la clase, leer su tabla y decidir si
# hay hallazgos- la hace el comando /loop del agente; este script es lo que ese comando
# ejecuta y lo que deja la evidencia que el check 9 verifica.
# ver docs/harness.md#h-05
#
# Clase por defecto segun el numero de iteracion: 1=correctness, 2=robustness,
# 3=perf, 4=context.
set -uo pipefail

cd "$(dirname "$0")/.."

N="${1:-}"
SLUG="${2:-}"
CLASS="${3:-}"

if [[ -z "$N" || -z "$SLUG" ]]; then
    echo "uso: $0 <n> <slug> [clase]"
    exit 2
fi

if [[ -z "$CLASS" ]]; then
    case "$N" in
        1) CLASS=correctness ;;
        2) CLASS=robustness ;;
        3) CLASS=perf ;;
        4) CLASS=context ;;
        *)
            echo "ERROR sin clase para la iteracion $N; pasala como tercer argumento"
            exit 2
            ;;
    esac
fi

PHASE="$(grep -m1 -E '^Fase:' STATE.md | grep -oE '[0-9]+' | head -1)"
[[ -n "$PHASE" ]] || {
    echo "ERROR STATE.md no declara 'Fase: <n>'"
    exit 2
}

DIR=".loop/${PHASE}/${SLUG}"
mkdir -p "$DIR"
LOG="${DIR}/i${N}.log"
METRICS="${DIR}/i${N}.metrics.json"

COMMIT_BEFORE="$(git rev-parse HEAD)"
STARTED_AT="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
START_MS="$(date +%s%3N)"

echo "loop i${N} clase=${CLASS} slug=${SLUG} fase=${PHASE} commit_before=${COMMIT_BEFORE}"

run_correctness() {
    echo "--- gate rapido ---"
    ./scripts/gate.sh --fast
    echo "--- tests de reglas y parser ---"
    ./build/release/bin/unit_tests "[rules],[ruleset],[brain]" --reporter compact
}

run_robustness() {
    echo "--- build con ASan + UBSan ---"
    cmake --preset debug && cmake --build --preset debug
    echo "--- tests con sanitizers, incluido el fuzz de 10000 estados ---"
    ASAN_OPTIONS=detect_leaks=1:abort_on_error=1:detect_stack_use_after_return=1 \
        UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 \
        ./build/debug/bin/unit_tests "[fuzz],[failsafe],[brain],[rules]" --reporter compact
    echo "--- mutantes ---"
    ./scripts/mutants.sh --json "${METRICS}.mutants"
}

run_perf() {
    echo "--- benchmarks ---"
    ./scripts/bench.sh --quick
    echo "--- latencia end-to-end sobre los fixtures ---"
    ./scripts/gate.sh 2>&1 | grep -E '^CHECK 8|p50=|p99='
}

run_context() {
    echo "--- lint de documentacion ---"
    ./scripts/lint-docs.sh
}

status=0
{
    echo "=== loop i${N} ${CLASS} ${SLUG} @ ${STARTED_AT} ==="
    case "$CLASS" in
        correctness) run_correctness ;;
        robustness) run_robustness ;;
        perf) run_perf ;;
        context) run_context ;;
        *)
            echo "ERROR clase desconocida: $CLASS"
            exit 2
            ;;
    esac
} >"$LOG" 2>&1 || status=$?

END_MS="$(date +%s%3N)"
DURATION=$((END_MS - START_MS))
LOG_SHA="$(sha256sum "$LOG" | awk '{print $1}')"
THRESHOLDS_SHA="$(sha256sum config/loop.json | awk '{print $1}')"

cat >"$METRICS" <<JSON
{
  "n": ${N},
  "class": "${CLASS}",
  "slug": "${SLUG}",
  "phase": ${PHASE},
  "commit_before": "${COMMIT_BEFORE}",
  "started_at": "${STARTED_AT}",
  "duration_ms": ${DURATION},
  "exit_code": ${status},
  "log_sha256": "${LOG_SHA}",
  "thresholds_sha256": "${THRESHOLDS_SHA}"
}
JSON

echo "log:       $LOG (exit=${status}, ${DURATION} ms)"
echo "metricas:  $METRICS"
echo "siguiente: revisar el log, anotar hallazgos y cerrar la iteracion en"
echo "           .loop/${PHASE}/${SLUG}.ledger.json"
exit $status
