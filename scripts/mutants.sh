#!/usr/bin/env bash
# Prueba de mutantes de la clase `robustness` del loop.
#
# Inyecta mutaciones de una linea en reglas y cerebro, reconstruye y corre los tests:
# un mutante esta MUERTO si los tests fallan. Un mutante VIVO significa que ninguna
# asercion cubre ese comportamiento, no que el codigo este bien.
#
# El trabajo se hace sobre una copia en el filesystem de Linux: compilar en /mnt/c es
# varias veces mas lento por el 9p de WSL2.
#
#   ./scripts/mutants.sh [--json salida.json]
set -uo pipefail

cd "$(dirname "$0")/.."
REPO="$PWD"

JSON_OUT=""
[[ "${1:-}" == "--json" ]] && JSON_OUT="${2:-}"

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

# id | archivo | patron sed | descripcion
MUTANTS=(
    "m1|engine/src/rules.cpp|s/snake.length <= other.length/snake.length < other.length/|cabeza a cabeza: empate de longitudes deja de matar a ambas"
    "m2|engine/src/rules.cpp|s/if (s.food.test(head)) {/if (false) {/|el hazard pasa a dañar aunque haya comida en la casilla"
    "m3|engine/include/engine/state.hpp|s/return length >= 2 \&\&/return false \&\&/|la cola apilada deja de detectarse"
    "m4|snake/src/brain_v0.cpp|s/if (seg == last \&\& !other.tail_is_stacked()) {/if (false) {/|el cerebro nunca considera libre la casilla de cola"
    "m5|engine/src/rules.cpp|s/- health_loss_per_turn/- 0/|el hambre deja de restar salud"
    "m6|engine/src/rules.cpp|s/std::clamp(health, 0, max_health)/std::clamp(health, 1, max_health)/|el daño de hazard nunca puede matar"
    "m7|engine/src/royale_map.cpp|s/if (turn < shrink_every_n_turns) {/if (turn <= shrink_every_n_turns) {/|el primer shrink llega un turno tarde"
    "m8|engine/src/royale_map.cpp|s/const int num_shrinks = turn \/ shrink_every_n_turns;/const int num_shrinks = turn \/ shrink_every_n_turns + 1;/|el rectangulo encoge un shrink de mas"
    "m9|engine/src/royale_map.cpp|s/Rng rng(seed);/Rng rng(seed + 1);/|el schedule de shrink usa otra semilla"
    "m10|engine/src/royale_map.cpp|s/++min_x;/++min_x, ++min_y;/|un shrink mueve dos bordes en vez de uno"
    "m11|tests/replay/replay_harness.hpp|s/if (respuesta.status != 200) {/if (false) {/|el replay acepta respuestas que el arbitro rechazo"
)

echo "== preparando copia limpia en $WORK =="
git -C "$REPO" archive HEAD | tar -x -C "$WORK" || {
    echo "ERROR no se pudo exportar el arbol con git archive"
    exit 2
}

cd "$WORK"
echo "== build de referencia =="
if ! cmake --preset release >/dev/null 2>&1 || ! cmake --build --preset release >/dev/null 2>&1; then
    echo "ERROR el build de referencia falla; no tiene sentido mutar"
    exit 2
fi
if ! ./build/release/bin/unit_tests >/dev/null 2>&1; then
    echo "ERROR los tests de referencia ya fallan"
    exit 2
fi
echo "OK   referencia verde"

# Restaurar con `mv` devuelve el fichero con su fecha original, que es ANTERIOR a los
# objetos compilados con el mutante dentro. Para un .cpp da igual -el siguiente mutante
# lo vuelve a tocar-, pero para una CABECERA es veneno: ninja no ve nada que rehacer y
# la mutacion se queda dentro del binario para todos los mutantes siguientes, que pasan
# a morir por el mutante anterior y no por el suyo. `touch` fuerza la reconstruccion.
restaura() {
    mv "$1.orig" "$1"
    touch "$1"
}

total=0
killed=0
broken=0
survivors=()

for entry in "${MUTANTS[@]}"; do
    IFS='|' read -r id file pattern description <<<"$entry"
    total=$((total + 1))

    if [[ ! -f "$file" ]]; then
        echo "ARNES_ROTO $id: no existe $file"
        broken=$((broken + 1))
        survivors+=("$id (archivo inexistente: arnes roto)")
        continue
    fi

    cp "$file" "$file.orig"
    sed -i "$pattern" "$file"
    if diff -q "$file" "$file.orig" >/dev/null; then
        echo "ARNES_ROTO $id: el patron sed no cambio nada en $file"
        echo "  (el codigo cambio de forma y el mutante dejo de aplicarse; no es un"
        echo "   mutante vivo, es el arnes de mutantes degradado en silencio)"
        restaura "$file"
        broken=$((broken + 1))
        survivors+=("$id (patron no aplicado: arnes roto)")
        continue
    fi

    if ! cmake --build --preset release >/dev/null 2>&1; then
        # Un mutante que no compila NO es un mutante muerto: ninguna asercion lo mato,
        # lo rechazo el compilador. Contarlo como muerto infla el ratio y esconde que
        # ese comportamiento no esta cubierto por ningun test. Cuenta como arnes roto y
        # sale del denominador.
        echo "ARNES_ROTO $id: no compila - $description"
        echo "  (reescribe el mutante para que compile; mientras tanto no mide nada)"
        restaura "$file"
        broken=$((broken + 1))
        survivors+=("$id (no compila: arnes roto)")
        continue
    elif ./build/release/bin/unit_tests >/dev/null 2>&1; then
        echo "VIVO     $id - $description"
        survivors+=("$id: $description")
    else
        echo "MUERTO   $id - $description"
        killed=$((killed + 1))
    fi

    restaura "$file"
done

cmake --build --preset release >/dev/null 2>&1

# El denominador son los mutantes validos: uno que no se aplico o no compilo no mide
# nada, ni a favor ni en contra.
validos=$((total - broken))
ratio="$(awk -v k="$killed" -v v="$validos" 'BEGIN{printf "%.4f", (v ? k/v : 0)}')"
echo
echo "mutantes=$total validos=$validos muertos=$killed patrones_rotos=$broken ratio=$ratio"
for survivor in "${survivors[@]:-}"; do
    [[ -n "$survivor" ]] && echo "SUPERVIVIENTE $survivor"
done

if [[ -n "$JSON_OUT" ]]; then
    case "$JSON_OUT" in
        /*) out="$JSON_OUT" ;;
        *) out="$REPO/$JSON_OUT" ;;
    esac
    printf '{\n  "mutants": %d,\n  "mutants_valid": %d,\n  "mutants_killed": %d,\n  "mutants_broken_patterns": %d,\n  "mutants_killed_ratio": %s\n}\n' \
        "$validos" "$validos" "$killed" "$broken" "$ratio" >"$out"
    echo "metricas escritas en $out"
fi

# Un patron roto invalida la corrida entera: sin el, el ratio miente por arriba.
[[ $broken -eq 0 ]] || exit 1
awk -v k="$killed" -v t="$total" 'BEGIN{exit !(t > 0 && k/t >= 0.8)}'
