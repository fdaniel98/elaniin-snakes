# Estado del proyecto

## Estado actual

Fase: 0 (Setup) — PARCIAL: solo falta reconfirmar los 4 venenos del check 9
Gate: **12 checks PASS, ninguno en rojo** (2026-09-15, maquina de referencia, 229 s)
Loop: engine/src/rules.cpp → CLOSED (5 it.) · snake/src/brain_v0.cpp → CLOSED (3 it.)
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

Esa tabla la regenera `./scripts/sync_state.sh` desde `docs/performance.md`, su unico
dueño; editarla a mano es un fallo que el gate detecta.

## Entregables de la fase con loop obligatorio

<!-- BEGIN:loop-deliverables -->
| slug | archivo | clases obligatorias |
|---|---|---|
| rules | engine/src/rules.cpp | correctness, robustness, perf |
| brain-v0 | snake/src/brain_v0.cpp | correctness, robustness, perf |
<!-- END:loop-deliverables -->

Que entra aqui lo decide el ambito del loop
(ver docs/decisions/ADR-0008-ambito-del-loop.md#d-0071). El ledger de `scripts/gate.sh`,
cerrado con 8 iteraciones, se conserva en `.loop/0/`.

## Bloqueado / pendiente de decision humana

- [ ] **Correr el gate completo y la autoprueba en la maquina de referencia.** El cierre
      del loop se hizo en un contenedor sin acceso a registro de imagenes, asi que ni el
      check 10 (`docker build` mas contenedor respondiendo) ni su veneno se ejecutaron
      ahi. En WSL2: `./scripts/gate.sh` y `./scripts/gate-selftest.sh`. Es lo unico que
      separa la fase de COMPLETA.
- [ ] **Reconfirmar el check 9:** `./scripts/gate-selftest.sh 9`. La corrida completa dio
      19 venenos cazados y 3 fallidos, los tres del check 9 y por el mismo defecto, ya
      reparado en el commit 891d799. Es lo unico que falta para el criterio 11; el 3 ya
      esta (12 checks PASS).
- [ ] **Integracion WSL de Docker Desktop**: no esta activada para `Ubuntu-24.04`, asi que
      dentro de WSL solo hay `docker.exe`. El gate lo acepta, pero conviene activarla
      (Docker Desktop, Settings, Resources, WSL integration).

## Decisiones humanas del 2026-09-15

Cinco, todas con su ADR, que es donde vive el contenido: ajustes del check 9
(ver docs/decisions/ADR-0005-cierre-del-loop.md#d-0043), duplicados medidos y techo
(ver docs/decisions/ADR-0006-umbral-de-duplicados.md#d-0052, que tambien recoge el
rechazo a estrechar los checks 5 y 7), umbrales por clase
(ver docs/decisions/ADR-0007-umbrales-por-clase.md#d-0061), ambito del loop
(ver docs/decisions/ADR-0008-ambito-del-loop.md#d-0071) y arranque en frio
(ver docs/decisions/ADR-0009-entorno-y-arranque-en-frio.md#d-0081).

## Hallazgos abiertos del loop

- [ ] El maximo de `POST /move` esta muy por encima de su p99; causa y veredicto,
      ver docs/performance.md#p-04. Conviene precalentar antes de medir en la fase 2.
- [ ] `placements()` quedo `SIN_VERIFICAR` contra la fuente: el motor oficial no expone
      placements y el JSONL no trae el turno de eliminacion (ver docs/rules.md#r-12). La
      formula de rango compartido promediado es nuestra, no derivada.
- [ ] La tabla canonica sigue siendo la del commit 8082d1d: parte del trabajo se hizo en
      otra maquina y por eso no se publico medicion nueva (ver docs/performance.md#p-03).
- [ ] `cold_start_ms_max` es un umbral nuevo sin veneno propio en `gate-selftest.sh`: el
      veneno del check 8 cubre el movimiento ilegal, no el arranque en frio. Deuda
      declarada en docs/decisions/ADR-0009-entorno-y-arranque-en-frio.md#d-0083.
- [ ] `royale_hazards()` sigue lanzando `logic_error`: es trabajo de la fase 1 y solo tiene
      sentido en la arena (ver docs/rules.md#r-09).
- [ ] Criterio 5 cerrado: v0 gana contra Eremetic Eric en 80 turnos, que murio de hambre
      dentro del hazard (`docs/results/2026-09-15-v0-vs-eremetic-eric.md`).

## Desviaciones del arbol de archivos

Seis, todas menores y justificadas: ver docs/architecture.md#a-06.

## Siguiente accion concreta

Correr `./scripts/gate-selftest.sh 9` en WSL2; con sus 4 venenos cazados la fase 0 pasa a
COMPLETA y se abre la fase 1 (reglas Royale completas y test diferencial contra >=500
partidas JSONL del CLI).
