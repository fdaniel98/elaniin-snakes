---
title: Roadmap de estrategia v0 a v5
read_when: "al proponer una version nueva del cerebro o al discutir que medir"
authority: speculative
last_verified: 2026-09-15
size_bytes: 11050
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

Resultado medido: ver docs/experimentos.md#s-v1r

### S-CUELLOS Hipotesis: no entrar donde solo hay una puerta {#s-cuellos}

**Hipotesis falsable:** si 132 de 178 muertes nuestras no tenian ninguna salida ese turno,
no morimos por elegir mal el turno que morimos: entramos en regiones que el rival cierra
despues. Penalizar el movimiento cuyo espacio se desploma al tapar UNA casilla deberia
subir el puesto medio contra `gauntlet-v1`.

**Que se mide:** `snake/config/cuellos.json`, que es v0 con `space.worst_case_weight` a
150 y `territory.version` en **0**. Una sola variable. `v2.json` -cuellos MAS el territorio
de v1- no se mide todavia a proposito: v1 ya se rechazo (ver docs/experimentos.md#s-v1r) y
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

Resultado medido: ver docs/experimentos.md#s-cuellos-r

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
ver docs/experimentos.md#s-cuellos-r.

**Estado: NO MEDIDA EN PARTIDA.** Pasa los 98 tests, respeta el deadline a presupuestos de
1 a 350 ms y nunca devuelve un movimiento ilegal, pero eso solo dice que no rompe nada.
Hasta que gane su A/B, `search.version` sigue en **0** en `default.json` y la snake que se
despliega es v0. Se enciende con `snake/config/v3-busqueda.json`.

Si tambien sale NO CONCLUYENTE, lo que falla no es la profundidad sino la evaluacion en
las hojas, y eso cambia por completo donde hay que mirar despues.

Resultado medido: ver docs/experimentos.md#s-busq-r

### S-HOJAS Hipotesis: la evaluacion buena va en las hojas {#s-hojas}

Cuatro experimentos dibujan este cuadro, y solo queda una celda:

| | evaluacion de v0 | evaluacion con territorio |
|---|---|---|
| **sin busqueda** | 2.767 (linea base) | v1 y cuellos: **no ayuda** |
| **con busqueda** | -0.28: **si ayuda** | **esto** |

La celda vacia es exactamente lo que el rechazo de v1 predijo: «cuando exista busqueda, una
evaluacion mejor en las HOJAS si deberia notarse» (ver docs/experimentos.md#s-v1r).

**Que es:** `evaluate()` mide el espacio como TERRITORIO -Voronoi por BFS simultaneo, las
casillas que alcanzamos antes que los rivales- en vez de como espacio alcanzable a secas.
El espacio crudo se conserva para la guarda de «no cabe ni mi cuerpo», que es una condicion
sobre casillas fisicas y no sobre quien llega antes.

**Lo que cuesta, medido:** 1.2 niveles de profundidad (12.03 -> 10.89 con 4 vivas, 200 ms).
Es un buen cambio precisamente por el hallazgo de arriba: se paga con profundidad que se
midio que no vale nada.

**Hipotesis falsable:** el territorio en las hojas sube el puesto medio contra
`gauntlet-v1` por encima del delta de 0.10, medido contra la busqueda SIN territorio -no
contra v0-, porque lo que se prueba es la evaluacion, no la busqueda.

**Estado: SIN MEDIR.** Se enciende con `snake/config/v4-hojas.json`.

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

### S-COBRAR Hipotesis: la ventaja de longitud no se cobra {#s-cobrar}

Subir `head.prefer_shorter` de 8 a 40 debia subir los duelos ganados. **Cerrada:** era un termino muerto bajo busqueda paranoica (ver docs/experimentos.md#s-cobrar-r).

### S-DUELO Hipotesis: el final de dos necesita su propia evaluacion {#s-duelo}

Con una sola rival viva, `duel.version` 1 (prefer_shorter 40 y gradiente de presion) debia ganar su A/B sin tocar la fase de cuatro. **Cerrada:** NO CONCLUYENTE, efecto acotado en [-0.16, +0.13] (ver docs/experimentos.md#s-duelo-r).

### S-DESESPERACION Hipotesis: una raiz perdida no se obedece {#s-desesperacion}

**Hipotesis falsable:** cuando la busqueda devuelve puntuacion de muerte en la raiz,
decidir con v0 -espacio real, zona de cabeza- en vez de con la rama que muere mas tarde
sube la tasa de duelos ganados y no cambia nada fuera de esas posiciones. Motivo medido:
ver docs/experimentos.md#s-desesperacion

**Que se mide:** `snake/config/v11-desesperacion.json`, v5 con `search.despair_version` 1
y nada mas. Dos tests lo acotan: en la posicion real del turno 241 v5 elige el bolsillo y
v11 no; y en posiciones donde la busqueda no se rinde, v11 decide identico a v5.

**Donde se mide:** en los DOS formatos del torneo. Royale de cuatro, que es la fase
principal, y **1v1 estandar**, que es el desempate: `arena_ab.py --serpientes 2 --mapa
standard`. Y contra snork Tree por HTTP, que fue 1o en la arena de duelos.

**Por que podria fallar:** a veces la rendicion es cierta y v0 no la salva; y v0 no mira
hacia delante, asi que puede elegir una casilla con espacio que el rival cierra en dos
turnos. Si la tasa de duelos ganados no sube, la hipotesis cae.

### S-LONGITUD-DUELO Hipotesis: en el duelo, cazar longitud {#s-longitud-duelo}

v12: con una rival viva, 40 por segmento de desventaja (saturado en +1) y caza de comida
mientras no vayamos por delante. **Sonda previa en contra**: 1v1 estandar contra v5, 3 000
nodos, 24 partidas: 1.75 de puesto medio sobre 1.5 del espejo. Perseguir la comida cede
territorio, que es lo que hizo perder el duelo real. No se le gasta un A/B completo.

### S-TERRITORIO-DUELO Hipotesis: en el duelo gana quien corta el tablero {#s-territorio-duelo}

**Hipotesis falsable:** con una sola rival viva, multiplicar `territory.weight` por 2 gana
mas duelos que v5. La longitud es consecuencia: quien tiene mas tablero llega antes a la
comida. En la partida real el rival no gano comiendo, gano levantando un muro por x=6.

**Sonda previa** (misma que v12): territorio x2 dio **1.375**, x0.5 dio 1.542. Es una
sonda de 24 partidas con 3 000 nodos; decide si merece el A/B, no si entra.

**Que se mide:** `snake/config/v13-territorio-duelo.json`, v5 con `duel.territory_version`
1. Con la version a 0 un test exige arbol identico. En 1v1 estandar y en royale, porque la
fase final de royale tambien es un duelo.

**Por que podria fallar:** el Voronoi premia llegar antes, no poder quedarse; con el cuerpo
mas corto, una region grande que no se puede defender vale menos de lo que puntua.

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
