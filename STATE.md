# Estado del proyecto

## Estado actual

Fase: 3 (Training Room MVP) — en curso. La 2 cerro en c5cead7
Gate: 14 checks; el 11 (lint-zoo) y el 12 (tests del orquestador) son nuevos de esta
fase, con cinco venenos. El veredicto vigente sigue siendo el de la fase 2 en bb99e84
Loop: `.loop/3/` sin abrir todavia; los de las fases 1 y 2 quedaron CLOSED
Snake activa: v0-baseline

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

El zoo se queda fuera del ambito a proposito
(ver docs/decisions/ADR-0017-el-instrumento-lleva-loop.md#d-0161).
<!-- END:loop-deliverables -->

Que entra aqui lo decide ver docs/decisions/ADR-0008-ambito-del-loop.md#d-0071.

## Bloqueado / pendiente de decision humana

- [ ] **Cuarta snake del campo:** `TheApX/battlesnake-hungry` (MIT, Dockerfile propio,
      C++). Aprobar un repositorio es confirmacion humana por repositorio
      (ver zoo/README.md); sin ella el `gauntlet-v1` se queda con tres snakes del mismo
      motor, `coreyja/battlesnake-rs`, y eso mide menos de lo que parece.


- [ ] **Linea base de la fase 1 en la maquina de referencia:** `./scripts/bench.sh` en
      WSL2. Lo medido hasta ahora, en ver docs/performance.md#p-06, es de otra maquina.

## Decisiones humanas

Cada una con su ADR, que es donde vive el contenido: `docs/decisions/`. Las de la fase 1
son el Rng propio del shrink (ver docs/decisions/ADR-0010-rng-del-shrink.md#d-0091) y el
corpus del diferencial (ver docs/decisions/ADR-0011-corpus-del-diferencial.md#d-0101).

## Hallazgos abiertos del loop

- [ ] El maximo que mide el arbitro son 169 ms y el de nuestro codigo 0.388: 168 ms que no
      son el cerebro. Medido y sin tocar; importa al recalibrar el margen de red en la
      fase 7 (ver docs/performance.md#p-07).

- [ ] El servidor sigue siendo agotable con tantas conexiones a medio abrir como hilos
      tiene el pool (64): entonces una peticion legitima espera hasta el read timeout de
      2 s. Acotado, no eliminado; en la fase 7 hay un balanceador delante.

- [ ] La tabla de causas de muerte que pide el Training Room de la fase 3 no va a tener
      contraste externo: ver docs/rules.md#r-12.
- [ ] El reparto de puestos de `placements()` sigue siendo una convencion propia. El
      diferencial verifica el turno de eliminacion y que el reparto es valido -suma
      n(n+1)/2, ningun rango fuera de rango-, pero el desempate promediado no se deriva
      de la fuente porque la fuente no lo define.
- [ ] La secuencia de lados del shrink es nuestra
      (ver docs/decisions/ADR-0010-rng-del-shrink.md#d-0091): la arena no reproducira una
      partida oficial casilla por casilla. La forma si esta verificada.
- [ ] `cold_start_ms_max` sigue sin veneno propio en `gate-selftest.sh`. Deuda declarada
      en docs/decisions/ADR-0009-entorno-y-arranque-en-frio.md#d-0083.
- [ ] Con el JSONL solo, la causa de muerte queda ambigua en 20 de 24 casos medidos: los
      movimientos no se exportan y los cuatro de la serpiente que muere son enumerables
      pero no distinguibles. Nuestro servidor ya registra el movimiento elegido; falta
      capturar su log por partida, y entonces nuestras muertes pasan a ser exactas. Las de
      los rivales seguiran siendo ambiguas y hay que decirlo en el reporte.

- [ ] El repositorio sigue sin remoto: toda la historia vive en un solo disco.
- [ ] `royale_hazards()` no tiene llamante todavia y su precondicion -cadencia >= 1- no
      la comprueba nadie: la arena de la fase 4 tendra que validarla antes de llamar.

## Desviaciones del arbol de archivos

Seis, todas menores y justificadas: ver docs/architecture.md#a-06.

## Siguiente accion concreta

Leer el resultado del torneo de 200 partidas que corre en la maquina de referencia, y
escribir el reporte. Las causas de muerte necesitan antes capturar el log de nuestro
propio servidor: con el JSONL solo, 20 de 24 muertes quedan ambiguas.
