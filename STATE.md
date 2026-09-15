# Estado del proyecto

## Estado actual

Fase: 0 (Setup) — PARCIAL: el loop del entregable `gate` esta BLOQUEADO
Gate: FAIL en el check 9 (lint del loop) por el ledger BLOQUEADO; los otros 10 checks en verde
Loop: engine/src/rules.cpp → CLOSED (5 it.) · snake/src/brain_v0.cpp → CLOSED (3 it.) · scripts/gate.sh → BLOQUEADO (6 it., techo alcanzado)
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

- [ ] **DECISION QUE BLOQUEA EL CIERRE DE LA FASE.** El entregable `scripts/gate.sh` agoto
      sus 6 iteraciones (el techo) con un hallazgo abierto: `duplicated_facts = 13` frente
      a un umbral de 0 en `config/loop.json`. Las duplicaciones son reales pero menores
      (el mismo hecho enunciado en `CLAUDE.md`, una skill y un invariante, en vez de
      enlazado). Dos salidas, y la eleccion es tuya:
      **(a)** seguir deduplicando la documentacion hasta llegar a 0, o
      **(b)** fijar un umbral realista en `config/loop.json` (por ejemplo 3) con su ADR.
      Tocar `config/loop.json` exige tu aprobacion explicita (regla de oro 10), por eso no
      lo he hecho.
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
- [ ] **F-G06 (abierto, mayor):** 13 hechos duplicados entre documentos. Se corrigieron
      los tres de mayor severidad (hot path, generadores de la STL, ISA de deploy); quedan
      diez, listados por `context-curator` en la auditoria final. Es lo que bloquea el
      cierre del loop del gate.
- [ ] `royale_hazards()` sigue lanzando `logic_error`: es trabajo de la fase 1 y solo tiene
      sentido en la arena (ver docs/rules.md#r-09).

## Desviaciones del arbol de archivos

Seis, todas menores y justificadas: ver docs/architecture.md#a-06.

## Siguiente accion concreta

Decidir entre (a) terminar de deduplicar los diez hechos que quedan o (b) aprobar un
umbral realista de `duplicated_facts_max` con su ADR; aplicar la opcion elegida, cerrar el
ledger del gate y volver a correr `./scripts/gate.sh` completo.
