# Estado del proyecto

## Estado actual

Fase: 3 (Training Room MVP) — COMPLETA
Gate: PASS 14/14 en la maquina de referencia (2026-09-18, a3af0e9)
El 11 (lint-zoo) y el 12 (tests del orquestador) son nuevos de esta fase, con 5 venenos.
Loop: `training-room/` → CLOSED en `.loop/3/` (4 iteraciones + auditoria, 5 hallazgos reparados)
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
| training-room | training-room/ | correctness, robustness, perf, context |

El zoo queda fuera a proposito
(ver docs/decisions/ADR-0017-el-instrumento-lleva-loop.md#d-0161). Que entra aqui lo
decide ver docs/decisions/ADR-0008-ambito-del-loop.md#d-0071.
<!-- END:loop-deliverables -->

## Bloqueado / pendiente de decision humana

- [ ] **El fixture `02-spawn-turno2-cola-apilada.json` afirma algo falso:** prohibe `down`
      diciendo que bajar es mortal, y no lo es -el rival apunta hacia abajo y no puede
      subir por la columna 5-. La busqueda lo ve y baja. Decidir si se corrige.

- [ ] **Cuarta snake del campo:** `TheApX/battlesnake-hungry` (MIT, C++). Aprobar un
      repositorio es confirmacion humana (ver zoo/README.md); sin ella `gauntlet-v1` se
      queda con tres snakes del mismo motor y mide menos de lo que parece.

- [ ] **Linea base de la fase 1 en la maquina de referencia:** `./scripts/bench.sh` en
      WSL2; lo de ver docs/performance.md#p-06 es de otra maquina.

## Decisiones humanas

Cada una con su ADR en `docs/decisions/`, que es donde vive el contenido.

## Hallazgos abiertos del loop

- [ ] Los tests de reloj de 5 ms no son deterministas en la maquina de referencia: con la
      maquina cargada dieron 9 ms en el primer fixture y 2 violaciones de 10 000, y con
      ella descargada pasan incluso sin calentar. El calentamiento les devuelve margen
      pero no los hace deterministas (ver
      docs/decisions/ADR-0021-arranque-en-frio.md#adr-0021-abierto).

- [ ] El transporte se come casi todo el presupuesto: el maximo del arbitro son 169 ms y
      el de nuestro codigo 0.388. En el torneo eso costo 8 timeouts en 23 831 movimientos.
      Importa al recalibrar el margen de red de la fase 7 (ver docs/performance.md#p-07).

- [ ] El servidor sigue siendo agotable con 64 conexiones a medio abrir (los hilos del
      pool): una peticion legitima espera hasta el read timeout de 2 s. Acotado, no
      eliminado; en la fase 7 hay un balanceador delante.

- [ ] Dos convenciones propias que la fuente no define: el desempate promediado de
      `placements()` y la secuencia de lados del shrink
      (ver docs/decisions/ADR-0010-rng-del-shrink.md#d-0091). La forma del hazard si esta
      verificada; la arena no reproducira una partida oficial casilla por casilla.
- [ ] `cold_start_ms_max` sigue sin veneno propio en `gate-selftest.sh`. Deuda declarada
      en docs/decisions/ADR-0009-entorno-y-arranque-en-frio.md#d-0083.
- [ ] Las causas de muerte de los RIVALES son ambiguas en su mayoria (349 de 600) y
      seguiran siendolo: no tenemos su cerebro. Las nuestras si, 193 de 194.
- [ ] El repositorio sigue sin remoto: toda la historia vive en un solo disco.
- [ ] `royale_hazards()` no tiene llamante todavia y su precondicion -cadencia >= 1- no
      la comprueba nadie: la arena de la fase 4 tendra que validarla antes de llamar.

## Desviaciones del arbol de archivos

Seis, todas menores y justificadas: ver docs/architecture.md#a-06.

## Siguiente accion concreta

Desplegar v5: falta el ID del proyecto GCP y nada mas. v6 ya se midio y NO entra
(ver docs/experimentos.md#s-supervivencia-r): la snake que se despliega es v5.
