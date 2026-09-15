---
title: Roadmap de estrategia v0 a v5
read_when: "al proponer una version nueva del cerebro o al discutir que medir"
authority: speculative
last_verified: 2026-09-15
size_bytes: 3408
---

Cada version entra **solo** si gana su A/B contra el campo congelado y no aumenta los
timeouts. Todo lo de aqui son hipotesis falsables, no hechos.

## S-V0 Baseline {#s-v0}

Implementado. Referencia fija contra la que se mide todo lo demas; **no se borra nunca**.

Filtro duro con la cola derivada de segmentos apilados (ver docs/rules.md#r-04), zona de
cabeza, flood fill con tail-escape, rechazo de huecos menores que la longitud propia,
comida solo por hambre o gratis, penalizacion de hazard y fail-safe de cuatro escalones.

**Debilidad conocida:** el flood fill no modela el tiempo. Cuenta como alcanzable la
casilla de la cola propia y lo que hay detras, aunque llegar alli lleve mas turnos de los
que tarda en liberarse. Es la primera hipotesis a atacar en v1.

## S-V1 Evaluacion {#s-v1}

**Hipotesis:** sustituir "espacio alcanzable" por control de territorio (Voronoi por BFS
simultaneo desde todas las cabezas, con hazards ponderados a la baja) sube la posicion
media contra el campo congelado.

Incluye salud propia y rival, diferencia de longitud, turnos hasta el proximo shrink,
penalizacion por proximidad a **cualquiera** de los 4 bordes candidatos (el lado no es
predecible, ver docs/rules.md#r-09) y deteccion de puntos de articulacion.

**Como se falsa:** A/B con SPRT contra `gauntlet-v1`. Si el veredicto es `NO CONCLUYENTE`
al agotar `--max-games`, no entra.

## S-V2 Busqueda multijugador {#s-v2}

**Hipotesis:** con 3 o 4 serpientes vivas, una busqueda de movimientos simultaneos
(SM-MCTS con seleccion desacoplada por serpiente, o MaxN con resolucion simultanea
explicita) bate a la evaluacion directa de v1.

El juego es de movimientos **simultaneos**: razonar por turnos alternos introduce un
sesgo que hay que documentar en un ADR junto con la alternativa descartada.

## S-V3 Final de dos {#s-v3}

**Hipotesis:** con 2 vivas, alpha-beta con profundizacion iterativa, tabla de
transposicion Zobrist y move ordering (killer y history) gana mas que seguir con MCTS.

Supuesto a mitigar: turnos alternos. Mitigacion propuesta: evaluacion paranoica en la
raiz.

## S-V4 Paralelismo {#s-v4}

**Hipotesis:** root o tree parallelism con N hilos mejora la fuerza sin empeorar el p99.
Se mide el escalado **real**, no el teorico, y su efecto sobre la latencia.

## S-V5 Tuning automatico {#s-v5}

**Hipotesis:** SPSA sobre los parametros de `snake/config/default.json` encuentra una
configuracion mejor que la escrita a mano. Solo tiene sentido cuando la arena de miles de
partidas por minuto.

## S-AB Protocolo de aceptacion {#s-ab}

Unidad de analisis: el bloque (una semilla por una rotacion de asientos), nunca la
partida. Metrica primaria **unica**: diferencia pareada de posicion media por bloque; el
resto se reporta sin p-valores ni veredicto. A y B nunca juegan la misma partida, sino
partidas espejo con identica composicion de rivales y la misma semilla.

Veredicto: `MEJORA`, `EMPEORA` o `NO CONCLUYENTE`, mas el numero de bloques consumidos.
Agotado `--max-games` sin cruzar frontera, el A/B no se relanza con otras semillas: solo
se amplia `--max-games` del mismo run.

## S-FUERA Fuera de alcance {#s-fuera}

NNUE: sin pipeline de datos y sin v3 estable, es una idea, no un plan.
