#!/usr/bin/env bash
# Check 6 del gate: lint de la capa de contexto.
#
#   1. front-matter valido en docs/** (excluyendo docs/results/**)
#   2. ningun doc `canonical` sin `last_verified`
#   3. size_bytes exacto y presupuestos de bytes (scripts/docs_meta.sh)
#   4. anchors bidireccionales sobre **/*.{cpp,hpp} y TODOS los .md del repo,
#      incluidos CLAUDE.md, */CLAUDE.md y STATE.md
#   5. hechos duplicados entre documentos (scripts/lint_dupes.py), con el umbral
#      duplicated_facts_max de config/loop.json
#   6. sync_state.sh --check
#
# Sintaxis fijada (docs/INDEX.md#i-03):
#   definicion: ^#{2,4} .+ \{#([a-z0-9-]+)\}$
#   cita:       (?:ver|see) (docs/[\w/.-]+\.md)#([a-z0-9-]+)
#   enlace:     [texto](ruta.md#anchor) o [texto](#anchor) en el mismo archivo
set -uo pipefail

cd "$(dirname "$0")/.."

status=0
fail() {
    echo "FAIL $*"
    status=1
}

REQUIRED_KEYS=(title read_when authority last_verified size_bytes)

echo "== front-matter =="
while read -r file; do
    if [[ "$(head -1 "$file")" != "---" ]]; then
        fail "$file: no empieza con front-matter"
        continue
    fi
    block="$(awk 'NR>1 && /^---$/{exit} NR>1{print}' "$file")"
    for key in "${REQUIRED_KEYS[@]}"; do
        if ! grep -qE "^${key}:" <<<"$block"; then
            fail "$file: falta '${key}' en el front-matter"
        fi
    done
    authority="$(grep -E '^authority:' <<<"$block" | head -1 | awk '{print $2}')"
    case "$authority" in
        canonical | derived | speculative) ;;
        *) fail "$file: authority invalido ('${authority}')" ;;
    esac
    if [[ "$authority" == "canonical" ]]; then
        verified="$(grep -E '^last_verified:' <<<"$block" | head -1 | awk '{print $2}')"
        if ! grep -qE '^[0-9]{4}-[0-9]{2}-[0-9]{2}$' <<<"$verified"; then
            fail "$file: canonical sin last_verified con fecha valida"
        fi
    fi
done < <(find docs -name '*.md' -not -path 'docs/results/*' | sort)
[[ $status -eq 0 ]] && echo "OK   front-matter valido"

echo "== size_bytes y presupuestos =="
./scripts/docs_meta.sh --check || status=1
./scripts/docs_meta.sh --budget || status=1

echo "== anchors =="
# Todos los anchors definidos, con su archivo.
anchors_file="$(mktemp)"
citations_file="$(mktemp)"
trap 'rm -f "$anchors_file" "$citations_file"' EXIT

while read -r file; do
    grep -nE '^#{2,4} .+ \{#[a-z0-9-]+\}$' "$file" |
        sed -E 's/^([0-9]+):.*\{#([a-z0-9-]+)\}$/\2/' |
        while read -r anchor; do
            echo "${file}#${anchor}"
        done
done < <(find docs -name '*.md' -not -path 'docs/results/*' | sort) >"$anchors_file"

# Citas en codigo y en markdown, incluidos CLAUDE.md, */CLAUDE.md y STATE.md.
while read -r file; do
    grep -oE '(ver|see) docs/[A-Za-z0-9_./-]+\.md#[a-z0-9-]+' "$file" 2>/dev/null |
        sed -E 's/^(ver|see) //' |
        while read -r citation; do
            echo "${file}|${citation}"
        done
    # Enlaces al mismo archivo, del tipo [texto](#anchor): antes pasaban invisibles
    # porque la regex exigia un nombre de fichero.
    grep -oE '\]\(#[a-z0-9-]+\)' "$file" 2>/dev/null |
        sed -E 's/^\]\(//; s/\)$//' |
        while read -r anchor; do
            echo "${file}|${file}${anchor}" | sed 's#|\./#|#'
        done
    grep -oE '\]\(([A-Za-z0-9_./-]*\.md)#[a-z0-9-]+\)' "$file" 2>/dev/null |
        sed -E 's/^\]\(//; s/\)$//' |
        while read -r citation; do
            # Enlaces relativos dentro de docs/: se normalizan a ruta desde la raiz.
            case "$citation" in
                docs/*) echo "${file}|${citation}" ;;
                # Enlace relativo: se normaliza a ruta desde la raiz y se le quita el
                # './' que mete find, o no casaria con la lista de anchors.
                *) echo "${file}|$(dirname "$file")/${citation}" | sed 's#|\./#|#' ;;
            esac
        done
done < <({
    find engine snake tests bench arena training-room zoo -name '*.cpp' -o -name '*.hpp' 2>/dev/null
    find . -name '*.md' -not -path './build/*' -not -path './.git/*' -not -path './docs/results/*' \
        -not -path './third_party/*'
} | sort -u) >"$citations_file"

missing=0
while IFS='|' read -r source citation; do
    [[ -z "$citation" ]] && continue
    if ! grep -qxF "$citation" "$anchors_file"; then
        fail "$source cita un anchor inexistente: $citation"
        missing=$((missing + 1))
    fi
done <"$citations_file"
[[ $missing -eq 0 ]] && echo "OK   $(wc -l <"$citations_file") citas resueltas"

# Anchors huerfanos: se listan, no rompen el gate (un anchor puede existir para que lo
# cite una fase futura), salvo que este marcado como obligatorio en docs/INDEX.md.
orphans=0
while read -r anchor; do
    if ! cut -d'|' -f2 "$citations_file" | grep -qxF "$anchor"; then
        echo "WARN anchor huerfano (nadie lo cita): $anchor"
        orphans=$((orphans + 1))
    fi
done <"$anchors_file"
echo "INFO $orphans anchors huerfanos de $(wc -l <"$anchors_file")"

echo "== duplicados ==" # un hecho, un lugar (docs/INDEX.md#i-04)
dupes_max="$(jq -r '.classes.context.thresholds.duplicated_facts_max' config/loop.json)"
# Se invoca por el interprete, no por el bit de ejecucion: un checkout desde Windows
# no lo conserva y el check moriria con "permission denied" en vez de medir.
python3 scripts/lint_dupes.py --max "$dupes_max" || fail "hechos duplicados por encima del umbral"

echo "== STATE.md =="
./scripts/sync_state.sh --check || status=1

exit $status
