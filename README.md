# Battlesnake Royale (C++20) + Training Room

Battlesnake competitiva para el formato **Royale** (11x11, 4 serpientes, timeout 500 ms), escrita
en C++20 con foco en rendimiento medible, y un Training Room local para entrenarla y compararla
contra snakes publicas.

## Que hay aqui

| Directorio | Que es |
|---|---|
| `engine/` | Libreria de reglas pura, sin I/O ni asignaciones en el hot path |
| `snake/` | Cerebro (`decide`) y servidor HTTP que habla el protocolo de Battlesnake |
| `arena/` | Self-play in-process con presupuesto por nodos (fase 4) |
| `training-room/` | Orquestador de torneos, SQLite, ratings, reportes (fase 3) |
| `zoo/` | Integracion con snakes publicas en contenedores aislados (fase 3) |
| `tests/` | Catch2: bitboard, reglas, parser del ruleset, cerebro, diferencial |
| `bench/` | Google Benchmark sobre el motor |
| `scripts/` | `gate.sh` (el oraculo de "hecho") y sus satelites |
| `docs/` | Capa de contexto: reglas citadas, invariantes, ADRs, context packs |
| `.loop/` | Ledgers del loop de ingenieria (§14 del prompt maestro) |

## Requisitos

El toolchain vive en **WSL2 (Ubuntu 24.04)**; el repositorio se edita desde Windows. Ver
`docs/decisions/ADR-0001-donde-corre-el-harness.md`.

```bash
./scripts/bootstrap.sh
```

## Comandos

```bash
./scripts/gate.sh             # veredicto de "hecho": 0 = PASA
./scripts/gate.sh --fast      # subconjunto rapido; no sirve para cerrar fase
cmake --preset release && cmake --build --preset release
ctest --preset release
./scripts/bench.sh
./snake/../build/release/snake/battlesnake-server   # servidor local en :8080
```

## Estado

`STATE.md` tiene la fase vigente, el gate, el loop y la siguiente accion concreta.
Para una sesion nueva de agente: leer `CLAUDE.md`, luego `STATE.md`, luego `docs/INDEX.md`.

## Licencia

Codigo propio. Las snakes de `zoo/` conservan la licencia de sus autores y no se copia su codigo.
