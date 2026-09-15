---
title: Reglas de Royale derivadas del codigo Go
read_when: "antes de tocar engine/src/rules.cpp, de escribir un fixture o de discutir mecanicas del juego"
authority: canonical
source: BattlesnakeOfficial/rules@87e094e2e1c224e9dea67743fd3c2249137c4057
last_verified: 2026-09-14
size_bytes: 12211
---

Toda afirmacion de este archivo cita `archivo.go:linea` del SHA del front-matter. Lo no verificable
contra esa fuente esta marcado como `speculative` y vive en las preguntas abiertas
(ver docs/rules-parametros.md#r-99); esta
prohibido implementar contra ello (regla de oro 9).

## R-01 Sistema de coordenadas {#r-01}

`(0,0)` es la esquina **inferior izquierda**. `up` suma a `y`, `down` resta, `right` suma a `x`.

| Movimiento | Efecto | Cita |
|---|---|---|
| `up` | `y + 1` | `standard.go:68-70` |
| `down` | `y - 1` | `standard.go:71-73` |
| `left` | `x - 1` | `standard.go:74-76` |
| `right` | `x + 1` | `standard.go:77-79` |

Fuera de tablero es `x < 0 || x >= width || y < 0 || y >= height`, evaluado sobre **todos** los
segmentos del cuerpo, no solo la cabeza (`standard.go:298-308`).

## R-02 Orden exacto de stages por turno {#r-02}

Royale (`royale.go:7-15`), con el primer stage sustituido por `NamedRuleset` (`ruleset.go:78-99`):

| # | Stage | Funcion | Cita |
|---|---|---|---|
| 1 | `game_over.standard` | `GameOverStandard` | `ruleset.go:79-83`, `standard.go:384-392` |
| 2 | `movement.standard` | `MoveSnakesStandard` | `standard.go:17-88` |
| 3 | `starvation.standard` | `ReduceSnakeHealthStandard` | `standard.go:118-128` |
| 4 | `hazard_damage.standard` | `DamageHazardsStandard` | `standard.go:130-170` |
| 5 | `feed_snakes.standard` | `FeedSnakesStandard` | `standard.go:329-354` |
| 6 | `elimination.standard` | `EliminateSnakesStandard` | `standard.go:172-292` |
| 7 | `spawn_hazards.shrink_map` | `PopulateHazardsRoyale` | `royale.go:17-62` |

El pipeline **para en el primer stage que devuelve `ended` o error** (`pipeline.go:192-200`), asi
que con una serpiente viva o menos el turno no se ejecuta: `game_over` corta antes del movimiento.

Ademas del pipeline, el arbitro llama a los hooks del mapa: `SetupBoard` antes del turno 0,
`PreUpdateBoard` antes de pedir movimientos, y `PostUpdateBoard` **despues** de `Execute`
(`cli/commands/play.go:353,378,421,427`). Con `-g royale -m royale` el hazard se calcula **dos
veces** --stage 7 y `maps/royale.go:40-88`-- con el mismo turno y la misma semilla, luego el
resultado es identico; el spawn de comida solo ocurre en el hook del mapa
(`maps/royale.go:42`, `maps/standard.go:64-73`).

El contador de turno **no lo toca el ruleset**: lo incrementa el arbitro despues de los hooks
(`cli/commands/play.go:432`). Nuestro `apply()` debe incrementarlo el mismo.

## R-03 Movimiento y movimiento por defecto {#r-03}

Todas las serpientes vivas se mueven **simultaneamente**: se antepone la nueva cabeza y se descarta
el ultimo segmento (`standard.go:82-83`).

Si la direccion recibida no es una de las cuatro literales (`up`, `down`, `left`, `right`), el motor
aplica `getDefaultMove` (`standard.go:58-63`), que deriva la direccion **de la cabeza respecto del
cuello** --es decir, repite el ultimo movimiento-- y cae a `up` si no puede (`standard.go:90-116`).
Una respuesta ausente por timeout llega al motor como cadena vacia y entra por esa misma rama.

`apply()` de nuestro motor **no filtra** direcciones: acepta la inmediatamente mortal y reproduce
`getDefaultMove`. Sin eso, el test diferencial de la Fase 1 no puede replicar los logs.

## R-04 Cola: cuando la casilla queda libre {#r-04}

La casilla de la cola queda libre **salvo que la serpiente tenga dos segmentos apilados al final**.
Hay tres fuentes de apilamiento, todas verificadas:

1. **Spawn.** Las serpientes nacen con sus `SnakeStartSize = 3` segmentos en la **misma casilla**
   (`board.go:217-223`). La cola no se libera hasta que la serpiente se haya desplegado.
2. **Comer.** `growSnake` duplica el ultimo segmento (`standard.go:361-365`), asi que el turno
   siguiente la cola no avanza.
3. **Constrictor.** `GrowSnakesConstrictor` re-duplica la cola cada turno (`constrictor.go:32-47`):
   la cola no avanza jamas.

Como `feed_snakes` corre **antes** de `elimination` (ver [R-02](#r-02)), una serpiente que come en
el turno T ya tiene el segmento duplicado cuando se evaluan las colisiones de T: seguir su cola ese
turno es mortal.

Corolario para el cerebro: la ocupacion de la cola se deriva de `body[n-1] == body[n-2]` en el ring
buffer, **nunca** de un flag de "comio el turno anterior".

## R-05 Hambre {#r-05}

Menos 1 de salud por turno a toda serpiente viva (`standard.go:118-128`). Se elimina con
`health <= 0` (`standard.go:294-296`), evaluado en el stage de eliminacion (`standard.go:199-202`)
con causa `out-of-health`.

## R-06 Daño de hazard {#r-06}

- Se evalua **solo sobre la cabeza** (`standard.go:140-142`).
- Se resta `hazardDamagePerTurn` y se acota al rango 0..100 (`standard.go:155-161`).
- **Excepcion no obvia:** si hay comida en la casilla de la cabeza, **no se aplica daño alguno**
  (`standard.go:143-152`).
- El bucle recorre `b.Hazards` y aplica el daño **una vez por entrada coincidente**
  (`standard.go:141-166`). El mapa royale nunca genera duplicados (`maps/royale.go:80-86` recorre
  cada casilla una sola vez), asi que **en Royale el daño es plano por casilla**; en otros mapas se
  acumularia. Por eso el motor modela hazards como bitboard sin contador.
- Si el daño deja la salud en 0, la eliminacion es inmediata con causa `hazard`
  (`standard.go:162-164`).

## R-07 Alimentacion {#r-07}

`feedSnake` pone la salud a `SnakeMaxHealth = 100` y hace crecer a la serpiente
(`standard.go:356-359`). El crecimiento duplica el ultimo segmento (`standard.go:361-365`), asi que
la longitud sube en 1 y la casilla de la cola queda ocupada por dos segmentos (ver [R-04](#r-04)).

Varias serpientes pueden comer la misma casilla el mismo turno: el bucle no corta tras la primera
(`standard.go:333-345`). La comida se retira del tablero si la comio alguien
(`standard.go:346-352`).

Combinado con [R-06](#r-06): comer dentro de un hazard **no** cuesta salud y ademas la restaura a
100, porque el daño se salta por la comida y el stage de alimentacion va despues.

## R-08 Eliminacion, colisiones y cabeza a cabeza {#r-08}

Orden dentro del stage (`standard.go:172-292`):

1. Se ordenan indices por longitud descendente, solo para atribuir la causa
   (`standard.go:178-186`).
2. **Se aplican ya**: hambre (`standard.go:199-202`) y fuera de tablero (`standard.go:204-207`).
3. **Se recolectan sin aplicar**: autocolision (`standard.go:228-235`), colision con cuerpo rival
   (`standard.go:238-256`) y cabeza a cabeza (`standard.go:259-277`). Cada serpiente se elimina por
   la **primera** causa que encuentra en ese orden.
4. Se aplican todas las colisiones recolectadas (`standard.go:280-289`).

Consecuencias:

- Una serpiente eliminada **por colision** este turno **sigue bloqueando** a las demas este turno,
  porque su eliminacion se aplica al final.
- Una serpiente eliminada **por hambre, por salir del tablero o por hazard** este turno
  **deja de bloquear**: la de hazard muere ya en el stage 4 (`standard.go:162-164`) y las
  otras dos en el paso 2, y todos los bucles de colision saltan a las eliminadas
  (`standard.go:241-243`, `standard.go:262-264`).
- **Cuello:** no es una regla especial. `snakeHasBodyCollided` salta el indice 0 y compara contra
  todos los demas segmentos (`standard.go:310-320`); el cuello es uno de ellos.
- **Cabeza a cabeza:** pierde la serpiente con longitud **menor o igual** (`standard.go:322-327`).
  Si son iguales, ambas cumplen la condicion y mueren las dos.
- La cabeza a cabeza se evalua **despues** de la colision con cuerpo: si la cabeza de A cae sobre el
  cuerpo de B y a la vez sobre la cabeza de B, se clasifica como `snake-collision`.

`EliminateSnake` registra causa, culpable y turno (`board.go:595-599`).

## R-09 Hazards de Royale: rectangulo que se encoge {#r-09}

`maps/royale.go:40-88`, identico a `royale.go:17-62`:

- Antes del turno `shrinkEveryNTurns` no hay hazard alguno (`maps/royale.go:54-56`).
- Cada turno se **borran todos los hazards y se regeneran desde cero** (`maps/royale.go:59`).
- `numShrinks = turn / shrinkEveryNTurns`, con `turn = estado.Turn + 1` (`maps/royale.go:47,64`).
- El generador se re-siembra **siempre al turno 0**: `settings.GetRand(0)` (`maps/royale.go:62`),
  que devuelve `NewSeedRand(seed + 0)` (`settings.go:41-52`). El bucle consume `numShrinks` valores
  de `Intn(4)` de esa misma secuencia (`maps/royale.go:67-78`).
- Cada shrink mueve **un solo borde** en 1: `minX+1`, `maxX-1`, `minY+1` o `maxY-1`
  (`maps/royale.go:69-77`). El lado se repite si sale repetido.
- El hazard es el **complemento del rectangulo** que va de `minX,minY` a `maxX,maxY`
  (`maps/royale.go:80-86`).

Consecuencias para la estrategia:

- La secuencia de lados queda **fijada por la semilla al empezar la partida** y no depende de lo que
  ocurra en ella. Es determinista, pero la semilla **no viaja en el payload de `/move`**
  (`client/models.go:12-19`): en partida real el lado del proximo shrink **no es conocible**.
- El rectangulo es **monotono y anidado**: nunca crece. Los cuatro rectangulos posibles tras el
  proximo shrink se derivan del actual, y eso es lo que modela `pessimistic_edges`.
- El default de `shrinkEveryNTurns` **difiere entre motor y arbitro**: 20 en `maps/royale.go:49`,
  25 en `cli/commands/play.go:117`. Por eso el parametro se lee del request y el fallback emite
  `WARN` (regla de oro 5).

## R-10 Spawn de comida {#r-10}

Lo hace el hook del mapa, no el pipeline (`maps/standard.go:64-73`):

- Si hay menos comida que `minimumFood`, se colocan las que faltan (`maps/standard.go:80-82`).
- Si no, con probabilidad `foodSpawnChance` sobre 100 se coloca **una**
  (`maps/standard.go:83-85`). La comparacion literal es `(100 - rand.Intn(100)) < foodSpawnChance`.
- Se coloca en casillas desocupadas barajadas (`maps/standard.go:90-106`), usando
  `GetUnoccupiedPoints(b, false, false)` (`board.go:522`), que **no** excluye hazards.
- El RNG del spawn de comida se siembra por turno: `settings.GetRand(lastBoardState.Turn)`
  (`maps/standard.go:65`), a diferencia del shrink, que siempre usa el turno 0.

En busqueda, el spawn de comida se ignora (ver [invariants.md#inv-09](invariants.md#inv-09)).

## R-11 Colocacion inicial {#r-11}

- Hasta 8 serpientes en tablero cuadrado de lado 7 o mayor: posiciones **fijas**, 4 esquinas y 4
  puntos cardinales a distancia 1 del borde, barajadas (`board.go:170-216`).
- Los 3 segmentos iniciales se apilan en la misma casilla (`board.go:217-223`), con salud 100
  (`board.go:174-177`).
- La comida inicial se coloca a distancia diagonal 1 de cada cabeza, nunca en el centro ni en una
  esquina, mas una en el centro (`board.go:378-440`).

## R-12 Fin de partida y placements {#r-12}

`GameOverStandard` devuelve fin cuando queda **una serpiente viva o ninguna**
(`standard.go:384-392`), evaluado al **principio** del turno (ver [R-02](#r-02)).

El motor Go **no expone placements**. El arbitro solo emite ganador y empate
(`cli/commands/output.go:18-22,54-58`), y el JSONL **no incluye** `EliminatedOnTurn`
(`client/models.go:33-45`), aunque el estado interno si lo guarde (`board.go:43`,
`board.go:595-599`).

Por tanto: el orden final se deriva del turno en que cada serpiente desaparece del JSONL, y las que
mueren en el mismo turno **no tienen orden asignado por el motor**. Nuestro `placements()` usa rango
compartido promediado y tiene prohibido desempatar por indice o asiento (ver
[invariants.md#inv-07](invariants.md#inv-07)).

## R-30 Donde estan los parametros y las variantes {#r-30}

Las variantes no Royale, la tabla de rutas JSON del request, la tabla de constantes
del motor y las preguntas abiertas viven en `docs/rules-parametros.md`, para que este
archivo quepa en el presupuesto por tarea del pack de reglas
(ver docs/INDEX.md#i-02).

- Variantes: ver docs/rules-parametros.md#r-13
- Parametros del request: ver docs/rules-parametros.md#r-20
- Constantes del motor: ver docs/rules-parametros.md#r-21
- Preguntas abiertas: ver docs/rules-parametros.md#r-99
