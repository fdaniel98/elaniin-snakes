---
title: El schedule de la arena se indexa por turno, no se materializa
read_when: "antes de tocar spawn_food, royale_hazards o el bucle de partida de la arena"
authority: derived
source: BattlesnakeOfficial/rules@87e094e2e1c224e9dea67743fd3c2249137c4057
last_verified: 2026-09-19
size_bytes: 3836
---

# ADR-0029 — El schedule se indexa por turno, no se materializa {#adr-0029}

## D-0290 Contexto {#d-0290}

El protocolo pareado exige que dos ramas de un A/B reciban **el mismo sorteo de comida y
hazards** aunque sus partidas hayan divergido (ver docs/experimentos.md#exp-resumen). El prompt
maestro lo formula como «el schedule se genera antes de la partida a partir de la semilla
e indexado por turno, no bajo demanda», y `arena/README.md` lo escribio como «el sorteo se
resuelve entero por adelantado».

**Resolverlo entero por adelantado es imposible**, y conviene decir por que antes de
intentarlo: el sorteo de comida elige entre las casillas **desocupadas**, y cuales estan
desocupadas depende de donde se hayan movido las serpientes. No es azar, es la partida.
Materializar «en el turno 31 la comida va en la casilla 47» produciria, en la rama donde
la casilla 47 tiene un cuerpo encima, o una comida ilegal o un salto que desalinea las dos
ramas justo donde importa.

## D-0291 Decision {#d-0291}

Lo que hay que fijar por adelantado no son las casillas: son **los numeros aleatorios**.

`spawn_food()` construye un `Rng` local sembrado con `semilla + turno` y lo tira al salir.
`royale_hazards()` ya hacia lo mismo con la semilla sola. Con eso, el sorteo del turno *t*
no depende de cuanto azar se haya consumido en los turnos anteriores, que es exactamente
la propiedad que el protocolo pide: **numeros aleatorios comunes indexados por turno**.

No se materializa nada. Un tablero de 11x11 y 2 000 turnos habria costado ~0.5 MB por
partida en permutaciones que ademas habria que recortar contra la ocupacion real.

Esto no es una desviacion de la fuente sino un calco: el motor oficial siembra su
generador **por turno** para la comida, `settings.GetRand(lastBoardState.Turn)`
(`maps/standard.go:65`), y siempre al turno 0 para el shrink (`maps/royale.go:62`).
La propiedad que necesita la arena ya estaba en el diseño del juego.

Lo tapa un test: «el sorteo depende del turno, no de la historia» mete mil sorteos de
otros turnos entre dos llamadas del turno 50 y exige el mismo resultado
(`tests/test_rules.cpp`). Si alguien cambia la siembra por un `Rng` conservado entre
llamadas, ese caso cae.

## D-0292 Lo que esta decision NO cubre {#d-0292}

Dos ramas ven el mismo **sorteo**, no el mismo **tablero**: en cuanto divergen, la comida
cae en casillas distintas porque las libres son otras. La reduccion de varianza que esto
compra es real pero parcial, y el veredicto sigue saliendo de la diferencia pareada por
bloque, no de comparar partidas casilla por casilla.

La secuencia sigue siendo la del `Rng` del repo y no la del `math/rand` de Go
(ver docs/decisions/ADR-0010-rng-del-shrink.md#d-0091): coincide la forma de la regla, no
que casilla toca. Una partida de la arena **no** reproduce una partida oficial.

## D-0293 Alternativas descartadas {#d-0293}

| Alternativa | Por que no |
|---|---|
| Materializar casillas por turno | No se puede: dependen de la ocupacion, que depende de la partida |
| Materializar una permutacion de casillas por turno y recortarla contra la ocupacion | Equivalente al resultado de sembrar por turno, con ~0.5 MB por partida y un buffer que mantener |
| Un solo `Rng` por partida, consumido segun hace falta | Rompe el pareado: en cuanto una rama repone comida una vez mas que la otra, los streams se desplazan y las semillas comunes dejan de reducir varianza |

## D-0294 Estado {#d-0294}

**ACEPTADA.** Implementada en `spawn_food()` (`engine/src/royale_map.cpp`).
