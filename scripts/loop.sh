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
    if [[ "$SLUG" == "gate" ]]; then
        # La robustez del oraculo no se mide con sanitizers sino con venenos: un check
        # que pasa por estar vacio se ve igual que uno que funciona.
        echo "--- autoprueba del gate por venenos ---"
        ./scripts/gate-selftest.sh --fast
        return $?
    fi

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
    if [[ "$SLUG" == "gate" ]]; then
        # Lo que se mide del gate es cuanto tarda: un oraculo que tarda demasiado deja de
        # ejecutarse, y entonces no es un oraculo. Se mide tres veces porque una sola
        # medicion no distingue el coste real de un pico de la maquina.
        echo "--- duracion del gate rapido (3 corridas) ---"
        local times=()
        local run
        for run in 1 2 3; do
            local started_gate
            started_gate="$(date +%s)"
            local output
            output="$(./scripts/gate.sh --fast 2>&1)"
            local gate_status=$?
            local seconds=$(($(date +%s) - started_gate))
            times+=("$seconds")
            echo "corrida $run: ${seconds}s exit=${gate_status}"
            grep -E '^CHECK |checks fallidos' <<<"$output"

            # Mientras se cierra el loop del propio gate, el check 9 falla porque el
            # ledger de este entregable todavia no existe: es la unica excepcion
            # aceptada, y solo esa. Cualquier otro check en rojo anula la iteracion.
            if [[ $gate_status -ne 0 ]]; then
                local failed_line
                failed_line="$(grep -E '^checks fallidos' <<<"$output")"
                if [[ "$failed_line" != "checks fallidos: 9 lint-loop" ]]; then
                    echo "ITERACION ANULADA: el gate falla en algo que no es el check 9"
                    return 1
                fi
                if [[ -f ".loop/${PHASE}/${SLUG}.ledger.json" ]]; then
                    echo "ITERACION ANULADA: el ledger ya existe, el check 9 no deberia fallar"
                    return 1
                fi
            fi
        done
        echo "gate_fast_seconds=${times[*]}"
        return 0
    fi

    echo "--- benchmarks con la ISA de deploy (linea base publicable) ---"
    # Sin --quick: una iteracion de la clase perf mide de verdad, y ademas el umbral
    # min_duration_ms de config/loop.json existe para que una medicion no sea un parpadeo.
    ./scripts/bench.sh

    echo "--- benchmarks con -march=native (local-only, solo para comparar) ---"
    # docs/performance.md#p-02 publica las dos columnas: sirve para saber cuanto deja
    # sobre la mesa la ISA portable, y el numero nativo NO puede usarse como linea base.
    ./scripts/bench.sh --native

    echo "--- asignaciones en el hot path (allocator instrumentado) ---"
    ./build/release/bin/unit_tests "[perf]" --reporter compact

    echo "--- latencia end-to-end sobre los fixtures ---"
    # Se mide contra el servidor directamente, no lanzando el gate entero: el gate
    # incluye el check 9, que depende del ledger que esta iteracion va a producir.
    cmake --build --preset release >/dev/null
    PORT=8097 ./build/release/bin/battlesnake-server >/dev/null 2>&1 &
    local server_pid=$!
    for _ in $(seq 1 50); do
        curl -fsS http://127.0.0.1:8097/health >/dev/null 2>&1 && break
        sleep 0.2
    done
    python3 scripts/smoke.py --url http://127.0.0.1:8097 --repeats 300         --json-out "${METRICS}.latency"
    local smoke_status=$?
    kill "$server_pid" 2>/dev/null
    wait "$server_pid" 2>/dev/null
    return $smoke_status
}

run_context() {
    echo "--- lint de documentacion ---"
    ./scripts/lint-docs.sh || return 1

    echo "--- front-matter de los agentes, comandos y skills ---"
    local bad=0
    local file
    for file in .claude/agents/*.md .claude/commands/*.md .claude/skills/*/SKILL.md; do
        [[ -f "$file" ]] || continue
        if [[ "$(head -1 "$file")" != "---" ]]; then
            echo "FAIL $file: sin front-matter"
            bad=1
            continue
        fi
        if ! grep -qE '^(name|description):' "$file"; then
            echo "FAIL $file: front-matter sin name ni description"
            bad=1
        fi
    done
    [[ $bad -eq 0 ]] && echo "OK front-matter del harness"

    if [[ "$SLUG" == "gate" ]]; then
        # La clase context del gate no se conforma con que el lint pase: comprueba que el
        # lint CAZA lo que dice cazar, con los cinco venenos del check 6.
        echo "--- venenos del check 6 (el lint de docs se prueba a si mismo) ---"
        ./scripts/gate-selftest.sh 6 || return 1
    fi

    return $bad
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
