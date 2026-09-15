---
title: Parametros del ruleset, variantes y preguntas abiertas
read_when: "antes de parsear el request, de tocar el config o de dar por cierto un default"
authority: canonical
source: BattlesnakeOfficial/rules@87e094e2e1c224e9dea67743fd3c2249137c4057
last_verified: 2026-09-14
size_bytes: 4110
---

Separado de `docs/rules.md` por presupuesto de bytes, no por tema: las mecanicas del
turno estan alli y estas son sus tablas de referencia. Misma fuente y mismo SHA.

## R-13 Variantes no Royale {#r-13}

| Variante | Diferencia verificada | Cita |
|---|---|---|
| `standard` | Sin stage de hazards | `standard.go:8-15` |
| `wrapped` | El movimiento envuelve los bordes: fuera de rango salta al opuesto | `wrapped.go:12-40` |
| `constrictor` | Se borra toda la comida y todas crecen cada turno; la cola nunca se libera | `constrictor.go:25-47` |
| `wrapped_constrictor` | Suma de las dos anteriores | `constrictor.go:14-23` |
| `solo` | Condicion de fin distinta | `ruleset.go:96` |

Nuestro cerebro declara soporte solo para `standard` y `royale`; el resto entra en modo degradado
seguro (ver `snake/CLAUDE.md`). En `constrictor` el tail-escape es directamente mortal por
[R-04](#r-04).

## R-20 Parametros que viajan en el request {#r-20}

Ruta JSON exacta, verificada contra `client/models.go` en el SHA fijado. **Prohibido hardcodearlos**
(regla de oro 5); usar el fallback emite `WARN`.

| Ruta JSON | Tipo | Default del arbitro | Cita |
|---|---|---|---|
| `game.timeout` | int (ms) | 500 | `client/models.go:18`, `cli/commands/play.go:101` |
| `game.ruleset.name` | string | `standard` | `client/models.go:52`, `cli/commands/play.go:103` |
| `game.map` | string | `standard` | `client/models.go:17`, `cli/commands/play.go:104` |
| `game.ruleset.settings.foodSpawnChance` | int (porcentaje) | 15 | `client/models.go:59`, `cli/commands/play.go:114` |
| `game.ruleset.settings.minimumFood` | int | 1 | `client/models.go:60`, `cli/commands/play.go:115` |
| `game.ruleset.settings.hazardDamagePerTurn` | int | 14 | `client/models.go:61`, `cli/commands/play.go:116` |
| `game.ruleset.settings.royale.shrinkEveryNTurns` | int | 25 arbitro / 20 motor | `client/models.go:69-70`, `cli/commands/play.go:117`, `maps/royale.go:49` |
| `board.width`, `board.height` | int | 11 | `client/models.go:24-25`, `cli/commands/play.go:97-98` |

`settings` **no es plano**: `royale` es un objeto anidado (`client/models.go:63,68-70`). El nombre
interno del parametro de hazard en el motor Go es `damagePerTurn` (`constants.go:52`), distinto del
campo JSON `hazardDamagePerTurn`; solo importa al invocar el CLI, no al parsear el request.

## R-21 Constantes del motor que NO viajan en el request {#r-21}

| Constante | Valor | Cita |
|---|---|---|
| Salud maxima | 100 | `constants.go:19` (`SnakeMaxHealth`) |
| Longitud inicial | 3 segmentos apilados | `constants.go:20` (`SnakeStartSize`), `board.go:217-223` |
| Crecimiento al comer | mas 1 segmento, duplicado de la cola | `standard.go:361-365` |
| Salud restaurada al comer | 100 absoluto, no incremento | `standard.go:356-359` |
| Perdida de salud por turno | 1 | `standard.go:124` |
| Tamaños de tablero nombrados | 7, 11, 19, 21, 25 | `constants.go:13-17` |

Buscar cualquiera de estas en el payload es un error de diseño: no estan ahi.

## R-99 Preguntas abiertas {#r-99}

`authority: speculative` -- prohibido implementar contra esto (regla de oro 9).

1. **Reproducibilidad del RNG.** El motor usa `math/rand` de Go (`rand.go:31-53`). No lo
   reproducimos desde C++ sin reimplementar `rngSource`. Impacto: el test diferencial de la Fase 1
   debe **inyectar** comida y hazards del log en vez de generarlos. Decidido asi; no es bloqueante.
2. **Semilla 0.** Si la semilla es 0 el motor cae al RNG global no reproducible
   (`settings.go:50-52`). El Training Room debe pasar siempre una semilla distinta de 0.
3. **Latencia reportada.** El campo que la DoD de la Fase 7 exige aparece como `you.latency`, un
   string, en el payload (`client/models.go:36`), pero su unidad y su origen no estan verificados
   contra `docs.battlesnake.com`. Pendiente de la Fase 7.
