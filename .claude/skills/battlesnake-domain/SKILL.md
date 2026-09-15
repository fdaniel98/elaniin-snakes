---
name: battlesnake-domain
description: Conocimiento tactico de Battlesnake condensado. Usar al escribir o revisar heuristicas del cerebro, al analizar una derrota, al crear un fixture, y siempre que alguien razone sobre colas, cabezazos o espacio.
---

# Tactica de Battlesnake

Las mecanicas se citan, no se enuncian: la fuente es `docs/rules.md`.

## Cola apilada (el error mas caro)

La casilla de cola solo se libera si los dos ultimos segmentos **no** estan apilados
(ver docs/rules.md#r-04). Se apilan al nacer, al comer y siempre en `constrictor`.

- Contraejemplo: "la cola siempre se libera, asi que puedo seguirla". En el turno 0 los
  tres segmentos estan en la misma casilla y no se libera nada.
- Contraejemplo: "comio el turno pasado, luego su cola esta apilada". Deriva del cuerpo
  (`body[n-1] == body[n-2]`), no de un flag: en `constrictor` no comio nadie y la cola no
  avanza jamas.

## Zona de cabeza

Las casillas adyacentes a una cabeza rival son apuestas: si ambos entran, gana la mas
larga y con longitudes iguales mueren las dos (ver docs/rules.md#r-08).

- Contra una **igual o mas larga**: evitar.
- Contra una estrictamente **mas corta**: preferir, es como se mata.
- Con 3 o 4 vivas, forzar un cabezazo que te deja con 1 de salud suele regalar la partida
  a la tercera. El kingmaker existe.

## Espacio antes que comida

Entrar en una region mas pequeña que tu longitud es morir con retraso. El flood fill manda
sobre cualquier otra consideracion, salvo hambre terminal.

- Contraejemplo: "hay comida, luego voy". Con salud alta, la comida en un callejon es una
  trampa; ademas crecer reduce tu propio espacio.
- Paridad: en pasillos estrechos, quien entra primero decide quien sale.

## Puntos de articulacion

Una casilla cuya ocupacion parte una region en dos. Entrar en el lado pequeño es perder.
Detectarlos es la mejora principal de v1 (ver docs/strategy.md#s-v1).

## Hazards de royale

El rectangulo seguro solo encoge, y el lado del proximo shrink **no es conocible** en
partida real (ver docs/rules.md#r-09). Modelar los 4 bordes como equiprobables es lo
correcto; predecir el siguiente es inventar informacion.

Comer dentro de un hazard es doblemente bueno: cancela el daño de esa casilla y restaura
la salud a 100 (ver docs/rules.md#r-06).

## Resolucion simultanea

Todos se mueven a la vez. Razonar "yo me muevo y luego el rival responde" sobreestima tu
seguridad: cuando el rival elige a la vez, tu mejor respuesta puede ser la peor.
