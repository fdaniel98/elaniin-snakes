#!/usr/bin/env bash
# Soak del servidor: N partidas locales completas contra el arbitro oficial, midiendo
# cada POST /move como lo ve el arbitro y contando timeouts.
#
#   ./scripts/soak.sh --games 200 [--out DIR] [--seed-base N]
#
# Es la DoD de la fase 2 y a la vez el diagnostico: 0 timeouts en 200 partidas y el p99
# publicado.
#
# Dos instrumentos, y ninguno sobra:
#
#   - `you.latency` del JSONL es lo que el ARBITRO midio de ida y vuelta, que es con lo
#     que decide si llegamos tarde (`cli/commands/play.go:455,738`). Viene en
#     milisegundos ENTEROS, truncados por `.Milliseconds()`, asi que sirve para contar
#     timeouts y para ver la cola, no para un p99 fino.
#   - el campo `us=` del log del servidor es el tiempo dentro de decide(), con resolucion
#     de microsegundos. Es lo que cuesta nuestro codigo.
#
# La diferencia entre ambos es transporte. NO se mete un proxy en medio para medirla: un
# salto de Python que en produccion no existe inflaba la cifra trece veces.
#
# Una partida cada vez y una sola instancia nuestra, con tres rivales cautos en Python:
# lanzar partidas en paralelo mide la maquina, no el cerebro. El numero de nucleos y el
# resto del entorno van al reporte, porque dos corridas con entornos distintos no se
# comparan (ver docs/performance.md#p-03).
set -uo pipefail

cd "$(dirname "$0")/.."

GAMES=20
OUT="docs/results/soak-$(date -u +%Y%m%dT%H%M%SZ)"
SEED_BASE=700000
while [[ $# -gt 0 ]]; do
    case "$1" in
        --games) GAMES="$2"; shift 2 ;;
        --out) OUT="$2"; shift 2 ;;
        --seed-base) SEED_BASE="$2"; shift 2 ;;
        *) echo "uso: $0 [--games N] [--out DIR] [--seed-base N]" >&2; exit 2 ;;
    esac
done

SERVER=build/release/bin/battlesnake-server
TIMEOUT_MS=500
mkdir -p "$OUT"

die() { echo "ERROR $*" >&2; exit 2; }
[[ -x "$SERVER" ]] || die "falta $SERVER; corre 'cmake --build --preset release'"

RULES_SHA="$(grep -oE '[0-9a-f]{40}' docs/SOURCES.md | head -1)"
CLI="$(command -v battlesnake || true)"
if [[ -z "$CLI" && -n "$(command -v go || true)" && -x "$(go env GOPATH)/bin/battlesnake" ]]; then
    CLI="$(go env GOPATH)/bin/battlesnake"
fi
if [[ -z "$CLI" ]]; then
    command -v go >/dev/null 2>&1 || die "falta el CLI oficial y no hay go para compilarlo"
    if ! go install "github.com/BattlesnakeOfficial/rules/cli/battlesnake@${RULES_SHA}" 2>/dev/null; then
        bash scripts/build-referee-mirrored.sh || die "no se pudo compilar el arbitro"
        CLI="$PWD/build/tools/battlesnake"
    else
        CLI="$(go env GOPATH)/bin/battlesnake"
    fi
fi
[[ -x "$CLI" ]] || die "el arbitro no es ejecutable: $CLI"

# Entorno, al reporte: sin esto los numeros no son comparables con nada.
{
    echo "fecha_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "commit=$(git rev-parse --short HEAD 2>/dev/null || echo desconocido)"
    echo "nucleos=$(nproc)"
    echo "memoria_gb=$(free -g 2>/dev/null | awk 'NR==2{print $2}')"
    echo "cpu=$(grep -m1 'model name' /proc/cpuinfo 2>/dev/null | cut -d: -f2- | sed 's/^ //')"
    echo "partidas=$GAMES"
    echo "timeout_ms=$TIMEOUT_MS"
    echo "arbitro=$CLI"
    echo "concurrencia=1 partida, 1 instancia propia, 3 rivales cautos"
} > "$OUT/entorno.txt"
cat "$OUT/entorno.txt"

PIDS=()
limpia() {
    for pid in "${PIDS[@]:-}"; do
        [[ -n "$pid" ]] && kill "$pid" 2>/dev/null
    done
    PIDS=()
}
trap 'limpia; exit 130' INT TERM

SERVERLOG="$OUT/server.log"
: > "$SERVERLOG"
FALLOS=0

for ((g = 0; g < GAMES; ++g)); do
    SEED=$((SEED_BASE + g))
    BASE_PORT=$((9600 + (g % 20) * 10))
    NUESTRO=$BASE_PORT

    # Cada partida arranca el servidor de cero: asi cada una aporta un arranque en frio
    # real, que es lo que se mide aparte del maximo (ver ADR-0009).
    PORT=$NUESTRO "$SERVER" >>"$SERVERLOG" 2>>"$SERVERLOG" &
    PIDS+=($!)

    ARGS=(--name "v0-baseline" --url "http://127.0.0.1:${NUESTRO}")
    for s in 1 2 3; do
        RIVAL=$((BASE_PORT + 2 + s))
        # `--cauta`: rivales que sobreviven. Con suicidas la partida dura siete turnos,
        # no se llega al primer shrink y la muestra de latencias no da para un p99.
        python3 tests/replay/chaos_snake.py "$RIVAL" "soak-$SEED-$s" --cauta >/dev/null 2>&1 &
        PIDS+=($!)
        ARGS+=(--name "caos-$s" --url "http://127.0.0.1:${RIVAL}")
    done

    listo=0
    for _ in $(seq 1 300); do
        listo=1
        curl -fsS "http://127.0.0.1:${NUESTRO}/" >/dev/null 2>&1 || listo=0
        for s in 1 2 3; do
            curl -fsS "http://127.0.0.1:$((BASE_PORT + 2 + s))/" >/dev/null 2>&1 || listo=0
        done
        [[ $listo -eq 1 ]] && break
        sleep 0.1
    done
    if [[ $listo -eq 0 ]]; then
        echo "FAIL partida $g: las snakes no respondieron" >&2
        limpia
        FALLOS=$((FALLOS + 1))
        continue
    fi

    "$CLI" play -W 11 -H 11 -g royale -m royale -t "$TIMEOUT_MS" -r "$SEED" \
        "${ARGS[@]}" -o "$OUT/g$(printf '%05d' "$g").jsonl" >/dev/null 2>&1
    limpia
    printf '.'
    [[ $(((g + 1) % 50)) -eq 0 ]] && printf ' %d\n' $((g + 1))
done
echo

# ---------------------------------------------------------------- analisis
python3 scripts/soak_analiza.py "$OUT" "$TIMEOUT_MS"
RC=$?

echo "resultados en $OUT"
[[ $FALLOS -eq 0 ]] || { echo "$FALLOS partidas no llegaron a jugarse" >&2; exit 1; }
exit $RC
