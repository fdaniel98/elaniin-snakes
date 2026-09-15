---
title: "Pack: añadir o cambiar una heuristica"
read_when: "antes de tocar la evaluacion del cerebro o de lanzar un A/B"
authority: derived
last_verified: 2026-09-15
size_bytes: 1437
---


## CP-20 Que cargar, en orden {#cp-20}

<!-- BEGIN:pack-load -->
| Orden | Archivo | Para que |
|---|---|---|
| 1 | `docs/strategy.md` | la hipotesis falsable de la version que tocas |
| 2 | `snake/include/snake/params.hpp` | los pesos existentes; no inventes constantes |
| 3 | `snake/config/default.json` | el JSON 1:1 con el struct |
| 4 | `snake/src/brain_v0.cpp` | como se puntua hoy un candidato |
| 5 | `docs/rules.md` | solo el anchor de la mecanica que vas a explotar |
<!-- END:pack-load -->

## CP-21 Como se hace {#cp-21}

1. Escribe la hipotesis en `docs/strategy.md` **antes** de tocar codigo: que deberia
   mejorar y como se falsaria.
2. Todo peso nuevo va a `Params` y a `default.json` a la vez, o el test de 1:1 falla.
3. v0 no se modifica: es la referencia fija. Una heuristica nueva es una version nueva.
4. El veredicto lo da el A/B, no la intuicion (ver docs/strategy.md#s-ab).

## CP-22 Criterio de salida {#cp-22}

- Fixture nuevo que capture la situacion que la heuristica pretende resolver.
- A/B con veredicto `MEJORA` contra el campo congelado, con bloques consumidos y
  `--max-games` escritos en el reporte.
- Clases del loop: `correctness`, `robustness` y `perf` (una heuristica cara puede
  romper el deadline).
