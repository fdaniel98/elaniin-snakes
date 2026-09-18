---
title: "ADR-0016: la reproducibilidad bit a bit se mide en la fase 4, no en la 3"
read_when: "antes de cerrar la fase 3 o de discutir que promete el Training Room"
authority: canonical
last_verified: 2026-09-18
size_bytes: 3001
---


## D-0150 Contexto {#d-0150}

La DoD de la fase 3 pide un torneo de 200 partidas con reporte completo, «reproducible bit
a bit en arena, y en modo HTTP con semilla y JSONL persistidos».

La arena in-process es entregable de la **fase 4**. Hoy `arena/` es un README y un stub.
La fase 3 no la construye, asi que su DoD exige demostrar una propiedad de un componente
que esa misma fase no entrega.

No es una ambiguedad de redaccion: son dos cosas con dueños distintos.

- La **arena** es reproducible porque no hay red, el presupuesto es por nodos y toda
  aleatoriedad recibe un `Rng&` explicito (ver docs/decisions/ADR-0010-rng-del-shrink.md#d-0091).
- El **modo HTTP** no puede serlo. El arbitro oficial usa `math/rand` de Go, que no se
  reproduce desde C++ (ver docs/rules-parametros.md#r-99), y el transporte añade su propio
  ruido: en la fase 2 el maximo que midio el arbitro fueron 169 ms contra 0.388 ms de
  nuestro codigo (ver docs/performance.md#p-07). Lo unico honesto ahi es persistir semilla
  y JSONL, que es lo que la propia DoD dice a continuacion.

## D-0151 Decision {#d-0151}

La fase 3 cierra con reproducibilidad **solo en modo HTTP**: semilla de 64 bits y ruta al
JSONL persistidas por partida, y el JSONL guardado.

La reproducibilidad bit a bit pasa a la DoD de la **fase 4**, junto a la arena que la hace
posible, y alli se comprueba de la unica forma que vale: dos corridas del mismo estado con
la misma semilla y el mismo `budget_nodes` dan el mismo resultado.

Aprobado por el humano el 2026-09-18, enunciada la alternativa de construir una arena
minima ya en la fase 3.

## D-0152 Alternativas descartadas {#d-0152}

| Alternativa | Por que no |
|---|---|
| Arena minima en la fase 3, solo para demostrar reproducibilidad | Adelanta a medias el entregable central de la fase 4. Una arena sin presupuesto por nodos no sirve para A/B (ver docs/strategy.md), asi que habria que rehacerla; y una arena que se rehace no demuestra nada sobre la que se queda |
| Declarar la fase 3 PARCIAL hasta que exista la arena | Honesto y sin ganancia: bloquea la fase 4, que es justo la que construye lo que falta |
| Reinterpretar «bit a bit» como «misma semilla, mismo JSONL» | Es cambiar el significado de una palabra para que un criterio pase. Exactamente lo que la regla de oro 4 prohibe hacerle al gate |

## D-0153 Consecuencias {#d-0153}

- La fase 3 no promete determinismo que no puede dar. Su reporte dice, por partida, que
  semilla se uso y donde esta el JSONL, y nada mas.
- La DoD de la fase 4 gana un criterio: reproducibilidad bit a bit en arena, que se suma a
  los cuatro que ya tiene (A/A, regresion inyectada, pareado, throughput).
- El check 9 del gate no cambia. Esto mueve un criterio de fase, no un umbral del loop.
