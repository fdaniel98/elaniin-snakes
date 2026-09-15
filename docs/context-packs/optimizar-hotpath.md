---
title: Pack: optimizar el hot path
read_when: "antes de optimizar engine/ o snake/ guiado por perfil"
authority: derived
last_verified: 2026-09-15
size_bytes: 1508
---


## CP-10 Que cargar, en orden {#cp-10}

<!-- BEGIN:pack-load -->
| Orden | Archivo | Para que |
|---|---|---|
| 1 | `docs/performance.md` | la linea base y como se mide lo publicable |
| 2 | `docs/invariants.md` | cero asignaciones y estado trivialmente copiable |
| 3 | `bench/bench_engine.cpp` | que se mide hoy |
| 4 | `engine/include/engine/bitboard.hpp` | la primitiva que domina el coste |
| 5 | el archivo que el perfil señale | nada mas |
<!-- END:pack-load -->

## CP-11 Como se hace {#cp-11}

1. **Mide antes.** Sin numero previo no hay optimizacion, hay opinion.
2. `./scripts/bench.sh` (preset `bench-deployisa`). Los numeros con `-march=native` son
   `local-only` y no sirven como linea base (ver docs/performance.md#p-02).
3. Cambia una cosa. Vuelve a medir. Reporta delta absoluto y relativo.
4. Si el cambio no mejora por encima de `regression_pct` de `config/loop.json`, revierte:
   el veredicto es `RUIDO`.

## CP-12 Criterio de salida {#cp-12}

- Tabla antes/despues en `docs/performance.md`, con commit y fecha.
- p99 de `POST /move` dentro del presupuesto (ver docs/performance.md#p-01).
- Clases del loop: `perf` (obligatoria, con el veredicto del subagente `perf-analyst`),
  mas `correctness` y `robustness` porque el hot path es codigo de reglas.
- `context` obligatoria: has cambiado un numero publicado.
