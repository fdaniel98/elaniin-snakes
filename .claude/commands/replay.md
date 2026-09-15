---
description: Reproduce una partida guardada e invoca al analista de partidas
argument-hint: "<archivo.jsonl> [turno]"
allowed-tools: Bash, Read, Task
---

Archivo: `$1`
Turno: `$2`

!`./scripts/replay.sh $1 $2`

Despues:

1. Invoca al subagente `match-analyst` sobre esa partida.
2. Exige su tabla `turno | estado | movimiento elegido | mejor alternativa | categoria`.
3. Convierte el turno del error decisivo en un fixture nuevo en `tests/fixtures/`, con
   `_comment` explicando el error y `_expect_not` con el movimiento que fallo.
