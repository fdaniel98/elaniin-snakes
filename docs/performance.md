---
title: Numeros medidos
read_when: "antes de afirmar cualquier cosa sobre rendimiento, y despues de cada bench"
authority: canonical
last_verified: 2026-09-15
size_bytes: 3617
---

Este archivo es el **unico dueño** de todo numero medido. `STATE.md` no tiene numeros
propios: su bloque `perf-snapshot` se regenera desde aqui con `scripts/sync_state.sh`, y
el gate falla si alguien lo edita a mano.

Lo que no se ha ejecutado se escribe `no medido`. Nunca se estima un numero.

## P-01 Presupuesto de latencia {#p-01}

Fijo por contrato; cambiarlo exige aprobacion humana explicita y un ADR.

| Concepto | Valor | Origen |
|---|---|---|
| `timeout` anunciado por el arbitro | 500 ms | `game.timeout` del request (ver docs/rules-parametros.md#r-20) |
| Margen de red | 100 ms | `time.network_margin_ms` de `snake/config/default.json` |
| Margen de seguridad | 50 ms | `time.safety_margin_ms` |
| Deadline de computo | 350 ms como techo | `time.max_compute_ms` |
| p99 de `POST /move` local sobre fixtures | 50 ms como techo | check 8 del gate |
| Maximo de `POST /move` local | 150 ms como techo | check 8 del gate |

Pasar de 350 ms de computo es fallo duro. El margen de red se recalibra con el p99 de RTT
real medido en la fase 7 y se reescribe en `default.json`.

## P-02 Como se miden los numeros publicables {#p-02}

Un numero medido con `-march=native` **no** puede usarse como linea base: no es el binario
que se despliega. Por eso hay dos presets de medicion.

| Preset | Flags | Etiqueta | Uso |
|---|---|---|---|
| `bench-deployisa` | `-O3 -march=x86-64-v2 -mtune=generic` | publicable | linea base y comparaciones |
| `bench` | `-O3 -march=native` | `local-only` | exploracion en la maquina de desarrollo |

`scripts/bench.sh` usa el primero por defecto y etiqueta el segundo como `local-only`.

## P-03 Maquina de referencia {#p-03}

| Campo | Valor |
|---|---|
| Host | Windows 11 con WSL2 (Ubuntu 24.04), repo en `/mnt/c` |
| CPU logicas visibles | 8 |
| Memoria visible | 15.9 GB |
| Compilador | clang 18.1.3 |
| Fecha | 2026-09-15 |

Dos corridas con campos distintos en esta tabla **no se comparan**.

<!-- BEGIN:perf-canonical -->
| metrica | valor | commit | fecha |
|---|---|---|---|
| `apply()/s` (1 hilo, bench-deployisa) | 6.10 M/s (164 ns) | 8082d1d | 2026-09-15 |
| `legal_moves()/s` (1 hilo, bench-deployisa) | 20.43 M/s (48.9 ns) | 8082d1d | 2026-09-15 |
| `decide()/s` (1 hilo, bench-deployisa) | 1.71 M/s (585 ns) | 8082d1d | 2026-09-15 |
| copias de estado/s | 106.35 M/s (9.40 ns) | 8082d1d | 2026-09-15 |
| `POST /move` p50 (local, 13 fixtures x 20) | 0.43 ms | 8082d1d | 2026-09-15 |
| `POST /move` p99 (local, 13 fixtures x 20) | 0.79 ms | 8082d1d | 2026-09-15 |
| `POST /move` maximo (local, 13 fixtures x 20) | 23.88 ms | 8082d1d | 2026-09-15 |
| asignaciones dinamicas en `apply`/`legal_moves`/`decide` | 0 / 0 / 0 | 8082d1d | 2026-09-15 |
<!-- END:perf-canonical -->

## P-04 Historico {#p-04}

Una fila por medicion publicada, con su commit. Las salidas crudas de Google Benchmark
van a `docs/results/bench-*.json`, que queda fuera del lint de docs.

| Fecha | Cambio | Metrica | Antes | Despues | Veredicto |
|---|---|---|---|---|---|
| 2026-09-15 | linea base inicial de la fase 0 (commit 8082d1d) | todas | - | ver tabla canonica | LINEA BASE |

El maximo de `POST /move` (23.88 ms) esta 30 veces por encima del p99 (0.79 ms): es la
primera peticion, que paga el arranque del servidor y la carga del config. Queda como
hallazgo abierto de la clase `perf`, no como regresion: el presupuesto de la fase 0 son
50 ms de p99 y 150 ms de maximo, y ambos se cumplen con margen.
