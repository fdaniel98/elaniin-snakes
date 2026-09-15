# Estado del proyecto

## Estado actual

Fase: 0 (Setup) — PARCIAL: falta el check 10 y la partida contra el zoo, ambos con docker
Gate: checks 0-9 en verde; el 10 no se pudo correr donde se cerro la fase (sin registro de imagenes)
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

Los numeros de arriba los regenera `./scripts/sync_state.sh` desde `docs/performance.md`,
que es su unico dueño. Editarlos a mano es un fallo que el gate detecta.

## Entregables de la fase con loop obligatorio

<!-- BEGIN:loop-deliverables -->
| slug | archivo | clases obligatorias |
|---|---|---|
| rules | engine/src/rules.cpp | correctness, robustness, perf |
| brain-v0 | snake/src/brain_v0.cpp | correctness, robustness, perf |
<!-- END:loop-deliverables -->

Que entra en esta tabla y que no lo decide el ambito del loop: ver
docs/decisions/ADR-0008-ambito-del-loop.md#d-0071. El ledger de `scripts/gate.sh`, cerrado
con 8 iteraciones, se conserva en `.loop/0/` como historia.

## Bloqueado / pendiente de decision humana

- [ ] **Correr el gate completo y la autoprueba en la maquina de referencia.** El cierre
      del loop se hizo en un contenedor sin acceso a registro de imagenes, asi que ni el
      check 10 (`docker build` mas contenedor respondiendo) ni su veneno se ejecutaron
      ahi. En WSL2: `./scripts/gate.sh` y `./scripts/gate-selftest.sh`. Es lo unico que
      separa la fase de COMPLETA.
- [ ] **Partida contra una snake del zoo** (criterio 5 de la DoD). Autorizada el
      2026-09-15, pendiente de ejecutarse: necesita `docker build` de un repo de terceros
      y el CLI oficial, y ninguno de los dos llegaba desde ese contenedor. El manifest
      fija el commit SHA aprobado y el contenedor corre aislado: `--read-only`,
      `--cap-drop=ALL`, sin volumenes y con el puerto solo en `127.0.0.1`.
- [ ] **Integracion WSL de Docker Desktop**: no esta activada para `Ubuntu-24.04`, asi que
      dentro de WSL solo hay `docker.exe`. El gate lo acepta, pero conviene activarla
      (Docker Desktop, Settings, Resources, WSL integration).

## Decisiones humanas tomadas el 2026-09-15

- Los tres ajustes del check 9 quedan **aprobados**
  (ver docs/decisions/ADR-0005-cierre-del-loop.md#d-0043).
- Estrechar los checks 5 y 7 para que ignorasen comentarios queda **rechazado**: ambos
  vuelven a su forma estricta (ver docs/decisions/ADR-0006-umbral-de-duplicados.md#d-0051).
- `duplicated_facts` pasa a medirse con `scripts/lint_dupes.py`, su umbral se queda en 0 y
  `max_iterations` sube de 6 a 8 (ver docs/decisions/ADR-0006-umbral-de-duplicados.md#d-0052).
- Los umbrales de clase se exigen a la ultima iteracion de cada clase
  (ver docs/decisions/ADR-0007-umbrales-por-clase.md#d-0061).

## Hallazgos abiertos del loop

- [ ] El maximo de `POST /move` esta muy por encima de su p99. Causa, numeros y veredicto
      en docs/performance.md#p-04; conviene precalentar antes de medir en la fase 2.
- [ ] `placements()` quedo `SIN_VERIFICAR` contra la fuente: el motor oficial no expone
      placements y el JSONL no trae el turno de eliminacion (ver docs/rules.md#r-12). La
      formula de rango compartido promediado es nuestra, no derivada.
- [ ] Los numeros publicados se midieron en la maquina de referencia
      (ver docs/performance.md#p-03). El cierre del loop se hizo en otra, de 2 nucleos, y
      por eso no se publico ninguna medicion nueva: la tabla canonica sigue siendo la del
      commit 8082d1d.
- [ ] `royale_hazards()` sigue lanzando `logic_error`: es trabajo de la fase 1 y solo tiene
      sentido en la arena (ver docs/rules.md#r-09).

## Desviaciones del arbol de archivos

Seis, todas menores y justificadas: ver docs/architecture.md#a-06.

## Siguiente accion concreta

Correr `./scripts/gate.sh` y `./scripts/gate-selftest.sh` completos en WSL2, con docker
disponible, y jugar la partida del criterio 5 contra una snake del zoo; con eso la fase 0
pasa de PARCIAL a COMPLETA.
