#!/usr/bin/env bash
# Regenera el bloque perf-snapshot de STATE.md desde docs/performance.md.
#
# docs/performance.md es `authority: canonical` y UNICO dueño de todo numero medido.
# STATE.md no tiene numeros propios: su tabla se copia de ahi. Editarla a mano es un
# fallo y `--check` lo detecta en el gate. ver docs/performance.md#p-01
#
#   ./scripts/sync_state.sh           reescribe el bloque
#   ./scripts/sync_state.sh --check   falla si el bloque no coincide
set -uo pipefail

cd "$(dirname "$0")/.."

STATE=STATE.md
PERF=docs/performance.md
BEGIN='<!-- BEGIN:perf-snapshot -->'
END='<!-- END:perf-snapshot -->'
SRC_BEGIN='<!-- BEGIN:perf-canonical -->'
SRC_END='<!-- END:perf-canonical -->'

for file in "$STATE" "$PERF"; do
    if [[ ! -f "$file" ]]; then
        echo "FAIL falta $file"
        exit 1
    fi
done

extract_source() {
    awk -v b="$SRC_BEGIN" -v e="$SRC_END" '
        $0 == b { inside = 1; next }
        $0 == e { inside = 0 }
        inside { print }
    ' "$PERF"
}

rendered() {
    awk -v b="$BEGIN" -v e="$END" -v src="$(extract_source)" '
        $0 == b { print; print src; skip = 1; next }
        $0 == e { skip = 0 }
        !skip { print }
    ' "$STATE"
}

if ! grep -qF "$BEGIN" "$STATE" || ! grep -qF "$END" "$STATE"; then
    echo "FAIL $STATE no tiene los marcadores $BEGIN / $END"
    exit 1
fi
if ! grep -qF "$SRC_BEGIN" "$PERF" || ! grep -qF "$SRC_END" "$PERF"; then
    echo "FAIL $PERF no tiene los marcadores $SRC_BEGIN / $SRC_END"
    exit 1
fi

tmp="$(mktemp)"
trap 'rm -f "$tmp"' EXIT
rendered >"$tmp"

if [[ "${1:-}" == "--check" ]]; then
    if diff -u "$STATE" "$tmp" >/dev/null; then
        echo "OK   perf-snapshot de STATE.md sincronizado con $PERF"
        exit 0
    fi
    echo "FAIL el bloque perf-snapshot de STATE.md no coincide con $PERF"
    diff -u "$STATE" "$tmp" | head -30
    echo "corrige con: ./scripts/sync_state.sh"
    exit 1
fi

mv "$tmp" "$STATE"
trap - EXIT
echo "OK   perf-snapshot regenerado en $STATE desde $PERF"
