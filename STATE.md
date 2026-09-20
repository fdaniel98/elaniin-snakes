# Estado del proyecto

## Estado actual

Fase: 4 (Arena in-process + A/B) — EN CURSO. La 3 quedo COMPLETA.
Gate: PASS 14/14 en la de referencia (2026-09-19, 2f06c27); la fase 4 aun sin gate completo.
Loop: fase 3 CLOSED en `.loop/3/`; la 4 abre cuando este el driver de A/B.
Snake activa: **v5** (busqueda + territorio en hojas + control de longitud). Gano su A/B
contra v4 por -0.6917 y contra v0 por -1.0583: 26 primeros de 60 y ningun cuarto
(ver docs/experimentos.md#s-longitud-r). v0 sigue entero en `v0-baseline.json`. v6 se
midio y no entra (ver docs/experimentos.md#s-supervivencia-r): apagada en `survival.version`.

<!-- BEGIN:perf-snapshot -->
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
<!-- END:perf-snapshot -->

La regenera `./scripts/sync_state.sh` desde su dueño, `docs/performance.md`.

## Entregables de la fase con loop obligatorio

<!-- BEGIN:loop-deliverables -->
| slug | archivo | clases obligatorias |
|---|---|---|
| arena | arena/ | correctness, robustness, perf, context |

El zoo queda fuera a proposito
(ver docs/decisions/ADR-0017-el-instrumento-lleva-loop.md#d-0161). Que entra aqui lo
decide ver docs/decisions/ADR-0008-ambito-del-loop.md#d-0071.
<!-- END:loop-deliverables -->

## Bloqueado / pendiente de decision humana

- [ ] **Desplegar v5:** solo falta el ID del proyecto GCP. Sin eso no hay RTT real y los
      margenes de red siguen siendo un default sin medir.
- [ ] **El fixture `02-spawn-turno2-cola-apilada.json` afirma algo falso:** prohibe `down`
      diciendo que bajar es mortal, y no lo es -el rival apunta hacia abajo y no puede
      subir por la columna 5-. La busqueda lo ve y baja. Decidir si se corrige.
- [ ] **Cuarta snake del campo:** `TheApX/battlesnake-hungry` (MIT, C++). Aprobar un
      repositorio es confirmacion humana (ver zoo/README.md); sin ella `gauntlet-v1` se
      queda con tres snakes del mismo motor y mide menos de lo que parece.
- [ ] **Lineas base en la de referencia:** `./scripts/bench.sh` y `sonda_arena` en WSL2;
      ver docs/performance.md#p-06 y ver docs/performance.md#p-08 son de otra maquina.

## Hallazgos abiertos del loop

- [ ] Los tests de reloj de 5 ms no son deterministas en la de referencia: cargada dio
      9 ms y 2 violaciones de 10 000; descargada pasan sin calentar
      (ver docs/decisions/ADR-0021-arranque-en-frio.md#adr-0021-abierto).
- [ ] El transporte se come casi todo el presupuesto: maximo del arbitro 169 ms contra
      0.388 del codigo, y 8 timeouts en 23 831 movimientos. Importa al recalibrar el
      margen de red de la fase 7 (ver docs/performance.md#p-07).
- [ ] El servidor es agotable con 64 conexiones a medio abrir: una peticion legitima
      espera al read timeout de 2 s. Acotado; en la fase 7 hay un balanceador delante.
- [ ] Dos convenciones propias que la fuente no define: el desempate promediado de
      `placements()` y la secuencia de lados del shrink
      (ver docs/decisions/ADR-0010-rng-del-shrink.md#d-0091). La arena no reproduce una
      partida oficial casilla a casilla.
- [ ] `cold_start_ms_max` sigue sin veneno propio en `gate-selftest.sh`. Deuda declarada
      en docs/decisions/ADR-0009-entorno-y-arranque-en-frio.md#d-0083.
- [ ] Las causas de muerte de los RIVALES son ambiguas (349 de 600): no tenemos su
      cerebro. Las nuestras si.
- [ ] `budget_nodes` sin calibrar contra el presupuesto de despliegue: hoy un numero de
      nodos no se traduce a ms (ver docs/decisions/ADR-0030-presupuesto-por-nodos.md#d-0302).
- [ ] `tools/` fuera de los checks 4 y 5
      (ver docs/decisions/ADR-0035-ambito-de-los-checks-4-y-5.md#d-0352).
- [ ] `budget_nodes` calibrado solo en el contenedor (20 774 = 200 ms): hay que correr
      `sonda_arena --calibrar` en la maquina de referencia antes de su primer A/B.

## Desviaciones del arbol de archivos

Seis, todas menores y justificadas: ver docs/architecture.md#a-06.

## Siguiente accion concreta

Correr `training-room/afina.py`: SPSA sobre los ~20 pesos nunca afinados. La propuesta se
verifica con un A/B de arena de campo distinto a las dos ramas y, si gana, con el gauntlet.
Despues, el loop de la fase 4.
