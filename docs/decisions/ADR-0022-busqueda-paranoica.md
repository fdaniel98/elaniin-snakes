---
title: La busqueda mira hacia delante, y supone lo peor de los rivales
read_when: "antes de tocar snake/src/search.cpp, los parametros de search, o de discutir por que la snake no busca mas hondo"
authority: derived
source: medicion propia (tools/sonda_busqueda.cpp) + docs/strategy.md#s-v1r y #s-cuellos-r
last_verified: 2026-09-18
size_bytes: 4469
---

# ADR-0022 — La busqueda mira hacia delante, y supone lo peor de los rivales {#adr-0022}

## Contexto {#adr-0022-contexto}

`brain_v0` decide en **~100 us** sobre un presupuesto de **350 ms**: gasta el **0.03%**
del tiempo que tiene. Mientras tanto, dos intentos de mejorar la EVALUACION dieron el
mismo resultado contra el mismo campo:

| version | que cambiaba | veredicto |
|---|---|---|
| v1 | territorio por Voronoi | NO CONCLUYENTE (ver docs/strategy.md#s-v1r) |
| cuellos | salas de una sola puerta | NO CONCLUYENTE (ver docs/strategy.md#s-cuellos-r) |

Dos heuristicas estaticas distintas, atacando el sintoma medido -132 de 178 muertes sin
ninguna salida ese turno- y ninguna mueve el puesto medio contra rivales que simulan. Lo
que falta no es evaluacion: es profundidad.

## Decision {#adr-0022-decision}

Busqueda **paranoica** con **profundizacion iterativa**, sobre la evaluacion de v0.

- **Paranoica:** elegimos el movimiento que maximiza nuestra evaluacion suponiendo que los
  rivales eligen a la vez lo que mas nos perjudica.
- **Iterativa:** profundidad 1, 2, 3... y se devuelve la mejor jugada de la ultima
  profundidad **COMPLETADA**. Una profundidad a medias no sustituye a la anterior porque
  sus hermanos no se compararon con las mismas reglas.
- **Rivales por cercania:** solo se simulan los `search.max_rivals` mas cercanos (2 por
  defecto); los demas repiten el movimiento por defecto del motor. Cada rival simulado
  multiplica el arbol por ~3, asi que este recorte es lo que compra profundidad.
- **Reserva de tiempo:** la busqueda corre contra un deadline propio, `search.reserve_us`
  antes del real. Detectar que se acabo el tiempo cuesta tiempo, y sin reserva la sonda se
  pasaba entre 6 y 34 us. INV-11 dice que no se excede el deadline, no que se excede poco.

Medido con `tools/sonda_busqueda.cpp` sobre los 15 fixtures, presupuesto 350 ms:

    profundidad media 7.7, minima 4 (la posicion de spawn con 4 serpientes)
    41.9 ms medios, 348 ms el peor — dentro del presupuesto
    ~64 000 nodos por movimiento

## El sesgo, declarado {#adr-0022-sesgo}

Battlesnake es de movimientos **simultaneos** y esto no lo modela: nosotros elegimos
primero y los rivales responden **viendo** nuestra eleccion. Les regalamos informacion que
en la partida real no tienen, asi que la busqueda es mas **cobarde** que la realidad —
nunca mas temeraria—. Es un sesgo conservador y por eso es aceptable como primer paso; el
roadmap (ver docs/strategy.md#s-v2) pide SM-MCTS/DUCT, que si lo modela.

El segundo sesgo es la paranoia: los tres rivales no coordinan contra nosotros. En un
juego de cuatro hay efectos kingmaker, y suponer coalicion nos hara rechazar lineas que en
la practica son buenas.

## Alternativas descartadas {#adr-0022-alternativas}

- **SM-MCTS/DUCT.** Es lo correcto para movimiento simultaneo y es lo que pide el roadmap.
  Se descarta AHORA por tiempo -el torneo es en dias- y porque necesita bastantes mas
  partidas para ajustar sus constantes. Esta busqueda se queda como linea base contra la
  que medirlo.
- **Seguir afinando la evaluacion.** Es lo que ya se hizo dos veces, con dos NO
  CONCLUYENTE. La tercera no iba a ser distinta.
- **Presupuesto por nodos en vez de por reloj.** Es lo correcto para el A/B reproducible y
  lo pide la fase 4 para la arena; en el servidor manda el reloj porque lo que el arbitro
  corta es tiempo, no nodos.
- **Buscar tambien en modo degradado.** Simular una variante cuyas reglas el motor no
  reproduce da un arbol de posiciones que no van a ocurrir: peor que no mirar.

## Lo que queda por comprobar {#adr-0022-abierto}

**Esto no esta medido en partida.** El A/B contra `gauntlet-v1` con el protocolo de
ver docs/strategy.md#s-cuellos-r es lo unico que decide si entra, y hasta entonces
`search.version` sigue en 0 en `default.json`. La hipotesis falsable es: **la busqueda
sube el puesto medio contra el campo congelado por encima del delta de 0.10.** Si tambien
sale NO CONCLUYENTE, lo que falla no es la profundidad sino la evaluacion en las hojas, y
eso cambia por completo donde hay que mirar despues.
