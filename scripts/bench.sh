#!/usr/bin/env bash
# Benchmarks del motor y comparacion contra la linea base de docs/performance.md.
#
#   ./scripts/bench.sh            preset bench-deployisa (linea base publicable)
#   ./scripts/bench.sh --native   preset bench (-march=native): resultados local-only,
#                                 NO pueden usarse como linea base
#   ./scripts/bench.sh --quick    menos repeticiones, para el loop
#
# Motivo de la distincion: un numero medido con -march=native no es comparable con el
# binario que se despliega. ver docs/performance.md#p-02
set -uo pipefail

cd "$(dirname "$0")/.."

PRESET=bench-deployisa
LABEL=deploy-isa
# Cinco repeticiones por benchmark: una sola medicion no distingue una mejora del ruido
# de la maquina, y la clase perf del loop compara contra una linea base.
REPETITIONS=5
EXTRA=()

for arg in "$@"; do
    case "$arg" in
        --native)
            PRESET=bench
            LABEL=local-only
            ;;
        --quick) EXTRA+=(--benchmark_min_time=0.05s) ;;
        --repetitions) REPETITIONS=5 ;;
        *)
            echo "uso: $0 [--native] [--quick]"
            exit 2
            ;;
    esac
done

echo "== preset $PRESET ($LABEL) =="
cmake --preset "$PRESET" >/dev/null || exit 2
cmake --build --preset "$PRESET" >/dev/null || exit 2

OUT="docs/results/bench-$(date -u +%Y%m%dT%H%M%SZ)-${LABEL}.json"
mkdir -p docs/results

./build/"$PRESET"/bin/bench_engine     --benchmark_format=console     --benchmark_out="$OUT"     --benchmark_out_format=json     --benchmark_repetitions="$REPETITIONS"     --benchmark_report_aggregates_only=true     "${EXTRA[@]}"

echo
echo "resultados en $OUT (etiqueta: $LABEL)"
if [[ "$LABEL" == "local-only" ]]; then
    echo "AVISO local-only: estos numeros NO pueden publicarse como linea base"
fi
