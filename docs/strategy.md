---
title: Roadmap de estrategia v0 a v5
read_when: "al proponer una version nueva del cerebro o al discutir que medir"
authority: speculative
last_verified: 2026-09-15
size_bytes: 12831
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

v3: alpha-beta con profundizacion iterativa y los rivales como minimizadores simultaneos, en
vez de decidir un turno. **Medida: ayuda (-0.28), y la profundidad no es el techo** -6 contra
12 niveles no se distingue- (ver docs/experimentos.md#s-busq-r). Esta en v5.

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
`ruleset.name` y `map` son **standard** y no hay un solo hazard. El proyecto apuntaba a
Royale, y eso invalida de raiz:

- **v17 no puede entrar:** sin hazard no hay shrink (ver docs/strategy.md#s-shrink).
- **El diagnostico de royale describe otro juego** (ver docs/experimentos.md#s-shrink-r).
- **v5 gano su A/B en royale**, pero **re-medido en standard aguanta**: apagar el control de
  longitud da 2.917 de puesto medio contra el 2.500 del neutro
  (ver docs/experimentos.md#s-formato-r).
- **Lo que si valia:** los duelos contra el zoo se jugaron en standard
  (ver docs/experimentos-duelo.md#s-campo-duelo).

El cerebro lee la variante del request y juega las dos, asi que esto no es codigo: es que el
campo que decide es `gauntlet-v2` -standard, tres motores, cuatro composiciones-, congelado
con `./scripts/congelar-gauntlet.sh` en la maquina que corre el torneo.

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

### S-SHRINK Hipotesis: anticipar el proximo shrink {#s-shrink}

v17: penalizar las casillas que el proximo shrink puede convertir en hazard, con el riesgo
repartido entre los cuatro bordes (el lado no es conocible: el payload no trae la semilla).
El mecanismo es real -`royale_hazards()` solo lo usa la arena, asi que la busqueda ve el
hazard congelado- y el diagnostico tambien
(ver docs/experimentos.md#s-shrink-r). **Pero el torneo es standard y ahi no hay hazard**,
asi que v17 no puede entrar (ver docs/strategy.md#s-formato). Queda apagada en
`hazard.shrink_version`, util solo si algun dia se juega royale.

### S-BARRIDO La snake mas fuerte en standard se construye tamizando, no apilando {#s-barrido}

**El error que este apartado evita:** juntar todos los algoritmos escritos en una sola
config. v12, v13, v14 y v15 salieron todas del lado malo y v17 no aplica en standard;
apilar cosas que restan resta mas.

**Lo que si tiene base:** casi ninguno se midio donde se juega. v6 y v13 en royale
(ver docs/strategy.md#s-formato), y v14, v15 y v16 contra snork Tree, el unico rival del
campo que nos saca distancia (ver docs/experimentos-duelo.md#s-campo-duelo).

**Procedimiento, en tres pasos y en este orden:**

1. **Tamiz** (`training-room/barrido.py`): cada candidato contra v5 en la arena, standard
   de cuatro, mismos bloques y mismo presupuesto por nodos. Self-play: sirve para
   DESCARTAR, no para aceptar. `exp-sin-longitud.json` va de control -se sabe que sale PEOR
   (ver docs/experimentos.md#s-formato-r)-, asi que si sale plano es el tamiz el que falla.
2. **Torneo** contra `gauntlet-v2` de lo que sobreviva. El veredicto sale de ahi, porque un
   campo de copias de uno mismo no dice como se juega contra otros motores.
3. **Combinar solo lo que gano, y volver a medir.** Dos terminos que ganan por separado no
   ganan juntos por definicion: la combinacion es una candidata nueva y necesita su A/B.

### S-UMBRAL-SUPERVIVENCIA Hipotesis: v15 servia, pero no siempre {#s-umbral-supervivencia}

**Hipotesis falsable:** encender el termino de supervivencia de v15 **solo** cuando el
espacio alcanzable baja de 1.6 veces nuestra longitud gana su A/B contra `gauntlet-v2`.

**Dos medidas que encajan:** sobre las 48 posiciones donde la derrota ya era irreversible,
v15 aguanta 27.0 turnos y sobrevive en 10, contra 17.8 y 3 de v5; pero su A/B contra Tree
dio +0.050, del lado malo. Un termino que ayuda en el final apretado y estorba en el resto da
exactamente ese par (ver docs/experimentos-duelo.md#s-derrumbe).

**Que se mide:** `snake/config/v18-umbral16.json` (y `v18-umbral25.json`, umbral 2.5). Con
`duel.survival_below_ratio` a 0 el arbol es identico a v15 y un test lo exige; otro exige que
en tablero abierto decida como v5. En el banco, v18 da 28.2 turnos y 11 supervivencias: el
umbral no quita nada donde el termino sirve. Si deja de estorbar en el resto lo dice el
torneo, no el banco.

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
