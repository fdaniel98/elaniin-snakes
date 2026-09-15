# Estado del proyecto

## Estado actual

Fase: 0 (Setup) — COMPLETA salvo lo listado en «Bloqueado»
Gate: PASS (2026-09-15, 12 checks, commit 507ea2f) · ./scripts/gate.sh
Loop: engine/src/rules.cpp → CLOSED (5 it.) · snake/src/brain_v0.cpp → CLOSED (3 it.) · scripts/gate.sh → CLOSED (6 it., 1 anulada)
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
| gate | scripts/gate.sh | correctness, robustness, perf, context |
<!-- END:loop-deliverables -->

## Bloqueado / pendiente de decision humana

- [ ] **Tres ajustes del check 9** (ver docs/decisions/ADR-0005-cierre-del-loop.md): exigir
      `checks_added` solo en iteraciones que producen commit; 3 clases distintas en todo el
      ledger en vez de en las tres primeras iteraciones; y encadenamiento por ancestro en
      vez de por igualdad. Las tres reglas originales eran incompatibles entre si. Falta tu
      visto bueno.
- [ ] **Estrechamiento de dos checks**: el grep de generadores prohibidos de la STL (check
      5) solo mira `.cpp`/`.hpp` y descarta comentarios, y el check 7 ignora comentarios en
      el Dockerfile. Sin eso, la documentacion no puede nombrar lo que prohibe.
- [ ] **Partida contra una snake del zoo**: la partida completa del CLI se jugo contra una
      snake tonta propia, no contra el zoo. `docker build` de un repo de terceros ejecuta
      codigo ajeno: hace falta tu confirmacion explicita, una vez por repositorio.
- [ ] **Integracion WSL de Docker Desktop**: no esta activada para `Ubuntu-24.04`, asi que
      dentro de WSL solo hay `docker.exe`. El gate lo acepta, pero conviene activarla
      (Docker Desktop, Settings, Resources, WSL integration).

## Hallazgos abiertos del loop

- [ ] `POST /move` maximo 23.9 ms frente a p99 0.79 ms: es la primera peticion, que paga el
      arranque del servidor y la carga del config. Dentro de presupuesto, pero conviene
      precalentar antes de medir en la fase 2.
- [ ] `placements()` quedo `SIN_VERIFICAR` contra la fuente: el motor oficial no expone
      placements y el JSONL no trae el turno de eliminacion (ver docs/rules.md#r-12). La
      formula de rango compartido promediado es nuestra, no derivada.
- [ ] Duplicaciones de hechos entre documentos que reporto `context-curator` y que solo se
      corrigieron en parte: quedan las de menor severidad (prohibiciones del hot path
      enunciadas en `CLAUDE.md`, `engine/CLAUDE.md` y la skill `cpp-hotpath`).
- [ ] `royale_hazards()` sigue lanzando `logic_error`: es trabajo de la fase 1 y solo tiene
      sentido en la arena (ver docs/rules.md#r-09).

## Desviaciones menores del arbol de archivos especificado

- `tests/test_rng.cpp`: los vectores de referencia del RNG necesitaban su propio archivo.
- `snake/include/snake/config_loader.hpp`: la frontera de JSON necesita cabecera propia
  para que los tests la usen.
- `snake/include/snake/eval/floodfill.hpp` no es un stub: `brain_v0` necesita flood fill.
  `voronoi.hpp` y `features.hpp` si lo son.
- `scripts/smoke.py` y `scripts/mutants.sh`: el check 8 y la prueba de mutantes necesitan
  una implementacion independiente de la del motor.
- `third_party/cpp-httplib/`: ver docs/decisions/ADR-0003-dependencias.md.
- `docs/rules-parametros.md`: `docs/rules.md` se partio en dos para no pasarse del
  presupuesto por tarea (ver docs/INDEX.md#i-02).

## Siguiente accion concreta

Pedir confirmacion humana para construir las imagenes del zoo y jugar la partida de la
DoD 5 contra una snake publica; despues, abrir la fase 1 con `/phase 1`.
