#!/usr/bin/env bash
# Metadatos de la capa de contexto.
#
#   --fix     reescribe size_bytes en cada doc hasta que el valor sea punto fijo
#   --check   falla si algun size_bytes commiteado no coincide con `wc -c`
#   --budget  falla si se pasan los presupuestos de bytes de docs/INDEX.md
#
# Convencion: tokens ~ bytes/4. Es una metrica barata y determinista, no un conteo
# real de tokens. ver docs/INDEX.md#i-02
set -uo pipefail

cd "$(dirname "$0")/.."

MODE="${1:---check}"

# Presupuestos (docs/INDEX.md#i-02). Subirlos exige aprobacion humana y un ADR.
STARTUP_BUDGET=12288
TASK_BUDGET=49152

docs_with_frontmatter() {
    find docs -name '*.md' -not -path 'docs/results/*' | sort
}

real_size() { wc -c <"$1" | tr -d ' '; }

declared_size() {
    awk '/^---$/{n++; next} n==1 && /^size_bytes:/{print $2; exit}' "$1"
}

set_size() {
    local file="$1" value="$2"
    awk -v v="$value" '
        /^---$/ { n++ }
        n==1 && /^size_bytes:/ { print "size_bytes: " v; next }
        { print }
    ' "$file" >"$file.tmp" && mv "$file.tmp" "$file"
}

fix() {
    local status=0
    while read -r file; do
        if [[ -z "$(declared_size "$file")" ]]; then
            echo "FAIL $file: sin campo size_bytes en el front-matter"
            status=1
            continue
        fi
        # Punto fijo: cambiar el numero puede cambiar el tamaño del archivo.
        for _ in 1 2 3 4 5; do
            local actual declared
            actual="$(real_size "$file")"
            declared="$(declared_size "$file")"
            [[ "$actual" == "$declared" ]] && break
            set_size "$file" "$actual"
        done
        echo "OK   $file $(real_size "$file")"
    done < <(docs_with_frontmatter)
    return $status
}

check() {
    local status=0
    while read -r file; do
        local actual declared
        actual="$(real_size "$file")"
        declared="$(declared_size "$file")"
        if [[ -z "$declared" ]]; then
            echo "FAIL $file: sin size_bytes"
            status=1
        elif [[ "$actual" != "$declared" ]]; then
            echo "FAIL $file: size_bytes=$declared pero wc -c=$actual (corrige con --fix)"
            status=1
        fi
    done < <(docs_with_frontmatter)
    [[ $status -eq 0 ]] && echo "size_bytes correcto en $(docs_with_frontmatter | wc -l) docs"
    return $status
}

sum_sizes() {
    local total=0
    for file in "$@"; do
        [[ -f "$file" ]] || continue
        total=$((total + $(real_size "$file")))
    done
    echo "$total"
}

budget() {
    local status=0

    local startup
    startup="$(sum_sizes CLAUDE.md STATE.md docs/INDEX.md)"
    if [[ "$startup" -gt "$STARTUP_BUDGET" ]]; then
        echo "FAIL arranque: $startup bytes > $STARTUP_BUDGET (CLAUDE.md + STATE.md + docs/INDEX.md)"
        status=1
    else
        echo "OK   arranque: $startup / $STARTUP_BUDGET bytes"
    fi

    for pack in docs/context-packs/*.md; do
        [[ -f "$pack" ]] || continue
        # Solo cuentan los archivos que el pack manda CARGAR, es decir los que lista entre
        # los marcadores pack-load. Un pack sin marcadores falla: quitarlos no es una via
        # de escape del presupuesto.
        if ! grep -qF '<!-- BEGIN:pack-load -->' "$pack"; then
            echo "FAIL $pack: sin bloque pack-load; no se puede medir su presupuesto"
            status=1
            continue
        fi
        local files
        files="$(awk '/<!-- BEGIN:pack-load -->/{inside=1; next}
                      /<!-- END:pack-load -->/{inside=0}
                      inside' "$pack" |
                 grep -oE '`[A-Za-z0-9_./-]+\.(md|hpp|cpp|json|sh)`' |
                 tr -d '`' | sort -u)"
        local total
        # shellcheck disable=SC2086
        total=$((startup + $(real_size "$pack") + $(sum_sizes $files)))
        if [[ "$total" -gt "$TASK_BUDGET" ]]; then
            echo "FAIL $pack: $total bytes > $TASK_BUDGET"
            status=1
        else
            echo "OK   $pack: $total / $TASK_BUDGET bytes"
        fi
    done
    return $status
}

case "$MODE" in
    --fix) fix ;;
    --check) check ;;
    --budget) budget ;;
    *)
        echo "uso: $0 [--fix|--check|--budget]"
        exit 2
        ;;
esac
