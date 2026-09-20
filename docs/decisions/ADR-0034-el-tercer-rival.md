---
title: Simular tambien al tercer rival
read_when: "antes de tocar search.max_rivals o de proponer gastar profundidad en otra cosa"
authority: derived
source: tools/sonda_tope.cpp y docs/experimentos.md
last_verified: 2026-09-20
size_bytes: 3652
---

# ADR-0034 — Simular tambien al tercer rival {#adr-0034}

## D-0340 Contexto {#d-0340}

En una partida de cuatro, la busqueda simula de verdad a los **dos rivales mas cercanos**
(`search.max_rivals = 2`). Al tercero se le aplica el movimiento por defecto del motor
-repetir la direccion deducida de cabeza y cuello, ver docs/rules.md#r-03 -, que no es lo
que va a hacer: es la suposicion mas barata que no lo deja quieto.

Dicho de otra manera: **hay una serpiente en el tablero que la busqueda no ve venir**.
Puede cerrarnos una region o ganarnos un cabeza a cabeza y en el arbol eso no existe.

El recorte se puso porque cada rival simulado multiplica el arbol por ~3, y en su momento
la profundidad parecia el recurso escaso. Despues se midio que no lo era.

## D-0341 La hipotesis, y de donde sale {#d-0341}

Sale de un resultado medido, no de una intuicion: pasar la busqueda de profundidad 6 a
profundidad 12 movio la diferencia pareada **-0.0167**, con IC95 [-0.373, +0.340]
(ver docs/experimentos.md#s-busq-r). Duplicar la profundidad no compro nada. Si un nivel
de profundidad vale tan poco, gastarlo en ver al tercer rival deberia salir a cuenta.

**El coste esta medido** (`tools/sonda_tope.cpp`, 200 ms, 40 posiciones por escenario):

| vivas | `max_rivals` 2 | `max_rivals` 3 | coste |
|---|---:|---:|---|
| 2 | 10.31 | 10.41 | ninguno: no hay tercer rival |
| 3 | 6.08 | 6.08 | ninguno: con 2 rivales ya estaban todos |
| **4** | **10.92** | **9.86** | **1.06 niveles** |

Solo cambia algo con cuatro vivas, que es exactamente donde esta el problema. El precio es
**un nivel**, y un nivel es lo que se midio que no vale casi nada.

## D-0342 Decision {#d-0342}

Se mide. `snake/config/v7-rivales3.json` es v5 con `max_rivals: 3` y **nada mas**; se
enfrenta a v5 con el protocolo de siempre -60 partidas, 15 bloques pareados,
`gauntlet-v1`-. `default.json` no se toca hasta que gane.

## D-0343 Lo que puede salir mal {#d-0343}

Se escribe ANTES de medir, como en ver docs/decisions/ADR-0028-turnos-de-supervivencia.md,
para que el resultado valga salga como salga.

El riesgo no es el nivel de profundidad: es que el modelo **paranoico** se vuelva mas
paranoico. La busqueda supone que los rivales simulados eligen a la vez lo peor para
nosotros; con dos ya es pesimista, y con tres pasa a suponer que las **tres** serpientes
del tablero se coordinan contra nosotros, que es una suposicion mas salvaje todavia. El
sintoma seria una snake mas pasiva: menos primeros puestos y mas segundos, sobreviviendo
mas turnos pero ganando menos.

Si sale `NO CONCLUYENTE` con ese reparto -mas turnos vividos y menos victorias-, la
lectura no es "el tercer rival no importa" sino "el modelo paranoico no escala a tres", y
el siguiente sitio donde mirar seria el modelo de la busqueda (SM-MCTS / DUCT del roadmap)
y no otro termino de la evaluacion.

## D-0344 Alternativas descartadas {#d-0344}

| Alternativa | Por que no |
|---|---|
| Simular a los tres siempre, sin medir | Regla de oro 3: ningun cambio de estrategia entra por intuicion |
| Simular al tercero solo cuando este cerca | Añade un umbral nuevo que tambien habria que afinar, encima de una hipotesis sin verificar |
| Dejarlo como esta | El coste medido es un nivel y el beneficio medido de un nivel es casi cero |

## D-0345 Estado {#d-0345}

**SIN MEDIR.** Se enciende con `snake/config/v7-rivales3.json` y se mide CONTRA v5.
