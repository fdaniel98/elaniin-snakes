---
title: Roadmap de estrategia v0 a v5
read_when: "al proponer una version nueva del cerebro o al discutir que medir"
authority: speculative
last_verified: 2026-09-15
size_bytes: 15211
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

Penalizar el movimiento cuyo espacio se desploma al taparse UNA casilla. **Medida y
rechazada en royale:** el detector se dispara en el 92.9% de los estados, asi que entra
como ruido sumado al espacio (ver docs/experimentos.md#s-cuellos-r). Apagada en
`space.worst_case_weight`; su forma umbralada se midio como v14 y tampoco entra
(ver docs/strategy.md#s-trampa-duelo).

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

Cuatro experimentos dibujan un cuadro con una sola celda vacia: la evaluacion con
territorio no ayuda sin busqueda (v1 y cuellos), la busqueda ayuda con la evaluacion de v0
(-0.28), y falta el cruce. Es lo que predijo el rechazo de v1: «cuando exista busqueda, una
evaluacion mejor en las HOJAS si deberia notarse» (ver docs/experimentos.md#s-v1r).

**Que es:** `evaluate()` mide el espacio como TERRITORIO -Voronoi por BFS simultaneo- en
vez de como espacio alcanzable a secas; el crudo se conserva para la guarda de «no cabe ni
mi cuerpo». **Cuesta 1.2 niveles** (12.03 -> 10.89 con 4 vivas, 200 ms), profundidad que ya
se midio que no vale nada. **Medida: MEJORA** y esta en v5
(ver docs/experimentos.md#s-hojas-r).

### S-FORMATO El torneo es STANDARD, no royale {#s-formato}

Verificado en la partida real del torneo (`tests/fixtures-reales/dee2b0c8-*.json`):
`ruleset.name` y `map` son **standard** y el tablero no trae un solo hazard. El proyecto se
escribio entero apuntando a Royale, y eso invalida de raiz varias cosas:

- **v17 no puede entrar:** sin hazard no hay shrink que anticipar
  (ver docs/strategy.md#s-shrink).
- **El diagnostico de royale describe otro juego:** las 27 muertes por salud de 34 y la
  brecha de territorio del turno 100 son de un tablero que se encoge
  (ver docs/experimentos.md#s-shrink-r).
- **v5 gano su A/B en royale.** El control de longitud -la unica mejora grande del
  proyecto, -0.6917 contra v4- se midio donde el tablero se encoge y obliga a crecer. En
  standard ese coste es otro, asi que la superioridad de v5 sobre v4 **no esta medida en el
  formato que se juega**. Se re-mide con `snake/config/exp-sin-longitud.json`, que es
  default con `length.version` a 0 y nada mas.
- **Lo que si vale:** los duelos contra el zoo se jugaron en standard 1v1
  (ver docs/experimentos-duelo.md#s-campo-duelo).

Royale sigue soportado y el cerebro lee la variante del request, asi que esto no es un
cambio de codigo: es un cambio de que campo decide. El campo nuevo es `gauntlet-v2`
-standard, tres motores distintos, cuatro composiciones-, y se congela en la maquina que
corre el torneo con `./scripts/congelar-gauntlet.sh`.

**Lo primero re-medido, y sale bien:** apagar el control de longitud en standard de cuatro
da puesto medio **2.917**, IC95 [2.563, 3.270], contra el 2.500 del neutro (cuatro
serpientes identicas). El intervalo no cruza el neutro: **el control de longitud de v5
tambien funciona en standard**, no era un artefacto del hazard
(ver docs/experimentos.md#s-formato-r).

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

Subir `head.prefer_shorter` de 8 a 40 debia subir los duelos ganados. **Cerrada:** era un termino muerto bajo busqueda paranoica (ver docs/experimentos-duelo.md#s-cobrar-r).

### S-DUELO Hipotesis: el final de dos necesita su propia evaluacion {#s-duelo}

Con una sola rival viva, `duel.version` 1 (prefer_shorter 40 y gradiente de presion) debia ganar su A/B sin tocar la fase de cuatro. **Cerrada:** NO CONCLUYENTE, efecto acotado en [-0.16, +0.13] (ver docs/experimentos-duelo.md#s-duelo-r).

### S-DESESPERACION Hipotesis: una raiz perdida no se obedece {#s-desesperacion}

Cuando la busqueda devuelve puntuacion de muerte en la raiz, decidir con v0 en vez de con
la rama que muere mas tarde. **Cerrada: neutra** en los dos formatos del torneo; codigo
conservado y apagado en `search.despair_version` (ver docs/experimentos-duelo.md#s-desesperacion-r).

### S-LONGITUD-DUELO Hipotesis: en el duelo, cazar longitud {#s-longitud-duelo}

v12: con una rival viva, 40 por segmento de desventaja (saturado en +1) y caza de comida
mientras no vayamos por delante. **Sonda previa en contra**: 1v1 estandar contra v5, 3 000
nodos, 24 partidas: 1.75 de puesto medio sobre 1.5 del espejo. Perseguir la comida cede
territorio, que es lo que hizo perder el duelo real. No se le gasta un A/B completo.

### S-TERRITORIO-DUELO Hipotesis: en el duelo gana quien corta el tablero {#s-territorio-duelo}

v13: con una sola rival viva, `territory.weight` por 2. **Medida en 1v1 real: +0.075, IC95
[-0.015, +0.165]**, o sea peor que v5 sin llegar al delta. No entra; codigo conservado y
apagado en `duel.territory_version` (ver docs/experimentos-instrumento.md#s-territorio-duelo-r2).

### S-TRAMPA-DUELO Hipotesis: la sala con una sola puerta {#s-trampa-duelo}

v14: con una sola rival viva y la region ya justa, penalizar si cerrar UNA casilla nos deja
por debajo de nuestra longitud. Salio de que 22 de 32 derrotas contra snork-tree acabaron
sin ninguna casilla libre. **Medida contra snork-tree, 40 bloques: +0.075 (lado malo), IC95
[-0.054, +0.204]. No entra**; codigo conservado y apagado en `duel.trap_version`
(ver docs/experimentos-duelo.md#s-trampa-duelo-r).

El diagnostico sigue vivo y el remedio no: morimos encerrados, pero penalizar el cuello en
las hojas no lo evita. Lo que queda por probar es la version de RAIZ -una vez por turno,
sobre los movimientos candidatos- que cuesta 1/miles de lo que cuesta en las hojas.

### S-SUPERVIVENCIA-DUELO Hipotesis: poder quedarse, no llegar antes {#s-supervivencia-duelo}

v15: con un rival vivo, contar turnos que aguanto -cola dentro de la region, y comparacion
exacta de supervivencia cuando las dos regiones ya no se tocan- en vez de casillas que
alcanzo. **Medida contra snork-tree, 40 bloques: +0.050 (lado malo), IC95 [-0.094, +0.194].
No entra**; apagada en `duel.survival_version`.

Lo que dejo medido vale mas que el veredicto: v15 reprodujo el comportamiento de las snakes
fuertes -cola a <= 2 pasos el 25% de los turnos tardios, como snork- y perdio igual. La
correlacion de los replays no era causa (ver docs/experimentos-duelo.md#s-supervivencia-duelo-r).

### S-TABLA-DUELO Hipotesis: en el duelo hace falta calcular mas, no puntuar mejor {#s-tabla-duelo}

v16: tabla de transposicion con sello por busqueda y el mejor movimiento de la tabla
primero, mas una ordenacion barata opcional. **Medida con `bin/sonda_tt`: +0.2 niveles y
+-5% de nodos. No entra**, apagada en `search.tt_version`.

Lo que dejo medido: en el duelo ya buscamos **17 niveles** -no los 6.2 de royale con cuatro
vivas-, o sea que la trampa que nos mata (~13 turnos antes) cae dentro del horizonte. Y en
este juego casi no hay transposiciones, porque el cuerpo ES el historial de movimientos
(ver docs/experimentos-duelo.md#s-tabla-duelo-r).

### S-SHRINK Hipotesis: la busqueda planifica sobre un tablero que va a cambiar {#s-shrink}

**Hipotesis falsable:** penalizar las casillas que el proximo shrink puede convertir en
hazard sube el puesto medio en royale de cuatro por encima del delta de 0.10.

**El mecanismo, que no es una correlacion:** `royale_hazards()` vive en el motor pero solo
lo usa la arena para generar partidas. La busqueda ve el hazard **congelado**, asi que
planifica sobre un rectangulo que se encoge cada `shrinkEveryNTurns` y que ella cree fijo.
Peor: al bajar por el arbol el turno cruza la frontera del shrink y el tablero sigue sin
crecer, asi que las hojas de mas alla puntuan un tablero que ya no existe.

**Lo que dicen los datos** (60 partidas de v5, ver docs/experimentos.md#s-shrink-r): 27 de
34 derrotas son por salud, no por colision; y la cuota de territorio es IDENTICA en las
ganadas y las perdidas hasta el turno 100 (0.308 y 0.308) y se separa justo despues
(0.420 contra 0.351), que es cuando el hazard ya se comio parte del tablero.

**Que se mide:** `snake/config/v17-shrink.json`, v5 con `hazard.shrink_version` 1. El lado
del proximo shrink NO es conocible en partida real -el payload no trae la semilla-, asi que
se penaliza el riesgo repartido: 1/4 por cada borde del rectangulo seguro en el que este la
cabeza, escalado por lo cerca que esta el shrink y por si la salud aguanta el hazard.

**Por que podria fallar:** es la tercera vez que atacamos las muertes por salud (v6 y el
diagnostico de v15 fueron las otras), y las dos veces el mecanismo funciono y el puesto no
se movio: la causa de muerte es un sintoma de la posicion. Ademas la penalizacion empuja
hacia el centro, y v15 enseño que jugar el centro no es lo que hace ganar.

### S-BARRIDO La snake mas fuerte en standard se construye tamizando, no apilando {#s-barrido}

**El error que este apartado evita:** juntar todos los algoritmos escritos en una sola
config. v12, v13, v14 y v15 salieron todas del lado malo y v17 no aplica en standard;
apilar cosas que restan resta mas.

**Lo que si tiene base:** casi ninguno de esos candidatos se midio en el formato que se
juega. v6 y v13 se midieron en royale (ver docs/strategy.md#s-formato) y v14, v15 y v16
contra snork Tree, que resulto ser el rival que nos saca una distancia enorme mientras al
resto del campo le ganamos (ver docs/experimentos-duelo.md#s-campo-duelo). Son seis
algoritmos escritos, probados y apagados, juzgados en el juego equivocado.

**Procedimiento, en tres pasos y en este orden:**

1. **Tamiz** (`training-room/barrido.py`): cada candidato contra v5 en la arena, standard
   de cuatro, mismo numero de bloques y mismo presupuesto por nodos. Es self-play, barato y
   reproducible: sirve para DESCARTAR, no para aceptar. `exp-sin-longitud.json` va en la
   lista como control: se sabe que sale PEOR (ver docs/experimentos.md#s-formato-r), asi
   que si sale plano, el tamiz no esta midiendo.
2. **Torneo** contra `gauntlet-v2` de lo que sobreviva. El veredicto sale de ahi, porque un
   campo de copias de uno mismo no dice como se juega contra otros motores.
3. **Combinar solo lo que gano, y volver a medir.** Dos terminos que ganan por separado no
   ganan juntos por definicion: la combinacion es una candidata nueva y necesita su A/B.

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
