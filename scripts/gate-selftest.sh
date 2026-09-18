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

# Guard de entorno. Media docena de checks llaman a git (ls-files, cat-file, archive,
# merge-base). Si git rechaza el repositorio -lo normal al trabajar desde /mnt/c, donde
# el propietario de los archivos no es quien ejecuta- todos ellos fallan y el gate acusa
# al codigo de cosas que no pasan. Eso es un error de ENTORNO: salida 2, no 1.
if ! git rev-parse HEAD >/dev/null 2>&1; then
    echo "ERROR de entorno: git no puede leer este repositorio." >&2
    git rev-parse HEAD 2>&1 | sed 's/^/  /' >&2
    echo "  Si el mensaje habla de 'dubious ownership', ejecuta una vez:" >&2
    printf "    git config --global --add safe.directory '%s'\n" "$PWD" >&2
    exit 2
fi

ONLY="${1:-}"
FAST_ONLY=0
[[ "$ONLY" == "--fast" ]] && {
    FAST_ONLY=1
    ONLY=""
}

# Checks que cubre `gate.sh --fast`.
FAST_CHECKS=(0 1 3 4 6 7 9 11)

is_fast_check() {
    local needle="$1"
    # `local`: sin el, esta funcion pisaba la variable `check` del bucle principal y los
    # venenos de los checks 2, 5, 8 y 10 se evaluaban contra el numero equivocado.
    local candidate
    for candidate in "${FAST_CHECKS[@]}"; do
        [[ "$candidate" == "$needle" ]] && return 0
    done
    return 1
}

# Ledger que el check 9 SI verifica: el del primer entregable declarado en el bloque
# loop-deliverables de STATE.md. Elegirlo con `find ... | head -1` dependia del orden de
# lectura del directorio, y en un checkout sobre NTFS devolvia el ledger de un entregable
# fuera de ambito (docs/decisions/ADR-0008-ambito-del-loop.md#d-0071): el veneno caia en
# un archivo que nadie lee y el gate pasaba con el veneno puesto.
ledger_en_ambito() {
    local fase slug
    fase="$(grep -m1 -E '^Fase:' STATE.md | grep -oE '[0-9]+' | head -1)"
    slug="$(awk '/<!-- BEGIN:loop-deliverables -->/{d=1; next}
                 /<!-- END:loop-deliverables -->/{d=0}
                 d && /^\| / && !/^\| *slug/ && !/^\|[-| ]*\|$/ {gsub(/ /,"",$2); print $2; exit}' \
        FS='|' STATE.md)"
    [[ -n "$fase" && -n "$slug" ]] || return 1
    local ledger=".loop/${fase}/${slug}.ledger.json"
    [[ -f "$ledger" ]] || return 1
    echo "$ledger"
}

docker_available() {
    (command -v docker >/dev/null 2>&1 && docker version >/dev/null 2>&1) ||
        (command -v docker.exe >/dev/null 2>&1 && docker.exe version >/dev/null 2>&1)
}

poison_0() { printf '\r\n' >>scripts/bench.sh; }

poison_0b() {
    # Un script sin el bit de ejecucion EN EL INDICE. En /mnt/c el archivo se ve 777, asi
    # que solo el indice lo delata: es como `scripts/zoo-game.sh` se commiteo sin bit.
    git update-index --chmod=-x scripts/bench.sh
}

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

poison_6e() {
    # Enlace a un anchor del MISMO archivo que no existe: el caso que se colaba antes
    # de que el lint mirara los enlaces sin nombre de fichero.
    printf '
Ver [preguntas abiertas](#no-existe-este-anchor).
' >>docs/glossary.md
}

poison_5b() {
    # Un generador prohibido de la STL dentro de engine/, en un comentario. El check 5
    # mira el arbol entero de engine/ y arena/ sin excepcion para comentarios: un
    # comentario esta a un `sed -i` de ser codigo. ver docs/invariants.md#inv-08
    printf '\n// ejemplo: std::shuffle sobre el tablero\n' >>engine/src/rules.cpp
}

poison_6f() {
    # El mismo hecho, con las mismas palabras, en dos archivos. Un hecho, un lugar:
    # ver docs/INDEX.md#i-04.
    python3 - <<'EOF'
import re
from pathlib import Path

prosa = [
    l.strip()
    for l in Path("docs/rules.md").read_text(encoding="utf-8").splitlines()
    if len(l.split()) >= 14 and not l.lstrip().startswith(("|", "#", "-", "*", "`", ">"))
]
if not prosa:
    raise SystemExit("no hay prosa larga que duplicar en docs/rules.md")
with Path("docs/glossary.md").open("a", encoding="utf-8") as fh:
    fh.write("\n" + prosa[0] + "\n")
EOF
}

poison_6g() {
    # Un valor de front-matter con dos puntos sin entrecomillar: no es YAML valido, y un
    # grep por clave lo deja pasar. Es el defecto que tenian diez documentos.
    sed -i '0,/^title: /s/^title: .*/title: Roto: esto no parsea como YAML/' docs/glossary.md
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
    ledger="$(ledger_en_ambito)" || return 1
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
    ledger="$(ledger_en_ambito)" || return 1
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

poison_9c() {
    # Ultima iteracion de una clase con una metrica fuera de umbral. Es la regla que
    # ADR-0007 deja en pie: las intermedias guardan lo que encontraron, la ultima de cada
    # clase tiene que cumplir. ver docs/decisions/ADR-0007-umbrales-por-clase.md#d-0061
    local ledger
    ledger="$(ledger_en_ambito)" || return 1
    python3 - "$ledger" <<'EOF'
import json, sys

path = sys.argv[1]
with open(path, encoding="utf-8") as fh:
    doc = json.load(fh)

vivas = [it for it in doc["iterations"] if not it.get("annulled")]
ultima_por_clase = {}
for it in vivas:
    ultima_por_clase[it["class"]] = it
objetivo = ultima_por_clase.get("perf") or ultima_por_clase.get("context") or vivas[-1]
objetivo.setdefault("metrics", {})["p99_move_ms"] = 999999
objetivo["metrics"]["duplicated_facts"] = 999

with open(path, "w", encoding="utf-8", newline="\n") as fh:
    json.dump(doc, fh, indent=2)
    fh.write("\n")
EOF
}

poison_9d() {
    # Un entregable declarado fuera del ambito del loop. La regla vive en
    # config/loop.json y el check 9 tiene que hacerla cumplir.
    # ver docs/decisions/ADR-0008-ambito-del-loop.md#d-0071
    python3 - <<'EOF'
from pathlib import Path

p = Path("STATE.md")
s = p.read_text(encoding="utf-8")
marca = "<!-- END:loop-deliverables -->"
fila = "| harness | scripts/gate.sh | correctness |\n"
p.write_text(s.replace(marca, fila + marca, 1), encoding="utf-8")
EOF
}

poison_8b() {
    # El servidor devuelve 5xx ante un payload que no entiende. INV-12 dice que /move
    # nunca lo hace: un 5xx hace que el arbitro aplique su movimiento por defecto.
    # Hasta la fase 2 el check 8 solo mandaba fixtures validos y no lo habria visto.
    python3 - <<'EOF'
import pathlib
p = pathlib.Path("snake/src/server.cpp")
s = p.read_text(encoding="utf-8")
viejo = 'std::cerr << "WARN=payload_no_soportado\\n";'
nuevo = 'res.status = 500;\n                std::cerr << "WARN=payload_no_soportado\\n";'
assert viejo in s, "el veneno 8b ya no encaja con server.cpp"
p.write_text(s.replace(viejo, nuevo, 1), encoding="utf-8")
EOF
}

poison_9e() {
    # Ledger sin bloque de auditorias. El criterio 14 las hace obligatorias y hasta
    # ADR-0013 el check 9 ni las miraba.
    # ver docs/decisions/ADR-0013-auditorias-fuera-del-loop.md#d-0121
    local ledger
    ledger="$(ledger_en_ambito)" || return 1
    python3 - "$ledger" <<'EOF'
import json, sys
path = sys.argv[1]
with open(path, encoding="utf-8") as fh:
    doc = json.load(fh)
doc.pop("auditorias", None)
with open(path, "w", encoding="utf-8", newline="\n") as fh:
    json.dump(doc, fh, indent=2)
    fh.write("\n")
EOF
}

poison_9f() {
    # Hallazgo de auditoria que se queda abierto. Registrar la auditoria y no cerrar lo
    # que encontro es peor que no auditar: parece hecho.
    local ledger
    ledger="$(ledger_en_ambito)" || return 1
    python3 - "$ledger" <<'EOF'
import json, sys
path = sys.argv[1]
with open(path, encoding="utf-8") as fh:
    doc = json.load(fh)
for auditoria in doc.get("auditorias", []):
    for hallazgo in auditoria.get("findings", []):
        hallazgo["estado"] = "ABIERTO"
        hallazgo.pop("fixed_in", None)
        break
    break
with open(path, "w", encoding="utf-8", newline="\n") as fh:
    json.dump(doc, fh, indent=2)
    fh.write("\n")
EOF
}

poison_9g() {
    # Hallazgo de auditoria que dice arreglarse en un commit que no toca el archivo que
    # el propio hallazgo cita. Es el antifraude de las iteraciones, aplicado al bloque
    # nuevo; se estreno cazando una traza falsa de verdad.
    local ledger
    ledger="$(ledger_en_ambito)" || return 1
    python3 - "$ledger" <<'EOF'
import json, sys
path = sys.argv[1]
with open(path, encoding="utf-8") as fh:
    doc = json.load(fh)
for auditoria in doc.get("auditorias", []):
    for hallazgo in auditoria.get("findings", []):
        if hallazgo.get("estado") == "REPARADO":
            hallazgo["cite"] = "deploy/Dockerfile"
            with open(path, "w", encoding="utf-8", newline="\n") as out:
                json.dump(doc, out, indent=2)
                out.write("\n")
            sys.exit(0)
sys.exit(1)
EOF
}

poison_9h() {
    # Un commit que existe como objeto pero no es alcanzable desde HEAD. Es lo que deja
    # un `git commit --amend`: en la maquina donde se reescribio el sha viejo todavia
    # resuelve, y en cualquier clon no existe. Paso de verdad en la fase 2.
    local ledger
    ledger="$(ledger_en_ambito)" || return 1
    python3 - "$ledger" <<'EOF'
import json, subprocess, sys
path = sys.argv[1]
with open(path, encoding="utf-8") as fh:
    doc = json.load(fh)
# Un commit real que existe en la historia pero se deja fuera de HEAD: se fabrica uno
# huerfano sobre el arbol actual, que resuelve con cat-file y no es ancestro de HEAD.
huerfano = subprocess.run(
    ["git", "commit-tree", "HEAD^{tree}", "-m", "huerfano del veneno"],
    capture_output=True, text=True, check=True).stdout.strip()
destino = doc.get("auditorias") or doc["iterations"]
destino[0]["commit_auditado" if "auditorias" in doc else "commit_before"] = huerfano
with open(path, "w", encoding="utf-8", newline="\n") as fh:
    json.dump(doc, fh, indent=2)
    fh.write("\n")
EOF
}

poison_10() {
    sed -i 's|^FROM gcr.io/distroless/cc-debian12:nonroot|FROM gcr.io/distroless/static-debian12:nonroot|' \
        deploy/Dockerfile
}

poison_11() {
    # Se le quita el --read-only al arranque de los contenedores del zoo. El check no
    # puede cazarlo con un grep del texto: lo caza porque compara el comando efectivo que
    # `zoo.sh up --dry-run` construye. ver zoo/README.md
    sed -i 's|^        --read-only \\$|        \\|' scripts/zoo.sh
}

poison_11b() {
    # Un manifest apunta a una rama en vez de a un commit: se estaria construyendo lo que
    # haya hoy en un repositorio ajeno, no lo que se aprobo.
    local m
    m="$(ls zoo/manifests/*.toml | head -1)"
    sed -i 's|^sha = .*|sha = "master"|' "$m"
}

poison_11c() {
    # Se publica el puerto en todas las interfaces, exponiendo codigo de terceros a la red.
    sed -i 's|-p "127.0.0.1:${puerto}:${puerto_snake}"|-p "${puerto}:${puerto_snake}"|' \
        scripts/zoo.sh
}

poison_11d() {
    # La imagen se etiqueta por snake en vez de por repositorio y commit: trece etiquetas
    # para una sola imagen, y el gauntlet congelaria trece digests del mismo binario.
    sed -i "s|printf 'zoo/%s:%s.n' \"\$repo\" \"\${sha:0:12}\"|printf 'zoo/%s:%s\\\\n' \"\$1\" \"\${sha:0:12}\"|" \
        scripts/zoo.sh
}

# veneno | check esperado | descripcion | [mensaje exacto que debe aparecer]
#
# El cuarto campo es opcional y existe para los venenos del check 9: ese check puede
# fallar por varias razones a la vez, asi que exigir el MENSAJE concreto es lo unico que
# prueba que el veneno se detecto y no que fallo otra cosa.
POISONS=(
    "poison_0|0|un .sh con CR"
    "poison_0b|0|un script sin bit de ejecucion en el indice de git|en el indice como 100644"
    "poison_1|1|error de sintaxis en el motor"
    "poison_2|2|codigo que solo rompe el build debug"
    "poison_3|3|un test que falla"
    "poison_4|4|codigo sin formatear"
    "poison_5|5|aviso de clang-tidy"
    "poison_5b|5|generador prohibido de la STL en un comentario de engine/"
    "poison_6|6|doc sin front-matter"
    "poison_6b|6|cita a un anchor inexistente desde STATE.md"
    "poison_6c|6|size_bytes que no coincide con wc -c"
    "poison_6d|6|doc canonical sin last_verified"
    "poison_6e|6|enlace a un anchor inexistente del mismo archivo"
    "poison_6f|6|el mismo hecho escrito en dos documentos"
    "poison_6g|6|front-matter con un valor que no es YAML valido|no es YAML valido"
    "poison_7|7|-march=native en un preset del que deploy hereda"
    "poison_8|8|el servidor devuelve un movimiento ilegal"
    "poison_8b|8|el servidor devuelve 5xx ante un payload que no entiende|HTTP 500"
    "poison_9|9|ledger con solo dos iteraciones|iteraciones validas (de 2), minimo 3"
    "poison_9b|9|ledger con el encadenamiento de commits roto|commit_after(i) != commit_before(i+1)"
    "poison_9c|9|ultima iteracion de una clase fuera de umbral|umbral incumplido"
    "poison_9d|9|entregable declarado fuera del ambito del loop|fuera de closing.deliverable_scope"
    "poison_9e|9|ledger sin bloque de auditorias|sin bloque auditorias"
    "poison_9f|9|hallazgo de auditoria abierto|hallazgos de auditoria abiertos"
    "poison_9g|9|arreglo de auditoria que no toca el archivo citado|que no toca ese archivo"
    "poison_9h|9|commit del ledger que existe pero no es alcanzable desde HEAD|no es alcanzable desde HEAD"
    "poison_10|10|runtime distroless sin libstdc++"
    "poison_11|11|contenedor del zoo sin --read-only|falta --read-only"
    "poison_11b|11|manifest que apunta a una rama en vez de a un commit|sha no es un commit completo"
    "poison_11c|11|puerto del zoo publicado en todas las interfaces|no se publica en 127.0.0.1"
    "poison_11d|11|imagen etiquetada por snake en vez de por repositorio|pero otra imagen"
)

passed=0
failed=0

for entry in "${POISONS[@]}"; do
    IFS='|' read -r fn check description expected <<<"$entry"

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
    elif ! grep -qE "^CHECK ${check} .* FAIL" <<<"$output"; then
        echo "FALLO check $check ($description): el gate fallo, pero no en el check $check"
        grep -E '^CHECK ' <<<"$output" | tail -5
        failed=$((failed + 1))
    elif [[ -n "${expected:-}" ]] && ! grep -qF "$expected" <<<"$output"; then
        echo "FALLO check $check ($description): el check fallo, pero sin el mensaje esperado"
        echo "  esperado: $expected"
        failed=$((failed + 1))
    else
        echo "OK    check $check ($description): el gate lo caza"
        passed=$((passed + 1))
    fi
    popd >/dev/null
    rm -rf "$work"
done

echo
echo "venenos cazados: $passed, fallidos: $failed"
[[ $failed -eq 0 ]]
