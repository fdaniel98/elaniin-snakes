#!/usr/bin/env bash
# Prueba que el gate SIRVE: inyecta un veneno por cada check sobre una copia temporal
# del repo y falla si el gate no falla en ese check.
#
# Sin esto, "el gate pasa en verde" es una afirmacion del agente, no un hecho: un check
# que pasa por estar vacio se ve exactamente igual que uno que pasa por funcionar.
# ver docs/harness.md#h-02
#
#   ./scripts/gate-selftest.sh            todos los venenos
#   ./scripts/gate-selftest.sh --fast     solo los que cubre `gate.sh --fast`
#   ./scripts/gate-selftest.sh 6          solo el veneno del check 6
set -uo pipefail

cd "$(dirname "$0")/.."
REPO="$PWD"

ONLY="${1:-}"
FAST_ONLY=0
[[ "$ONLY" == "--fast" ]] && {
    FAST_ONLY=1
    ONLY=""
}

# Checks que cubre `gate.sh --fast`.
FAST_CHECKS=(0 1 3 4 6 7 9)

is_fast_check() {
    local needle="$1"
    for check in "${FAST_CHECKS[@]}"; do
        [[ "$check" == "$needle" ]] && return 0
    done
    return 1
}

docker_available() {
    (command -v docker >/dev/null 2>&1 && docker version >/dev/null 2>&1) ||
        (command -v docker.exe >/dev/null 2>&1 && docker.exe version >/dev/null 2>&1)
}

poison_0() { printf '\r\n' >>scripts/bench.sh; }

poison_1() { echo 'int veneno( {' >>engine/src/rules.cpp; }

poison_2() {
    # Solo rompe en debug: release define NDEBUG. Asi se distingue el check 2 del 1.
    cat >>engine/src/rules.cpp <<'EOF'
#ifndef NDEBUG
#error "veneno del check 2: el build debug debe fallar"
#endif
EOF
}

poison_3() {
    cat >>tests/test_rules.cpp <<'EOF'

TEST_CASE("veneno del check 3", "[veneno]") { REQUIRE(1 == 2); }
EOF
}

poison_4() { printf 'namespace engine {   int    sin_formatear   =    1 ;  }\n' >>engine/src/rules.cpp; }

poison_5() {
    # modernize-use-nullptr: compila sin avisos del compilador, pero clang-tidy lo caza.
    cat >>engine/src/rules.cpp <<'EOF'

namespace engine {
const int* veneno_tidy() {
    const int* p = 0;
    return p;
}
} // namespace engine
EOF
}

poison_6() {
    printf '# Documento sin front-matter\n\nEsto deberia romper el lint de docs.\n' \
        >docs/veneno.md
}

poison_6b() {
    # Cita a un anchor inexistente desde un .md que no esta bajo docs/.
    printf '\n<!-- ver docs/rules.md#r-no-existe -->\n' >>STATE.md
}

poison_6c() {
    # size_bytes que no coincide con wc -c.
    sed -i 's/^size_bytes: .*/size_bytes: 1/' docs/glossary.md
}

poison_6d() {
    # canonical sin last_verified.
    sed -i '/^last_verified:/d' docs/rules.md
}

poison_7() {
    # -march=native en el preset del que deploy HEREDA: el grep textual sobre el bloque
    # `deploy` no lo ve; solo los flags efectivos lo delatan.
    python3 - <<'EOF'
import json
with open("CMakePresets.json", encoding="utf-8") as fh:
    doc = json.load(fh)
for preset in doc["configurePresets"]:
    if preset["name"] == "base":
        # CMAKE_CXX_FLAGS_RELEASE, no CMAKE_CXX_FLAGS: el preset deploy define el
        # segundo y lo sobreescribiria, mientras que el primero se hereda intacto y
        # acaba en los flags efectivos. Justo el caso que el grep textual no ve.
        preset.setdefault("cacheVariables", {})["CMAKE_CXX_FLAGS_RELEASE"] = "-O3 -march=native"
with open("CMakePresets.json", "w", encoding="utf-8", newline="\n") as fh:
    json.dump(doc, fh, indent=2)
    fh.write("\n")
EOF
}

poison_8() {
    # El servidor responde siempre "up", que es ilegal en algun fixture.
    sed -i 's|const json reply{{"move", direction_name(move.direction)}, {"shout", ""}};|const json reply{{"move", "up"}, {"shout", ""}};|' \
        snake/src/server.cpp
}

poison_9() {
    # Ledger con dos iteraciones: por debajo del minimo.
    local ledger
    ledger="$(find .loop -name '*.ledger.json' | head -1)"
    [[ -n "$ledger" ]] || return 1
    python3 - "$ledger" <<'EOF'
import json, sys
path = sys.argv[1]
with open(path, encoding="utf-8") as fh:
    doc = json.load(fh)
doc["iterations"] = doc["iterations"][:2]
with open(path, "w", encoding="utf-8", newline="\n") as fh:
    json.dump(doc, fh, indent=2)
    fh.write("\n")
EOF
}

poison_9b() {
    # Encadenamiento roto: commit_after(i1) deja de ser commit_before(i2).
    local ledger
    ledger="$(find .loop -name '*.ledger.json' | head -1)"
    [[ -n "$ledger" ]] || return 1
    python3 - "$ledger" <<'EOF'
import json, sys
path = sys.argv[1]
with open(path, encoding="utf-8") as fh:
    doc = json.load(fh)
doc["iterations"][1]["commit_before"] = "0" * 40
with open(path, "w", encoding="utf-8", newline="\n") as fh:
    json.dump(doc, fh, indent=2)
    fh.write("\n")
EOF
}

poison_10() {
    sed -i 's|^FROM gcr.io/distroless/cc-debian12:nonroot|FROM gcr.io/distroless/static-debian12:nonroot|' \
        deploy/Dockerfile
}

# veneno | check esperado | descripcion
POISONS=(
    "poison_0|0|un .sh con CR"
    "poison_1|1|error de sintaxis en el motor"
    "poison_2|2|codigo que solo rompe el build debug"
    "poison_3|3|un test que falla"
    "poison_4|4|codigo sin formatear"
    "poison_5|5|aviso de clang-tidy"
    "poison_6|6|doc sin front-matter"
    "poison_6b|6|cita a un anchor inexistente desde STATE.md"
    "poison_6c|6|size_bytes que no coincide con wc -c"
    "poison_6d|6|doc canonical sin last_verified"
    "poison_7|7|-march=native en un preset del que deploy hereda"
    "poison_8|8|el servidor devuelve un movimiento ilegal"
    "poison_9|9|ledger con solo dos iteraciones"
    "poison_9b|9|ledger con el encadenamiento de commits roto"
    "poison_10|10|runtime distroless sin libstdc++"
)

passed=0
failed=0

for entry in "${POISONS[@]}"; do
    IFS='|' read -r fn check description <<<"$entry"

    [[ -n "$ONLY" && "$ONLY" != "$check" ]] && continue
    if [[ $FAST_ONLY -eq 1 ]] && ! is_fast_check "$check"; then
        continue
    fi
    if [[ "$check" == "10" ]] && ! docker_available; then
        echo "SKIP  check $check ($description): sin docker"
        continue
    fi

    work="$(mktemp -d)"
    git archive HEAD | tar -x -C "$work"
    # Los ledgers del loop no van en `git archive` si estan sin commitear: se copian.
    [[ -d .loop ]] && cp -r .loop "$work/" 2>/dev/null
    cp -r .git "$work/.git"

    pushd "$work" >/dev/null
    if ! "$fn"; then
        echo "ERROR no se pudo aplicar el veneno de $description"
        popd >/dev/null
        rm -rf "$work"
        failed=$((failed + 1))
        continue
    fi

    mode=()
    if is_fast_check "$check"; then
        mode=(--fast)
    fi

    output="$(./scripts/gate.sh "${mode[@]}" 2>&1)"
    gate_status=$?

    if [[ $gate_status -eq 0 ]]; then
        echo "FALLO check $check ($description): el gate paso con el veneno aplicado"
        failed=$((failed + 1))
    elif grep -qE "^CHECK ${check} .* FAIL" <<<"$output"; then
        echo "OK    check $check ($description): el gate lo caza"
        passed=$((passed + 1))
    else
        echo "FALLO check $check ($description): el gate fallo, pero no en el check $check"
        grep -E '^CHECK ' <<<"$output" | tail -5
        failed=$((failed + 1))
    fi
    popd >/dev/null
    rm -rf "$work"
done

echo
echo "venenos cazados: $passed, fallidos: $failed"
[[ $failed -eq 0 ]]
