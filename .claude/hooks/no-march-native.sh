#!/usr/bin/env bash
# PreToolUse sobre Edit|Write|MultiEdit: deniega meter -march=native en CMakePresets.json
# o bajo deploy/. Es CONVENIENCIA; la garantia es el check 7 del gate, que mira los flags
# efectivos. No se pone sobre Bash: el flag entra por edicion de archivos, y un matcher
# sobre Bash bloquearia el propio grep del check 7.
# ver docs/harness.md#h-03
set -uo pipefail

payload="$(cat)"
path="$(jq -r '.tool_input.file_path // empty' <<<"$payload")"
content="$(jq -r '(.tool_input.content // "") + (.tool_input.new_string // "")' <<<"$payload")"

case "$path" in
    *CMakePresets.json | */deploy/* | deploy/*) ;;
    *) exit 0 ;;
esac

if grep -qE -- '-m(arch|tune|cpu)=native' <<<"$content"; then
    echo "denegado: -march=native esta prohibido en $path (ver docs/decisions/ADR-0004-deploy.md)" >&2
    exit 2
fi
exit 0
