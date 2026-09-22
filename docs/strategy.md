---
title: Roadmap de estrategia v0 a v5
read_when: "al proponer una version nueva del cerebro o al discutir que medir"
authority: speculative
last_verified: 2026-09-15
size_bytes: 11978
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

**Hipotesis falsable:** si el duelo de dos decide 50 de cada 60 partidas y lo ganamos al
54% (ver docs/experimentos.md#s-duelo), y el unico termino que convierte la ventaja de
longitud en algo es `head.prefer_shorter` a 8.0 contra `head.avoid_equal_or_longer` a 80.0,
entonces subir ese peso debe subir la **tasa de duelos ganados** sin cambiar nada mas.

**Que se mide:** `snake/config/v9-cobrar.json`, que es `default.json` con
`head.prefer_shorter` subido y **nada mas**. Una sola variable, cero codigo nuevo.

**Metrica primaria:** diferencia pareada de puesto medio por bloque, como siempre. La tasa
de duelos ganados se reporta como **descriptiva**, sin p-valor: es el mecanismo, no el
veredicto (la leccion de ver docs/experimentos.md#s-supervivencia-r es justamente que un
mecanismo que funciona no garantiza un puesto mejor).

**Por que podria fallar, escrito antes de medir:** estar al lado de la cabeza de un rival
mas corto no fuerza el cabezazo -son movimientos simultaneos y el rival puede apartarse-,
asi que el premio puede estar pagando por una amenaza que no se ejecuta. Y un peso alto
acerca nuestra cabeza a la suya, que es exactamente donde se pierde si la busqueda calculo
mal la longitud relativa un turno mas tarde. Si el resultado es NO CONCLUYENTE con la tasa
de duelos igual, la hipotesis del mecanismo queda viva; si la tasa sube y el puesto no se
mueve, es otro caso de sintoma y no palanca.

### S-DUELO Hipotesis: el final de dos necesita su propia evaluacion {#s-duelo}

**Hipotesis falsable:** con exactamente dos vivas el juego deja de ser el que evalua v5
-cuatro serpientes, causas de muerte que son sintomas de la posicion- y pasa a ser un
juego de dos de suma cero, donde el modelo paranoico **deja de ser un sesgo y es
correcto**. Una evaluacion propia para esa fase debe ganar su A/B sin tocar la fase de
cuatro.

**Que cambia en el duelo, y por que:**

- la ventaja de longitud deja de acumularse y pasa a ser **presion**: si somos
  estrictamente mas largos, acercarse a la cabeza rival vale, porque el cabezazo lo
  ganamos por regla;
- los terminos que solo tienen sentido con varias vivas -la penalizacion por numero de
  vivos y la de «cuantos hay mas largos que yo»- dejan de aportar y se apagan.

**Aislamiento:** detras de `duel.version`, a 0 por defecto. Con 0 el arbol produce
exactamente los mismos movimientos que hoy, y eso es un test, no una promesa.

**Por que podria fallar:** el duelo empieza en el turno 136 de media, con el tablero ya
comido por los hazards. Puede que lo que decida ahi no sea la tactica sino la salud con la
que se llega, en cuyo caso la evaluacion del duelo llega tarde a una partida que ya estaba
perdida en el turno 100. La tasa de duelos ganados, segmentada por salud de entrada, lo
separa.

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
