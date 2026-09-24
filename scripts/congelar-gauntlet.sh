#!/usr/bin/env bash
# Rellena los digests de un gauntlet con los de ESTA maquina, y lo congela.
#
#   ./scripts/congelar-gauntlet.sh training-room/gauntlets/gauntlet-v2.json [--refijar]
#
# Por que hace falta: `tr.py` aborta si la imagen local no tiene el digest congelado en el
# gauntlet -asi un campo distinto no se cuela como si fuera el mismo-, y el id de una
# imagen depende de donde se construyo. Asi que el campo se congela en la maquina que va a
# correr el torneo, una vez, y a partir de ahi el fichero es el contrato.
#
# Sin `--refijar` se niega a tocar un gauntlet ya congelado: reescribir digests a mano es
# exactamente la forma de invalidar una comparacion sin enterarse.
set -uo pipefail

cd "$(dirname "$0")/.."

RUTA="${1:-}"
REFIJAR=0
[[ "${2:-}" == --refijar ]] && REFIJAR=1
[[ -f "$RUTA" ]] || {
    echo "uso: $0 <gauntlet.json> [--refijar]" >&2
    exit 2
}
command -v jq >/dev/null || {
    echo "ERROR falta jq" >&2
    exit 2
}

congelado="$(jq -r '.congelado // ""' "$RUTA")"
if [[ -n "$congelado" && $REFIJAR -eq 0 ]]; then
    echo "ERROR $RUTA ya esta congelado ($congelado). Un campo distinto es otro gauntlet."
    echo "      Si de verdad quieres reescribirlo, pasa --refijar y registra por que."
    exit 1
fi

slugs="$(jq -r '.rivales[].slug' "$RUTA")"
imagenes="{}"
for slug in $slugs; do
    img="$(./scripts/zoo.sh imagen "$slug")" || exit 1
    digest="$(./scripts/zoo.sh digest "$slug")" || {
        echo "ERROR falta la imagen de $slug; corre './scripts/zoo.sh build $slug'" >&2
        exit 1
    }
    # Varias snakes pueden compartir imagen (mismo repo, distinto cerebro): el mapa las
    # colapsa en una sola entrada, que es justo lo que comprueba el orquestador.
    imagenes="$(jq --arg i "$img" --arg d "${digest:0:19}" '. + {($i): $d}' <<<"$imagenes")"
    printf '%-18s %-40s %s\n' "$slug" "$img" "${digest:0:19}"
done

hoy="$(date -u +%Y-%m-%d)"
tmp="$(mktemp)"
jq --argjson im "$imagenes" --arg hoy "$hoy" '.imagenes = $im | .congelado = $hoy' "$RUTA" >"$tmp" &&
    mv "$tmp" "$RUTA"

echo
echo "congelado $hoy con $(jq '.imagenes | length' "$RUTA") imagenes"
echo "commitea $RUTA: a partir de ahora es el contrato del campo"
