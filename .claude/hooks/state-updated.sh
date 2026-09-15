#!/usr/bin/env bash
# Stop: si hubo commits en la sesion y STATE.md no esta entre los archivos tocados,
# bloquea y pide /sync-state. Un hook Stop que sale 0 escribe en un stdout que el modelo
# no lee: como recordatorio informativo no existiria.
# ver docs/harness.md#h-03
set -uo pipefail

payload="$(cat)"
if [[ "$(jq -r '.stop_hook_active // false' <<<"$payload")" == "true" ]]; then
    exit 0
fi

cd "${CLAUDE_PROJECT_DIR:-.}" || exit 0

# Commits de las ultimas 8 horas: aproximacion barata a "esta sesion".
commits="$(git log --since='8 hours ago' --oneline 2>/dev/null | wc -l | tr -d ' ')"
[[ "$commits" -eq 0 ]] && exit 0

touched="$(git log --since='8 hours ago' --name-only --format= 2>/dev/null | sort -u)"
if grep -qx 'STATE.md' <<<"$touched"; then
    exit 0
fi

printf '{"decision":"block","reason":"%s commits sin actualizar STATE.md; ejecuta /sync-state"}\n' \
    "$commits"
exit 0
