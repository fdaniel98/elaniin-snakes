---
title: "Pack: añadir o cambiar una heuristica"
read_when: "antes de tocar la evaluacion del cerebro o de lanzar un A/B"
authority: derived
last_verified: 2026-09-15
size_bytes: 2335
---


## CP-20 Que cargar, en orden {#cp-20}

<!-- BEGIN:pack-load -->
| Orden | Archivo | Para que |
|---|---|---|
| 1 | `docs/strategy.md` | la hipotesis falsable de la version que tocas |
| 1b | `docs/experimentos.md` | **lee esto antes de proponer nada**: que se probo ya y que dio |
| 2 | `snake/include/snake/params.hpp` | los pesos existentes **y `evaluate()` documentada en sus doc-comments**; no inventes constantes |
| 3 | `snake/config/default.json` | el JSON 1:1 con el struct |
<!-- END:pack-load -->

Fuera de la lista a proposito, y por que:

- `snake/src/search.cpp` (creciendo con cada version): lo que se lee para añadir una
  heuristica es **`evaluate()`**, no el motor de busqueda que ocupa el resto del fichero.
  Abrelo por esa funcion. Cada parametro nuevo se declara en `params.hpp` con el doc-comment
  que explica por que existe y que numero lo justifica, asi que ese fichero es el indice.

- `snake/src/brain_v0.cpp` (20 KB): v0 esta CONGELADO y es la referencia fija. Una
  heuristica nueva no se toca ahi, se toca en `evaluate()`. Abrelo solo si necesitas ver
  como puntuaba el baseline.
- `docs/rules.md` (14 KB): no se lee entero para añadir una heuristica, se abre por el
  anchor de la mecanica concreta que se va a explotar.

## CP-21 Como se hace {#cp-21}

1. Escribe la hipotesis en `docs/strategy.md` **antes** de tocar codigo: que deberia
   mejorar y como se falsaria.
2. Todo peso nuevo va a `Params` y a `default.json` a la vez, o el test de 1:1 falla.
3. v0 no se modifica: es la referencia fija. Una heuristica nueva es una version nueva.
4. El veredicto lo da el A/B, no la intuicion (ver docs/strategy.md#s-ab).
5. Comprueba en ver docs/experimentos.md#exp-resumen que tu idea no se ha medido ya.
   Dos de las cuatro que hay ahi se propusieron dos veces.

## CP-22 Criterio de salida {#cp-22}

- Fixture nuevo que capture la situacion que la heuristica pretende resolver.
- A/B con veredicto `MEJORA` contra el campo congelado, con bloques consumidos y
  `--max-games` escritos en el reporte.
- Clases del loop: `correctness`, `robustness` y `perf` (una heuristica cara puede
  romper el deadline).
