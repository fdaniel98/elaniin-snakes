---
title: Roadmap de estrategia v0 a v5
read_when: "al proponer una version nueva del cerebro o al discutir que medir"
authority: speculative
last_verified: 2026-09-15
size_bytes: 9909
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

60 partidas contra `gauntlet-v1` (`snake/config/v1.json`), 15 bloques pareados contra v0:

| | v0 | v1 |
|---|---|---|
| puesto medio | 2.767 | 2.817 |
| turnos vividos | 119.2 | 136.3 |

Diferencia pareada **+0.0500**, dentro del ruido. **NO ENTRA.** El territorio hace algo
-aguantamos 17 turnos mas- pero aguantar no adelanta a nadie.

**La leccion, que es el motivo de conservarlo:** contra un rival que busca, una evaluacion
estatica mejor no basta. Da igual lo fina que sea la heuristica si el otro simula tres
turnos y nosotros cero. El codigo se conserva y es seleccionable
(`territory.version = 1`): con busqueda, una evaluacion mejor en las HOJAS si deberia
notarse, y entonces esta medicion es la linea base.

### S-CUELLOS Hipotesis: no entrar donde solo hay una puerta {#s-cuellos}

**Hipotesis falsable:** si 132 de 178 muertes nuestras no tenian ninguna salida ese turno,
no morimos por elegir mal el turno que morimos: entramos en regiones que el rival cierra
despues. Penalizar el movimiento cuyo espacio se desploma al tapar UNA casilla deberia
subir el puesto medio contra `gauntlet-v1`.

**Que se mide:** `snake/config/cuellos.json`, que es v0 con `space.worst_case_weight` a
150 y `territory.version` en **0**. Una sola variable. `v2.json` -cuellos MAS el territorio
de v1- no se mide todavia a proposito: v1 ya se rechazo (ver docs/strategy.md#s-v1r) y
medir los dos juntos no diria cual de los dos hizo que.

**Protocolo:** 60 partidas, `--seed-base 1`, o sea las semillas 1..15 con rotacion de los
4 asientos. Son los MISMOS 15 bloques que las primeras 60 partidas de `torneo-v1`, asi que
la comparacion es pareada por bloque (ver docs/results/torneo-v1/). Metrica primaria
unica: diferencia pareada de puesto medio por bloque. Lo demas es descriptivo y va sin
p-valores.

**Lo que este tamaño puede y no puede ver:** 15 bloques solo resuelven un efecto grande.
La medicion de v1 enseño que 0.05 de puesto medio pide cientos de bloques; este run no
pretende resolver 0.05, pretende ver si los cuellos mueven la aguja de forma visible. Un
resultado dentro del ruido **no** significa que la idea sea mala, significa que no cabe en
el presupuesto de tiempo de esta semana y que la decision se toma con busqueda, no con mas
partidas de heuristica.

### S-CUELLOS-R Resultado: NO ENTRA, y el motivo no es el que parecia {#s-cuellos-r}

60 partidas, 15 bloques pareados contra v0:

| | v0 | cuellos |
|---|---|---|
| puesto medio | 2.767 | 2.842 |
| turnos vividos | 119.2 | 120.1 |

Diferencia pareada **+0.0750**, IC95 [-0.169, +0.319]. **NO ENTRA.**

Los turnos casi no se movieron (+0.9 frente a los +17 de v1), lo que admitia dos lecturas
opuestas. Se midio con `tools/sonda_cuellos.cpp`:

    estados=20000  con cuello=18574 (92.9%)  perdida media=13.11 casillas

**Se dispara en el 92.9% de los estados.** En 11x11 con cuatro serpientes, «existe una
casilla cuyo cierre encoge mi region» es cierto casi siempre, asi que la penalizacion
entra como ruido sumado al espacio y no separa a los candidatos. Una heuristica que se
activa el 93% de las veces no es una heuristica, es una constante con varianza. Un detector
de trampas tiene que ser *relativo* o estar *umbralado*, y ninguna de las dos formas se ha
probado. Codigo conservado y apagado (`space.worst_case_weight = 0.0`).

### S-BUSQ Busqueda paranoica: la primera vez que miramos hacia delante {#s-busq}

**El numero que la justifica:** `brain_v0` decide en ~100 us sobre 350 ms. Gasta el
**0.03%** del presupuesto. Las dos heuristicas que probamos -Voronoi y cuellos- dieron NO
CONCLUYENTE contra rivales que simulan. Lo que falta no es evaluacion, es profundidad.

**Que es:** busqueda paranoica con profundizacion iterativa sobre la evaluacion de v0.
Detalle y sesgos declarados en ver docs/decisions/ADR-0022-busqueda-paranoica.md.

**Capacidad medida** (`tools/sonda_busqueda.cpp`, 15 fixtures, 350 ms):

| | |
|---|---|
| profundidad media | 7.7 |
| profundidad minima | 4 (spawn con 4 serpientes, ~600 000 nodos) |
| tiempo medio / peor | 41.9 ms / 348 ms |

Compara eso con el turno unico de v0. Es la diferencia entre ver el movimiento y ver la
partida.

**Hipotesis falsable:** la busqueda sube el puesto medio contra `gauntlet-v1` por encima
del delta de 0.10, medido con el mismo protocolo pareado de
ver docs/strategy.md#s-cuellos-r.

**Estado: NO MEDIDA EN PARTIDA.** Pasa los 98 tests, respeta el deadline a presupuestos de
1 a 350 ms y nunca devuelve un movimiento ilegal, pero eso solo dice que no rompe nada.
Hasta que gane su A/B, `search.version` sigue en **0** en `default.json` y la snake que se
despliega es v0. Se enciende con `snake/config/v3-busqueda.json`.

Si tambien sale NO CONCLUYENTE, lo que falla no es la profundidad sino la evaluacion en
las hojas, y eso cambia por completo donde hay que mirar despues.

### S-BUSQ-R Resultado: la primera que mueve la aguja, pero NO CONCLUYENTE {#s-busq-r}

Medido, 60 partidas contra `gauntlet-v1` con `snake/config/v3-busqueda.json`, pareadas por
los mismos 15 bloques que v0:

| | v0 | v3 |
|---|---|---|
| puesto medio | 2.767 | **2.500** |
| victorias | 2 de 60 | **7 de 60** |
| ultimos puestos | 4 | **2** |
| turnos vividos | 119.2 | **170.5** (+43 %) |

Diferencia pareada **-0.267** (negativo = mejor), IC95 **[-0.562, +0.029]**. El efecto es
**2.7 veces el delta declarado** de 0.10 y el intervalo se queda a **0.029** de no cruzar
el cero. De 15 bloques: 7 a favor, 3 en contra, 5 empatados.
Veredicto formal: **NO CONCLUYENTE**.

**El sesgo corre EN CONTRA de v3, no a favor.** Fallos de rival por partida en esas mismas
60 partidas:

| corrida | fallos de rival / partida |
|---|---|
| `torneo-v1` (base de v0) | **2.92** |
| `torneo-v3` | **0.88** |

v0 jugo contra un campo 3.3 veces mas averiado, y un rival que no contesta recibe el
movimiento por defecto del arbitro y suele morir: puestos regalados. Asi que el 2.767 de
v0 esta inflado y **-0.267 es una cota inferior** de la mejora real.

**Lo que decide el protocolo (§10.4):** agotado el presupuesto sin cruzar frontera, no se
relanza con otras semillas; se amplia el mismo run. Quedan pendientes los bloques 16-25.

**Lo que si esta decidido:** v3 no es peor, sobrevive un 43 % mas, triplica las victorias
y reduce los ultimos puestos a la mitad, contra un campo mas duro. Es la primera de las
tres hipotesis que mueve algo, y confirma el diagnostico de
ver docs/strategy.md#s-v1r: lo que faltaba era profundidad, no evaluacion.

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
