# Estado del proyecto

## Estado actual

Fase: 0 (Setup) — EN CURSO
Gate: pendiente de la primera pasada completa
Loop: engine/src/rules.cpp → pendiente · snake/src/brain_v0.cpp → pendiente · scripts/gate.sh → pendiente
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

- [ ] **Cierre del loop vs antifraude** (ver docs/decisions/ADR-0005-cierre-del-loop.md):
      exigir `checks_added` en toda iteracion y a la vez que las dos ultimas compartan
      `commit_after` es imposible. Resuelto exigiendo `checks_added` solo cuando la
      iteracion produce commit, y `commands_run` cuando no. Falta tu visto bueno.
- [ ] **Integracion de snake-zoo**: su runner lanza contenedores sin aislamiento y publica
      en todas las interfaces. Se usaran sus manifests TOML con un runner propio que
      aplique `--user`, `--read-only`, `--cap-drop=ALL` y publicacion solo en `127.0.0.1`.
      Construir la imagen de una snake de terceros ejecuta codigo ajeno: hace falta tu
      confirmacion explicita una vez por repositorio.
- [ ] **Integracion WSL de Docker Desktop**: no esta activada para `Ubuntu-24.04`, asi que
      dentro de WSL solo hay `docker.exe`. El gate lo acepta, pero conviene activarla en
      Docker Desktop (Settings, Resources, WSL integration).

## Hallazgos abiertos del loop

- [ ] (ninguno todavia: el loop aun no ha corrido)

## Desviaciones menores del arbol de archivos especificado

- `tests/test_rng.cpp`: los vectores de referencia del RNG necesitaban su propio archivo.
- `snake/include/snake/config_loader.hpp`: la frontera de JSON necesita cabecera propia
  para que los tests la usen.
- `snake/include/snake/eval/floodfill.hpp` no es un stub: `brain_v0` necesita flood fill.
  `voronoi.hpp` y `features.hpp` si lo son.
- `scripts/smoke.py` y `scripts/mutants.sh`: el check 8 y la prueba de mutantes necesitan
  una implementacion independiente de la del motor.
- `third_party/cpp-httplib/`: ver docs/decisions/ADR-0003-dependencias.md.

## Siguiente accion concreta

Ejecutar `./scripts/gate.sh` completo y arreglar el primer check en rojo.
