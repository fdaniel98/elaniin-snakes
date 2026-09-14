---
title: Fuentes externas verificadas
read_when: "antes de afirmar cualquier cosa sobre reglas, CLI oficial o snake-zoo"
authority: canonical
source: BattlesnakeOfficial/rules@87e094e2e1c224e9dea67743fd3c2249137c4057
last_verified: 2026-09-14
size_bytes: 5193
---

## Tabla de fuentes

| Fuente | SHA / versión | Fecha consulta | Qué se derivó |
|---|---|---|---|
| `github.com/BattlesnakeOfficial/rules` (Go) | `87e094e2e1c224e9dea67743fd3c2249137c4057` (clone `--depth 1` de la rama por defecto) | 2026-09-14 | Orden de stages, colisiones, hambre, hazards, feeding, spawn, movimiento por defecto, constantes del motor |
| `github.com/BattlesnakeOfficial/rules` — `cli/commands/play.go` | mismo SHA | 2026-09-14 | Flags reales de `battlesnake play` y sus defaults |
| `github.com/BattlesnakeOfficial/rules` — `client/models.go` | mismo SHA | 2026-09-14 | Rutas JSON exactas del payload de `/start`, `/move`, `/end` |
| `github.com/BattlesnakeOfficial/snake-zoo` | `6c2edcdb6e35a5ccc03a9cdd80e4af74e9baf4a8` | 2026-09-14 | Formato de manifest TOML, requisito de toolchain Rust, argumentos reales de `docker run` |

## Qué está verificado en esta sesión (Bloque A)

Cada fila cita `archivo.go:línea` del SHA fijado arriba.

| Hecho | Cita | Nota |
|---|---|---|
| Orden de stages de Royale | `royale.go:7-15` | `game_over` → `movement` → `starvation` → `hazard_damage` → `feed_snakes` → `elimination` → `spawn_hazards.shrink_map` |
| El primer stage del turno es `game_over`, no el movimiento | `ruleset.go:78-84`, `royale.go:8` | `NamedRuleset` antepone `StageGameOverStandard` y descarta el primer elemento de la lista de la variante |
| Salud máxima 100, longitud inicial 3 | `constants.go:16-17` (`SnakeMaxHealth`, `SnakeStartSize`) | No viajan en el payload |
| Comer restaura salud a 100 y añade un segmento duplicado en la cola | `standard.go:356-365` (`feedSnake`, `growSnake`) | El duplicado se apila sobre el último segmento |
| Las serpientes nacen con sus 3 segmentos apilados en la misma casilla | `board.go:217-223` (`PlaceSnakesFixed`) | La cola no se libera en los primeros turnos |
| Movimiento por defecto ante respuesta inválida/ausente | `standard.go:61-63`, `standard.go:90-116` (`getDefaultMove`) | Se deriva de cabeza vs cuello; si no hay cuello usable, `"up"` |
| El daño de hazard **no** se aplica si hay comida en la casilla de la cabeza | `standard.go:143-152` | Excepción no obvia |
| El daño de hazard es plano por casilla, pero se aplica una vez por cada entrada de `b.Hazards` que coincida | `standard.go:141-166` | En el mapa royale no hay duplicados generados |
| El hazard se evalúa solo sobre la cabeza | `standard.go:140-142` | |
| Cabeza a cabeza: pierde la **≤** en longitud (empate ⇒ mueren ambas) | `standard.go:322-327` (`snakeHasLostHeadToHead`) | |
| Orden de evaluación de eliminación: hambre/fuera de tablero → autocolisión → cuerpo rival → cabeza-cabeza | `standard.go:190-278` | Las colisiones se **aplican** después de evaluarlas todas (`standard.go:280-289`) |
| El cuello no es regla especial: es colisión con el propio cuerpo | `standard.go:310-320` (`snakeHasBodyCollided` salta `i == 0`) | |
| Los hazards de royale se **recalculan desde cero** cada turno | `maps/royale.go:58-86` | `editor.ClearHazards()` y regeneración completa |
| El lado de cada shrink sale de un RNG sembrado por la semilla de la partida, siempre re-sembrado en turno 0 | `maps/royale.go:61-78`, `settings.go:46-56` (`GetRand(0)`) | Secuencia fija por partida; con repetición de lado |
| `shrinkEveryNTurns` default del motor: 20; del CLI: 25 | `maps/royale.go:49`; `cli/commands/play.go:117` | Divergencia real entre defaults |
| Rutas JSON exactas: `game.ruleset.settings.hazardDamagePerTurn` y `game.ruleset.settings.royale.shrinkEveryNTurns` | `client/models.go` (`RulesetSettings`, `RoyaleSettings`) | El nombre interno del parámetro es `damagePerTurn` (`constants.go:52`), distinto del campo JSON |
| Flags reales de `battlesnake play` y defaults | `cli/commands/play.go:97-117` | `--timeout` 500, `--foodSpawnChance` 15, `--minimumFood` 1, `--hazardDamagePerTurn` 14, `--shrinkEveryNTurns` 25, `--seed`, `--output` |
| El JSONL del CLI es: 1 línea `game`, N líneas `SnakeRequest`, 1 línea `result` con `winnerId`/`isDraw` | `cli/commands/output.go:40-63` | No incluye placements ni `EliminatedOnTurn` |
| El motor usa `math/rand` de Go para comida, colocación y shrink | `rand.go:31-53` | No reproducible bit a bit desde C++ sin reimplementar `rngSource` de Go |
| snake-zoo requiere toolchain Rust (`cargo install --path .`) | `README.md` (Quick Start) | |
| snake-zoo lanza contenedores sin flags de aislamiento y publica en todas las interfaces | `src/docker.rs:80-91` (`run -d --name … -p 0:<port>`) | Incompatible con §10.2 del prompt maestro: usaremos sus manifests, no su runner |

## Pendiente de verificar en el Bloque B

- Salida literal de `battlesnake play --help` (requiere compilar el CLI).
- Payload literal emitido por el CLI contra un servidor real (fixture exigido por §5).
- Referencia de API en `docs.battlesnake.com` (nombre exacto del campo de latencia para la DoD de la Fase 7).
