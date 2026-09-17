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
    "m4|snake/src/brain_v0.cpp|s/if (seg == last \&\& !other.tail_is_stacked()) {/if (seg == last) {/|el cerebro trata toda cola como libre"
    "m5|engine/src/rules.cpp|s/- health_loss_per_turn/- 0/|el hambre deja de restar salud"
    "m6|engine/src/rules.cpp|s/std::clamp(health, 0, max_health)/std::clamp(health, 1, max_health)/|el daño de hazard nunca puede matar"
    "m7|engine/src/royale_map.cpp|s/if (turn < shrink_every_n_turns) {/if (false) {/|hay hazards antes del primer shrink"
    "m8|engine/src/royale_map.cpp|s/const int num_shrinks = turn \/ shrink_every_n_turns;/const int num_shrinks = turn \/ shrink_every_n_turns + 1;/|el rectangulo encoge un shrink de mas"
    "m9|engine/src/royale_map.cpp|s/Rng rng(seed);/Rng rng(1);/|el schedule de shrink ignora la semilla"
    "m10|engine/src/royale_map.cpp|s/++min_x;/++min_x, ++min_y;/|un shrink mueve dos bordes en vez de uno"
    "m11|tests\/replay\/replay_harness.hpp|s/if (respuesta.status != 200) {/if (false) {/|el replay acepta respuestas que el arbitro rechazo"
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

total=0
killed=0
broken=0
survivors=()

for entry in "${MUTANTS[@]}"; do
    IFS='|' read -r id file pattern description <<<"$entry"
    total=$((total + 1))

    cp "$file" "$file.orig"
    sed -i "$pattern" "$file"
    if diff -q "$file" "$file.orig" >/dev/null; then
        echo "ARNES_ROTO $id: el patron sed no cambio nada en $file"
        echo "  (el codigo cambio de forma y el mutante dejo de aplicarse; no es un"
        echo "   mutante vivo, es el arnes de mutantes degradado en silencio)"
        mv "$file.orig" "$file"
        broken=$((broken + 1))
        survivors+=("$id (patron no aplicado: arnes roto)")
        continue
    fi

    if ! cmake --build --preset release >/dev/null 2>&1; then
        # Si no compila, el mutante tambien esta muerto: el cambio no pasa el build.
        echo "MUERTO   $id (no compila) - $description"
        killed=$((killed + 1))
    elif ./build/release/bin/unit_tests >/dev/null 2>&1; then
        echo "VIVO     $id - $description"
        survivors+=("$id: $description")
    else
        echo "MUERTO   $id - $description"
        killed=$((killed + 1))
    fi

    mv "$file.orig" "$file"
done

cmake --build --preset release >/dev/null 2>&1

ratio="$(awk -v k="$killed" -v t="$total" 'BEGIN{printf "%.4f", (t ? k/t : 0)}')"
echo
echo "mutantes=$total muertos=$killed patrones_rotos=$broken ratio=$ratio"
for survivor in "${survivors[@]:-}"; do
    [[ -n "$survivor" ]] && echo "SUPERVIVIENTE $survivor"
done

if [[ -n "$JSON_OUT" ]]; then
    case "$JSON_OUT" in
        /*) out="$JSON_OUT" ;;
        *) out="$REPO/$JSON_OUT" ;;
    esac
    printf '{\n  "mutants": %d,\n  "mutants_killed": %d,\n  "mutants_broken_patterns": %d,\n  "mutants_killed_ratio": %s\n}\n' \
        "$total" "$killed" "$broken" "$ratio" >"$out"
    echo "metricas escritas en $out"
fi

# Un patron roto invalida la corrida entera: sin el, el ratio miente por arriba.
[[ $broken -eq 0 ]] || exit 1
awk -v k="$killed" -v t="$total" 'BEGIN{exit !(t > 0 && k/t >= 0.8)}'
