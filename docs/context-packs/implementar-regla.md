---
title: "Pack: implementar o corregir una regla"
read_when: "antes de tocar engine/src/rules.cpp o engine/src/royale_map.cpp, o de añadir un fixture de reglas"
authority: derived
last_verified: 2026-09-17
size_bytes: 1975
---


## CP-01 Que cargar, en orden {#cp-01}

<!-- BEGIN:pack-load -->
| Orden | Archivo | Para que |
|---|---|---|
| 1 | `docs/rules.md` | la regla y su cita `archivo.go:linea`; es la unica fuente |
| 2 | `docs/invariants.md` | que no puedes romper al tocar el motor |
| 3 | `engine/src/rules.cpp` | donde vive el orden de fases del turno |
<!-- END:pack-load -->

No abras `snake/` para esto: una regla del juego no depende del cerebro.
Tres archivos se abren mas tarde, no al arrancar, porque no caben en el presupuesto de
la tarea (ver docs/INDEX.md#i-02): `engine/src/royale_map.cpp`, si y solo si la regla es
del mapa -hazards y shrink-, y en vez de `rules.cpp`, no ademas; la representacion del cuerpo, en
engine/include/engine/state.hpp, cuando necesites tocar el ring buffer; y el archivo de
tests de reglas, cuando vayas a escribir el caso.

## CP-02 Como se hace {#cp-02}

1. Si la regla no esta en `docs/rules.md` con cita, **para**: primero se verifica contra
   el Go del SHA fijado y se documenta. Implementar contra `speculative` esta prohibido.
2. Escribe el test antes que el codigo, con la etiqueta del anchor, por ejemplo
   `[rules][r-08]`.
3. Toca `engine/src/rules.cpp` respetando el orden de fases (ver docs/rules.md#r-02).
4. Si añades estado al `GameState`, comprueba que sigue siendo trivialmente copiable
   (ver docs/invariants.md#inv-04).

## CP-03 Criterio de salida {#cp-03}

- `./scripts/gate.sh` completo en verde.
- La regla nueva citada en `docs/rules.md` con `archivo.go:linea` y el SHA.
- Clases del loop obligatorias para este tipo de tarea: `correctness` (i1),
  `robustness` (i2) y `perf` (i3). Añade `context` si tocaste `docs/`.
- El ledger `.loop/<fase>/<slug>.ledger.json` cierra con `status: CLOSED`.
