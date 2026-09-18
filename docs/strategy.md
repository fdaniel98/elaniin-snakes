---
title: Roadmap de estrategia v0 a v5
read_when: "al proponer una version nueva del cerebro o al discutir que medir"
authority: speculative
last_verified: 2026-09-15
size_bytes: 4811
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

### S-V1R Resultado: NO ENTRA {#s-v1r}

Medido, 60 partidas contra `gauntlet-v1` (`snake/config/v1.json`, solo la parte de
Voronoi; los puntos de articulacion quedaron en `v2.json` y no llegaron a medirse):

| | v0 (200 partidas) | v1 (60 partidas) |
|---|---|---|
| puesto medio | 2.770 | **2.817** |
| turnos vividos | 119.2 | **136.3** |

La hipotesis era que el territorio subiria la posicion media. **No la sube.** La
diferencia de 0.05 esta muy por debajo del ruido de la propia corrida: su primera mitad
promedia 2.683 y la segunda 2.950. Se corto en 60 partidas a proposito, porque distinguir
0.05 con ese ruido pide cientos de bloques y no 140 partidas mas.

Lo que si cambia es la supervivencia: **17 turnos mas de media**. Asi que el territorio
hace algo -aguantamos mas- pero aguantar no adelanta a nadie: Hobbs y Devin viven 185 y
189 turnos y nos entierran igual.

**Lo que este resultado enseña, y es el motivo de conservarlo:** contra un rival que
busca, una evaluacion estatica mejor no basta. Da igual lo fina que sea la heuristica si
el otro simula tres turnos y nosotros cero. Seguir afinando pesos aqui es trabajo que se
siente productivo y no mueve el marcador.

El codigo se conserva entero y seleccionable (`territory.version = 1`): cuando exista
busqueda, una evaluacion mejor en las hojas si deberia notarse, y entonces esta medicion
es la linea base contra la que comparar.

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
