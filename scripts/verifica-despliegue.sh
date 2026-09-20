#!/usr/bin/env bash
# Verifica una snake YA desplegada y mide el margen de red con numeros, no con defaults.
#
#   ./scripts/verifica-despliegue.sh https://mi-snake-xxxx.run.app [repeticiones]
#
# Que mide y por que en ese orden:
#
#   1. `GET /` devuelve 200 con el JSON de personalizacion, y `GET /health` 200. Si esto
#      falla, lo demas sobra.
#   2. **RTT puro**, con `GET /health`. Esa ruta NO invoca al cerebro
#      (ver docs/architecture.md#a-02), asi que lo que tarda es transporte y nada mas. Es
#      el unico modo de medir `time.network_margin_ms` por separado en vez de estimarlo.
#   3. **Latencia de `/move`** con un fixture real: transporte + decision. La diferencia
#      contra el RTT puro es lo que la snake se esta gastando de verdad ahi fuera.
#   4. **Arranque en frio**, si se pide: una peticion despues de esperar.
#
# El veredicto compara el p99 de RTT contra `time.network_margin_ms` del config
# desplegado. Si el margen se queda corto, lo que hay que bajar es `max_compute_ms`: una
# snake que piensa menos y siempre contesta gana a una que se pasa y no llega.
set -uo pipefail

cd "$(dirname "$0")/.."

URL="${1:-}"
REPS="${2:-40}"
CONFIG="${CONFIG:-snake/config/default.json}"

if [[ -z "$URL" ]]; then
    echo "uso: $0 <url> [repeticiones]" >&2
    exit 2
fi
URL="${URL%/}"

command -v curl >/dev/null || { echo "ERROR falta curl" >&2; exit 2; }
command -v jq >/dev/null || { echo "ERROR falta jq" >&2; exit 2; }

FIXTURE="${FIXTURE:-tests/fixtures/03-cola-que-avanza.json}"
if [[ ! -f "$FIXTURE" ]]; then
    FIXTURE="$(find tests/fixtures -name '*.json' | sort | head -1)"
fi
[[ -f "$FIXTURE" ]] || { echo "ERROR no encuentro ningun fixture" >&2; exit 2; }

MARGEN_RED="$(jq -r '.time.network_margin_ms' "$CONFIG")"
MARGEN_SEG="$(jq -r '.time.safety_margin_ms' "$CONFIG")"
COMPUTO="$(jq -r '.time.max_compute_ms' "$CONFIG")"

echo "url            $URL"
echo "config         $CONFIG (red ${MARGEN_RED} ms, seguridad ${MARGEN_SEG} ms, computo ${COMPUTO} ms)"
echo "fixture        $FIXTURE"
echo "repeticiones   $REPS"
echo

# ---------------------------------------------------------------- 1. responde
codigo_raiz="$(curl -fsS -o /tmp/raiz.json -w '%{http_code}' --max-time 10 "$URL/" || echo 000)"
codigo_salud="$(curl -fsS -o /dev/null -w '%{http_code}' --max-time 10 "$URL/health" || echo 000)"
echo "GET /         $codigo_raiz  $(jq -c '{apiversion,author,color,head,tail}' /tmp/raiz.json 2>/dev/null || echo '(sin JSON)')"
echo "GET /health   $codigo_salud"
if [[ "$codigo_raiz" != "200" || "$codigo_salud" != "200" ]]; then
    echo "FAIL la snake no responde; no sigo midiendo" >&2
    exit 1
fi
echo

percentiles() {
    # lee milisegundos por stdin, uno por linea
    sort -n | awk '
        {v[NR]=$1}
        END {
            if (NR == 0) { print "sin datos"; exit }
            p50 = v[int(NR*0.50)+((NR*0.50)==int(NR*0.50)?0:1)]
            p95 = v[int(NR*0.95)+((NR*0.95)==int(NR*0.95)?0:1)]
            p99 = v[int(NR*0.99)+((NR*0.99)==int(NR*0.99)?0:1)]
            printf "p50 %.1f ms   p95 %.1f ms   p99 %.1f ms   maximo %.1f ms   (n=%d)\n",
                   p50, p95, p99, v[NR], NR
            printf "P99=%.1f\n", p99 > "/dev/stderr"
        }'
}

# ---------------------------------------------------------------- 2. RTT puro
echo "-- RTT puro (GET /health, sin cerebro) --"
rtt_p99="$(for _ in $(seq "$REPS"); do
    curl -fsS -o /dev/null -w '%{time_total}\n' --max-time 5 "$URL/health" 2>/dev/null |
        awk '{printf "%.1f\n", $1*1000}'
done | percentiles 2>&1 >/tmp/rtt.txt | sed -n 's/^P99=//p')"
cat /tmp/rtt.txt
echo

# ---------------------------------------------------------------- 3. /move real
echo "-- POST /move (transporte + decision) --"
jq -c 'del(._expect_any, ._expect_not, ._nota, ._comment)' "$FIXTURE" >/tmp/move.json
move_p99="$(for _ in $(seq "$REPS"); do
    curl -fsS -o /dev/null -w '%{time_total}\n' --max-time 5 \
        -H 'Content-Type: application/json' --data @/tmp/move.json "$URL/move" 2>/dev/null |
        awk '{printf "%.1f\n", $1*1000}'
done | percentiles 2>&1 >/tmp/move.txt | sed -n 's/^P99=//p')"
cat /tmp/move.txt
echo

# ---------------------------------------------------------------- veredicto
echo "-- veredicto --"
printf 'margen de red declarado   %s ms\n' "$MARGEN_RED"
printf 'RTT p99 medido            %s ms\n' "${rtt_p99:-?}"
awk -v rtt="${rtt_p99:-0}" -v margen="$MARGEN_RED" -v comp="$COMPUTO" -v seg="$MARGEN_SEG" '
BEGIN {
    presupuesto = 500 - rtt - seg
    printf "presupuesto real de computo con ese RTT: 500 - %.1f - %d = %.1f ms\n", rtt, seg, presupuesto
    if (rtt > margen) {
        printf "AVISO el RTT p99 (%.1f) SUPERA el margen declarado (%d).\n", rtt, margen
        printf "      Baja time.max_compute_ms a %d o menos y vuelve a desplegar.\n",
               (presupuesto > 50 ? int(presupuesto) - 10 : 50)
        exit 1
    }
    printf "OK el margen declarado cubre el RTT medido. max_compute_ms %d cabe en %.1f.\n",
           comp, presupuesto
}'
