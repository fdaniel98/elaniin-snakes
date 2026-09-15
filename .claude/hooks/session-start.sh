#!/usr/bin/env bash
# SessionStart: su stdout SI se inyecta al contexto. Imprime la cabecera de STATE.md y el
# orden de lectura, que es justo lo que una sesion nueva necesita para no explorar a
# ciegas. ver docs/harness.md#h-03
set -uo pipefail

cd "${CLAUDE_PROJECT_DIR:-.}" || exit 0

echo "== estado del proyecto (STATE.md) =="
sed -n '1,12p' STATE.md 2>/dev/null

echo
echo "== siguiente accion concreta =="
awk '/^## Siguiente accion concreta/{flag=1; next} flag && NF {print; exit}' STATE.md 2>/dev/null

echo
echo "== orden de lectura =="
echo "1. STATE.md  2. docs/INDEX.md  3. el context pack de la tarea (docs/context-packs/)"
echo "El gate es el arbitro: ./scripts/gate.sh . No se cierra fase sin loop cerrado."
exit 0
