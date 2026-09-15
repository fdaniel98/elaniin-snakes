---
title: Fuentes externas verificadas
read_when: "antes de afirmar algo sobre una herramienta externa o de re-verificar una regla"
authority: canonical
source: BattlesnakeOfficial/rules@87e094e2e1c224e9dea67743fd3c2249137c4057
last_verified: 2026-09-15
size_bytes: 3163
---

Este archivo lleva **fuentes**, no hechos: qué se consultó, en qué SHA, cuándo, y qué
documento del repo es dueño de lo que se derivó. Los hechos sobre reglas viven en
`docs/rules.md` y `docs/rules-parametros.md`, con su cita `archivo.go:linea`
(ver docs/INDEX.md#i-04).

## S-01 Tabla de fuentes {#s-01}

| Fuente | SHA / version | Fecha | Que se derivo | Dueño de lo derivado |
|---|---|---|---|---|
| `github.com/BattlesnakeOfficial/rules` (Go) | `87e094e2e1c224e9dea67743fd3c2249137c4057` | 2026-09-14 | Orden de fases del turno, colisiones, hambre, hazards, alimentacion, colocacion inicial, fin de partida, movimiento por defecto | ver docs/rules.md#r-02 |
| mismo repo, `maps/royale.go` | mismo SHA | 2026-09-14 | Modelo del shrink de royale y su RNG | ver docs/rules.md#r-09 |
| mismo repo, `client/models.go` | mismo SHA | 2026-09-14 | Rutas JSON exactas del request | ver docs/rules-parametros.md#r-20 |
| mismo repo, `constants.go` | mismo SHA | 2026-09-14 | Constantes del motor que no viajan en el payload | ver docs/rules-parametros.md#r-21 |
| mismo repo, `cli/commands/play.go` | mismo SHA | 2026-09-14 | Flags y defaults del arbitro, verificados tambien con `battlesnake play --help` compilado | ver docs/rules-parametros.md#r-20 |
| mismo repo, `cli/commands/output.go` | mismo SHA | 2026-09-14 | Formato del JSONL y ausencia de placements | ver docs/rules.md#r-12 |
| mismo repo, `rand.go`, `settings.go` | mismo SHA | 2026-09-14 | El motor usa `math/rand` de Go: no reproducible desde C++ | ver docs/rules-parametros.md#r-99 |
| `github.com/BattlesnakeOfficial/snake-zoo` | `6c2edcdb6e35a5ccc03a9cdd80e4af74e9baf4a8` | 2026-09-14 | Formato del manifest TOML, requisito de Rust, y que su runner lanza contenedores sin aislamiento (`src/docker.rs:80-91`) | `zoo/README.md` |
| `cpp-httplib` | v0.18.7 (MIT) | 2026-09-15 | Cabecera unica vendorizada en `third_party/` | ver docs/decisions/ADR-0003-dependencias.md |

## S-02 Ejecutado en esta maquina, no solo leido {#s-02}

| Comprobacion | Cuando | Resultado |
|---|---|---|
| `battlesnake play --help` con el CLI compilado del SHA fijado | 2026-09-15 | Flags y defaults coinciden con `cli/commands/play.go:97-117` |
| Partida completa `battlesnake play -g royale -m royale` contra nuestra v0 | 2026-09-15 | 11 turnos, JSONL en `docs/results/2026-09-15-v0-vs-dummy.jsonl` |
| Payload literal de `/move` capturado del arbitro | 2026-09-15 | `tests/fixtures/13-payload-literal-del-cli.json` |

## S-03 Pendiente de verificar {#s-03}

- Referencia de API en `docs.battlesnake.com`: nombre y unidad exactos del campo de
  latencia que exige la DoD de la fase 7 (ver docs/rules-parametros.md#r-99).
- Que ruta de hazards ejecuta realmente el CLI con `-g royale -m royale`: el stage del
  pipeline y el hook del mapa calculan lo mismo, pero solo se ha verificado leyendo el
  codigo, no instrumentando una partida.
