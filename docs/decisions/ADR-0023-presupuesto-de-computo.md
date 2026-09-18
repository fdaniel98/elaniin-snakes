---
title: El presupuesto de computo baja a 200 ms, porque los ultimos 150 no compran nada
read_when: "antes de tocar time.max_compute_ms, los margenes de red o seguridad, o de discutir timeouts"
authority: derived
source: tools/sonda_busqueda.cpp + docs/results/torneo-v3 (60 partidas, 10 221 movimientos)
last_verified: 2026-09-18
size_bytes: 4357
---

# ADR-0023 — El presupuesto de computo baja a 200 ms {#adr-0023}

## Contexto {#adr-0023-contexto}

Con `brain_v0` los margenes de tiempo eran decorativos: decidia en ~100 us sobre un
presupuesto de 350 ms. Con la busqueda de v3 pasan a ser **carga estructural**, y en el
torneo de 60 partidas se rompieron:

| metrica (nuestra snake, 60 partidas, 10 221 movimientos) | valor |
|---|---|
| p50 | **348 ms** |
| maximo tipico por partida | 376 ms |
| **maximo absoluto** | **751 ms** |
| timeouts | 8 (0.078 %) |
| partidas con un maximo > 480 ms | 7 de 60 |

El p50 de 348 ms dice lo esencial: la busqueda agota el presupuesto en **casi todos** los
movimientos, porque una busqueda anytime se para cuando la para el reloj.

El maximo de 751 ms **no es computo nuestro**: el deadline interno corta a 348 ms y la
sonda lo confirma. Esos 400 ms de mas son un paron del sistema (WSL2 sobre Windows, el
mismo host que dio los 9 ms de arranque en frio de
ver docs/decisions/ADR-0021-arranque-en-frio.md). Bajar el presupuesto no arregla eso.

## Lo que se midio antes de decidir {#adr-0023-medicion}

Los fixtures no sirven para calibrar: 14 de 15 tienen 2 serpientes y acaban la profundidad
8 en menos de 60 ms. La medicion se hizo sobre **40 posiciones de media partida con 4
serpientes vivas**, que es donde el presupuesto se agota:

| presupuesto | profundidad media | profundidad minima |
|---|---|---|
| 350 ms | 6.27 | 5 |
| 300 ms | 6.22 | 5 |
| 250 ms | 6.19 | 5 |
| **200 ms** | **6.08** | **5** |
| 150 ms | 5.89 | 4 |
| 100 ms | 5.62 | 4 |

**De 350 a 200 ms se pierden 0.19 niveles: un 3 %.** Es la aritmetica del arbol
exponencial —cada nivel cuesta unas 3 veces el anterior—, asi que la mitad del presupuesto
estaba comprando el ultimo 3 % de profundidad.

## Decision {#adr-0023-decision}

`time.max_compute_ms` pasa de **350 a 200** en todos los configs.

`network_margin_ms` (100) y `safety_margin_ms` (50) **no se tocan**: se recalibran con el
p99 de RTT real medido contra la URL desplegada, que es la DoD de la fase 7, y no con una
estimacion. El deadline sigue siendo `min(timeout - 100 - 50, max_compute)`, o sea que
sigue por debajo del techo de 350 ms que fija §4 del prompt maestro; esto lo **endurece**,
no lo relaja.

Efecto esperado sobre la exposicion al timeout:

| | antes | despues |
|---|---|---|
| latencia tipica | 376 ms | ~226 ms |
| colchon bajo los 500 ms del arbitro | 124 ms | **274 ms** |

## Lo que NO arregla, dicho claramente {#adr-0023-limites}

El pico de 751 ms pasaria a ~600 ms y **seguiria siendo timeout**. Un paron de 400 ms del
host se come cualquier presupuesto razonable. Lo unico que lo reduce es correr en una
maquina que no se pare: en Cloud Run con `--no-cpu-throttling` y `--concurrency=1` deberia
ser mucho mas raro, pero eso esta por medir.

## Alternativas descartadas {#adr-0023-alternativas}

- **Parar la busqueda cuando el mejor movimiento se estabiliza.** Medido: 94 de 117
  profundidades son confirmatorias y la decision se fija de media en la profundidad 1.5,
  asi que la tentacion es fuerte. Pero en `tests/fixtures/10-trampa-de-espacio.json` el
  mejor movimiento cambia en la profundidad **8**, la ultima. Cortar por estabilidad
  habria acertado en 14 fixtures y fallado justo en el que justifica buscar hondo.
- **250 ms.** Cuesta solo 0.08 niveles, pero deja 224 ms de colchon en vez de 274 y el RTT
  real todavia no esta medido. Con el dato de la fase 7 se puede subir.
- **Dejar 350 y medir el RTT primero.** Si el RTT resulta alto, los primeros partidos
  desplegados dan timeouts, y en un torneo eso se paga con la partida.

## Consecuencia sobre lo medido {#adr-0023-consecuencia}

El torneo de v3 (ver docs/strategy.md#s-busq-r) se jugo a 350 ms. Lo que se despliegue a 200
es un 3 % menos profundo que lo medido. Queda **declarado**, no escondido: si se quiere
rigor completo, el A/B se repite a 200 ms; la diferencia esta por debajo del ruido del
propio A/B, que sobre 15 bloques tiene un IC de +-0.3 de puesto.
