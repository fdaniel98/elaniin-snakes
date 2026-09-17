#!/usr/bin/env bash
# Genera el corpus del test diferencial: partidas reales del arbitro oficial con sus
# movimientos anotados.
#
#   ./scripts/gen-replays.sh --games N --out DIR [--seed-base N] [--only-chaos]
#
# Por cada partida deja tres cosas en DIR:
#   <id>.jsonl        el JSONL del arbitro, tal cual
#   <id>.moves.jsonl  lo que respondio cada snake en cada turno, del proxy grabador
#   index.json        semilla y parametros de cada partida, para regenerarlas
#
# El JSONL del arbitro no trae los movimientos y la serpiente que muere desaparece del
# turno siguiente, asi que sin el grabador el caso mas interesante del diferencial es
# irreproducible. ver tests/replay/recorder.py
#
# Las partidas mezclan nuestra v0 (sobrevive y llega a los shrinks) con snakes de caos
# (mueren de todas las formas posibles). ver tests/replay/chaos_snake.py
set -uo pipefail

cd "$(dirname "$0")/.."

GAMES=10
OUT=""
SEED_BASE=1000
ONLY_CHAOS=0
while [[ $# -gt 0 ]]; do
    case "$1" in
        --games) GAMES="$2"; shift 2 ;;
        --out) OUT="$2"; shift 2 ;;
        --seed-base) SEED_BASE="$2"; shift 2 ;;
        --only-chaos) ONLY_CHAOS=1; shift ;;
        *) echo "argumento desconocido: $1" >&2; exit 2 ;;
    esac
done
[[ -n "$OUT" ]] || { echo "falta --out DIR" >&2; exit 2; }

RULES_SHA="$(grep -oE '[0-9a-f]{40}' docs/SOURCES.md | head -1)"
SERVER=build/release/bin/battlesnake-server
mkdir -p "$OUT"

die() { echo "ERROR $*" >&2; exit 2; }

# ---------------------------------------------------------------- arbitro oficial
CLI="$(command -v battlesnake || true)"
if [[ -z "$CLI" && -n "$(command -v go || true)" && -x "$(go env GOPATH)/bin/battlesnake" ]]; then
    CLI="$(go env GOPATH)/bin/battlesnake"
fi
if [[ -z "$CLI" ]]; then
    command -v go >/dev/null 2>&1 || die "falta el CLI oficial y no hay go para compilarlo"
    echo "-- compilando el arbitro oficial en $RULES_SHA --"
    if ! go install "github.com/BattlesnakeOfficial/rules/cli/battlesnake@${RULES_SHA}" 2>/dev/null; then
        # Algunos entornos (el contenedor de Cowork) bloquean proxy.golang.org y los
        # dominios de vanidad golang.org/x, gopkg.in y go.uber.org. Se clona el motor
        # del SHA fijado y se apuntan esos modulos a su espejo en GitHub, en la MISMA
        # version que declara su go.mod. Ninguno es el motor de reglas: son cobra,
        # viper, fsnotify y websocket. ver docs/SOURCES.md#s-02
        echo "-- el proxy de modulos no responde; se compila desde el clon con espejos --"
        bash scripts/build-referee-mirrored.sh || die "no se pudo compilar el arbitro"
        CLI="$PWD/build/tools/battlesnake"
    else
        CLI="$(go env GOPATH)/bin/battlesnake"
    fi
fi
[[ -x "$CLI" ]] || die "el arbitro no es ejecutable: $CLI"
echo "arbitro: $CLI"

if [[ $ONLY_CHAOS -eq 0 && ! -x "$SERVER" ]]; then
    die "falta $SERVER; corre 'cmake --build --preset release' o usa --only-chaos"
fi

PIDS=()
limpia() {
    for pid in "${PIDS[@]:-}"; do
        [[ -n "$pid" ]] && kill "$pid" 2>/dev/null
    done
    PIDS=()
}
trap 'limpia; exit 130' INT TERM

# Matriz de escenarios. Varia lo que cambia de reglas, no lo decorativo: tamaño de
# tablero (el motor esta instanciado en 7, 11 y 19), numero de serpientes, cada cuanto
# encoge el mapa y cuanto duele el hazard.
#   ancho alto serpientes v0s shrink hazard comida minimo ruleset mapa
ESCENARIOS=(
    "11 11 4 4 5  14  15 1 royale royale"
    "11 11 4 1 5  14  15 1 royale royale"
    "11 11 4 0 2  14  25 2 royale royale"
    "11 11 4 4 3  25  15 1 royale royale"
    "11 11 3 3 25 14  15 1 royale royale"
    "11 11 3 1 25 14  15 1 royale royale"
    "11 11 2 1 3  100 15 1 royale royale"
    "7  7  4 0 2  14  30 1 royale royale"
    "7  7  4 4 4  14  15 1 royale royale"
    "7  7  2 1 4  14  15 1 royale royale"
    "19 19 4 4 8  14  15 1 royale royale"
    "19 19 4 1 8  14  15 1 royale royale"
    "19 19 4 0 3  14  20 3 royale royale"
    "11 11 4 0 4  0   15 1 royale royale"
    "11 11 4 4 6  14  10 1 royale royale"
    "11 11 3 0 2  14  15 1 standard standard"
    # Sin comida las longitudes no cambian, asi que TODO choque cabeza a cabeza es un
    # empate y mueren las dos: es la unica forma de que el corpus cubra ese caso.
    # ver docs/rules.md#r-08
    "7  7  4 0 3  14  0  0 royale royale"
    "11 11 4 0 5  14  0  0 royale royale"
    "7  7  4 0 99 14  0  0 royale royale"
    "11 11 4 0 99 0   0  0 royale royale"
    # Cada escenario de aqui abajo existe para cubrir una causa de muerte que el resto
    # del corpus no producia. Un corpus que no mata de hambre no verifica el hambre.
    "11 11 4 4 99 14  0  0 royale royale"
    "11 11 4 0 2  100 0  0 royale royale"
    "11 11 4 0 25 14  100 5 royale royale"
    "7  7  4 0 25 14  100 4 royale royale"
)

INDEX="$OUT/index.json"
echo "[" > "$INDEX"
PRIMERA=1
FALLOS=0

for ((g = 0; g < GAMES; ++g)); do
    IFS=' ' read -r W H NSNAKES NV0 SHRINK HAZARD FOOD MINFOOD RULESET MAPA <<<"${ESCENARIOS[$((g % ${#ESCENARIOS[@]}))]}"
    SEED=$((SEED_BASE + g))
    ID="$(printf 'g%05d' "$g")"
    MOVES="$OUT/$ID.moves.jsonl"
    : > "$MOVES"

    arranca() {
        ARGS=()
        PUERTO_BASE=$((9000 + (g % 40) * 20 + intento * 800))
        for ((s = 0; s < NSNAKES; ++s)); do
            PUERTO_SNAKE=$((PUERTO_BASE + s * 2))
            PUERTO_PROXY=$((PUERTO_BASE + s * 2 + 1))
            if [[ $s -lt $NV0 ]]; then
                PORT=$PUERTO_SNAKE "$SERVER" >/dev/null 2>&1 &
                PIDS+=($!)
                NOMBRE="v0-$s"
            else
                # Todas las de caos responden mal de vez en cuando: sin eso la rama del
                # LastMove del arbitro apenas se ejercita. ver docs/rules.md#r-03
                if [[ $((s % 3)) -eq 2 ]]; then
                    EXTRA=(--invalid 0.20 --slow 0.08 --slow-ms $((600 + s * 50)))
                else
                    EXTRA=(--invalid 0.08 --slow 0.03 --slow-ms $((600 + s * 50)))
                fi
                python3 tests/replay/chaos_snake.py "$PUERTO_SNAKE" "$SEED-$s" "${EXTRA[@]}" >/dev/null 2>&1 &
                PIDS+=($!)
                NOMBRE="caos-$s"
            fi
            python3 tests/replay/recorder.py "$PUERTO_PROXY" "http://127.0.0.1:${PUERTO_SNAKE}" "$MOVES" >/dev/null 2>&1 &
            PIDS+=($!)
            ARGS+=(--name "$NOMBRE" --url "http://127.0.0.1:${PUERTO_PROXY}")
        done

        for _ in $(seq 1 300); do
            listo=1
            for ((s = 0; s < NSNAKES; ++s)); do
                curl -fsS "http://127.0.0.1:$((PUERTO_BASE + s * 2 + 1))/" >/dev/null 2>&1 || listo=0
            done
            [[ $listo -eq 1 ]] && return 0
            sleep 0.1
        done
        return 1
    }

    # Arrancar ocho procesos en dos nucleos falla de vez en cuando por tiempo, no por el
    # codigo. Un reintento con otros puertos mantiene el corpus reproducible: sin el,
    # regenerar con las mismas semillas puede dar un numero de partidas distinto.
    listo=0
    for intento in 0 1 2; do
        : > "$MOVES"
        if arranca; then
            listo=1
            break
        fi
        echo "reintento $ID (intento $intento)" >&2
        limpia
        sleep 2
    done
    if [[ $listo -eq 0 ]]; then
        echo "FAIL $ID: las snakes no respondieron en 30 s" >&2
        limpia
        FALLOS=$((FALLOS + 1))
        continue
    fi

    # Flags verificados en cli/commands/play.go del SHA fijado. ver docs/rules-parametros.md#r-20
    "$CLI" play \
        -W "$W" -H "$H" \
        -g "$RULESET" -m "$MAPA" \
        -t 500 -r "$SEED" \
        --shrinkEveryNTurns "$SHRINK" \
        --hazardDamagePerTurn "$HAZARD" \
        --foodSpawnChance "$FOOD" \
        --minimumFood "$MINFOOD" \
        "${ARGS[@]}" \
        -o "$OUT/$ID.jsonl" >/dev/null 2>&1
    limpia

    if [[ ! -s "$OUT/$ID.jsonl" ]]; then
        echo "FAIL $ID: sin JSONL" >&2
        FALLOS=$((FALLOS + 1))
        continue
    fi

    [[ $PRIMERA -eq 0 ]] && echo "," >> "$INDEX"
    PRIMERA=0
    printf '  {"id":"%s","seed":%d,"width":%d,"height":%d,"snakes":%d,"v0":%d,"shrinkEveryNTurns":%d,"hazardDamagePerTurn":%d,"foodSpawnChance":%d,"minimumFood":%d,"ruleset":"%s","map":"%s","turns":%d}' \
        "$ID" "$SEED" "$W" "$H" "$NSNAKES" "$NV0" "$SHRINK" "$HAZARD" "$FOOD" "$MINFOOD" "$RULESET" "$MAPA" \
        "$(($(wc -l <"$OUT/$ID.jsonl") - 2))" >> "$INDEX"
    printf '.'
done

echo "" >> "$INDEX"
echo "]" >> "$INDEX"
echo
echo "partidas: $((GAMES - FALLOS)) de $GAMES en $OUT"
[[ $FALLOS -eq 0 ]] || exit 1
