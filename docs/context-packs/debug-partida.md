---
title: "Pack: investigar una derrota"
read_when: "al analizar un replay JSONL de una partida perdida"
authority: derived
last_verified: 2026-09-15
size_bytes: 1747
---


## CP-30 Que cargar, en orden {#cp-30}

<!-- BEGIN:pack-load -->
| Orden | Archivo | Para que |
|---|---|---|
| 1 | el JSONL de la partida | los hechos |
| 2 | `docs/rules.md` | por que el motor hizo lo que hizo |
| 3 | `snake/src/brain_v0.cpp` | por que elegimos lo que elegimos |
| 4 | `tests/fixtures/` | el fixture mas parecido, para clonarlo |
<!-- END:pack-load -->

## CP-31 Como se hace {#cp-31}

0. Si la partida es REAL (leaderboard o torneo) y no una del arbitro local, baja sus
   frames con `./scripts/bajar-partidas.sh` y mirala con `training-room/partida.py`; para
   muchas partidas a la vez, `training-room/liga.py` da el patron por rival y por causa.
   Con `--servidor` se le pregunta al cerebro de ESTE arbol que habria jugado en cada
   turno, que es lo que convierte un replay en una lista de posiciones donde diferimos.
1. Invoca `match-analyst`. El procedimiento completo -que comando corre, como localiza el
   turno decisivo, con que categorias clasifica y que formato devuelve- vive en su
   contrato, `.claude/agents/match-analyst.md`, y no se copia aqui.
2. Para las posiciones finales, ver docs/rules.md#r-12: el log por si solo no las da.
3. Convierte el turno del error decisivo en un **fixture nuevo** con su movimiento
   esperado. Una derrota sin fixture se repite.

## CP-32 Criterio de salida {#cp-32}

- Fixture nuevo en `tests/fixtures/` con `_comment` que explique el error.
- Causa clasificada y, si es un bug, hallazgo abierto en `STATE.md`.
- Clases del loop: la del defecto encontrado (normalmente `correctness` o `robustness`).
