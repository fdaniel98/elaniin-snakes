#!/usr/bin/env bash
# [FASE 3] Reproduce una partida guardada en JSONL y deja el estado turno a turno para
# que el subagente match-analyst localice el turno del error decisivo.
#
#   ./scripts/replay.sh <archivo.jsonl> [turno]
#
# El JSONL del arbitro oficial trae: 1 linea de game, N lineas de SnakeRequest y 1 linea
# de resultado con winnerId/isDraw. No incluye placements ni el turno de eliminacion:
# el orden final se deriva del turno en que cada serpiente desaparece.
# ver docs/rules.md#r-12
set -uo pipefail

FILE="${1:-}"
TURN="${2:-}"

if [[ -z "$FILE" ]]; then
    echo "uso: $0 <archivo.jsonl> [turno]"
    exit 2
fi
if [[ ! -f "$FILE" ]]; then
    echo "no existe $FILE"
    exit 2
fi

echo "== resumen de $FILE =="
head -1 "$FILE" | jq -r '"game=\(.id) ruleset=\(.ruleset.name) map=\(.map) timeout=\(.timeout)"'
tail -1 "$FILE" | jq -r '"resultado: winner=\(.winnerName // "-") draw=\(.isDraw)"'
echo "turnos registrados: $(($(wc -l < "$FILE") - 2))"

if [[ -n "$TURN" ]]; then
    echo
    echo "== turno $TURN =="
    sed -n "$((TURN + 2))p" "$FILE" | jq '{turn, snakes: [.board.snakes[] | {name, health, length, head}]}'
fi

echo
echo "el analisis del error decisivo lo hace el subagente match-analyst: /replay $FILE $TURN"
echo "no implementado: fase 3 (reproduccion turno a turno contra el motor propio)"
exit 3
