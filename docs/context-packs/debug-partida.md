---
title: Pack: investigar una derrota
read_when: "al analizar un replay JSONL de una partida perdida"
authority: derived
last_verified: 2026-09-15
size_bytes: 1310
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

1. `./scripts/replay.sh <archivo.jsonl>` para el resumen, y con `<turno>` para el detalle.
2. Reconstruye las posiciones finales segun la regla canonica
   (ver docs/rules.md#r-12); el log por si solo no las da.
3. Invoca `match-analyst` para clasificar la causa: cabezazo, cuerpo propio, cuerpo rival,
   pared, hambre o hazard.
4. Convierte el turno del error decisivo en un **fixture nuevo** con su movimiento
   esperado. Una derrota sin fixture se repite.

## CP-32 Criterio de salida {#cp-32}

- Fixture nuevo en `tests/fixtures/` con `_comment` que explique el error.
- Causa clasificada y, si es un bug, hallazgo abierto en `STATE.md`.
- Clases del loop: la del defecto encontrado (normalmente `correctness` o `robustness`).
