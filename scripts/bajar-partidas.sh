#!/usr/bin/env bash
# Descarga los frames de partidas JUGADAS DE VERDAD (leaderboard, torneo, arena publica)
# para poder analizarlas en bloque con training-room/liga.py.
#
#   ./scripts/bajar-partidas.sh partidas.txt [carpeta]
#
# `partidas.txt` lleva una URL o un id por linea; se ignoran lineas vacias y las que
# empiezan por '#'. Valen las dos formas:
#   https://play.battlesnake.com/game/16d5acb3-198c-4d83-8a17-c759f2c6f561
#   16d5acb3-198c-4d83-8a17-c759f2c6f561
#
# El motor sirve como mucho 100 frames por peticion, asi que de cada partida se piden
# paginas hasta que una vuelve vacia. Una partida de 400 turnos son 4 peticiones y ~1 MB.
# Verificado contra el motor en esta sesion, no supuesto: ver docs/SOURCES.md
set -uo pipefail

cd "$(dirname "$0")/.."

LISTA="${1:-}"
DESTINO="${2:-matches/liga}"
MOTOR="https://engine.battlesnake.com/games"

[[ -f "$LISTA" ]] || {
    echo "uso: $0 <archivo con urls o ids> [carpeta destino]" >&2
    exit 2
}
command -v curl >/dev/null || {
    echo "ERROR falta curl" >&2
    exit 2
}

bajadas=0
saltadas=0
while read -r linea; do
    linea="${linea%%#*}"
    linea="$(echo "$linea" | tr -d '[:space:]')"
    [[ -z "$linea" ]] && continue
    id="${linea##*/}"
    [[ ${#id} -eq 36 ]] || {
        echo "SALTO  no parece un id de partida: $linea" >&2
        saltadas=$((saltadas + 1))
        continue
    }
    dir="${DESTINO}/${id}"
    if [[ -s "${dir}/f0.json" ]]; then
        echo "YA     $id"
        continue
    fi
    mkdir -p "$dir"
    offset=0
    paginas=0
    while :; do
        salida="${dir}/f${offset}.json"
        curl -fsS "${MOTOR}/${id}/frames?offset=${offset}&limit=100" -o "$salida" || {
            echo "ERROR  no se pudo bajar $id offset $offset" >&2
            break
        }
        # Una pagina sin frames cierra la partida; se borra para no dejar basura.
        if ! grep -q '"Turn"' "$salida"; then
            rm -f "$salida"
            break
        fi
        paginas=$((paginas + 1))
        offset=$((offset + 100))
        [[ $offset -gt 5000 ]] && break
    done
    echo "OK     $id  ($paginas paginas)"
    bajadas=$((bajadas + 1))
done <"$LISTA"

echo
echo "partidas nuevas: $bajadas   saltadas: $saltadas   en: $DESTINO"
