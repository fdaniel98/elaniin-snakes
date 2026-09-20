---
title: Experimentos de estrategia, medidos
read_when: "antes de proponer una heuristica o una version nueva: aqui esta lo que ya se probo y que dio"
authority: derived
source: docs/results/torneo-* y training-room/compara.py
last_verified: 2026-09-19
size_bytes: 20595
---

# Experimentos de estrategia, medidos {#exp}

Las hipotesis y el roadmap viven en ver docs/strategy.md. Aqui solo lo que se midio y que
dio, para que nadie vuelva a proponer algo que ya se probo. Todos con el mismo protocolo:
60 partidas contra `gauntlet-v1`, 15 bloques pareados (semilla x rotacion de asientos),
metrica primaria unica (diferencia pareada de puesto medio), `training-room/compara.py`.

## Resumen {#exp-resumen}

| version | que cambiaba | dif. pareada | veredicto |
|---|---|---|---|
| v1 | territorio por Voronoi, sin busqueda | +0.050 | NO ENTRA |
| cuellos | salas de una sola puerta | +0.075 | NO ENTRA |
| busqueda prof. 6 | mirar hacia delante | **-0.267** | NO CONCLUYENTE |
| busqueda prof. 12 | el doble de profundidad | **-0.283** | NO CONCLUYENTE |
| prof. 6 -> prof. 12 | solo la profundidad | -0.017 | **la profundidad no es el techo** |
| v4 hojas | territorio en las HOJAS | **-0.367** | **MEJORA** |
| v5 longitud | ventaja de longitud + comida | **-0.692** vs v4 | **MEJORA** |
| v6 turnos | salud medida en turnos de vida | +0.058 | NO ENTRA |
| v7 tercer rival | simular al 3er rival (ARENA, self-play) | +0.083 | NO CONCLUYENTE |
| v8 afinado | SPSA sobre 14 pesos (ARENA, self-play) | -0.03 en control | NO ENTRA (sobreajuste) |

### S-SUPERVIVENCIA Por que v5 pierde las que pierde {#s-supervivencia}

Causas del final en las 60 partidas de `torneo-v5-longitud`:

| causa | partidas |
|---|---|
| sobrevivio (gano) | 26 |
| **hazard con poca salud** | **15** |
| **hambre** | **12** |
| otra / eleccion | 3 |
| sin salida (encerrada) | 3 |
| zona de cabeza mas larga | 1 |

**El diagnostico se ha invertido.** En v0 moriamos 132 de 178 veces sin ninguna salida; en
v5, 3 de 34. Ese problema esta resuelto. Ahora mata la salud: **27 de 34**. Y de los 21
segundos puestos, 11 son hazard y 10 hambre.

La causa es una unidad equivocada, y esta explicada en
ver docs/decisions/ADR-0028-turnos-de-supervivencia.md: en Royale el hazard cuesta 15 de
vida por turno, asi que los umbrales en salud absoluta -comer por debajo de 50, castigar el
hazard por debajo de 2 turnos- valen en tablero limpio y mienten donde de verdad se decide
la partida.



### S-LONGITUD-R Resultado: MEJORA, la mayor medida, y el mecanismo confirmado {#s-longitud-r}

60 partidas, 15 bloques pareados **contra v4** (lo que se prueba es la evaluacion, no la
busqueda):

| | v4 | v5 |
|---|---|---|
| puesto medio | 2.400 | **1.708** |
| turnos vividos | 179.8 | **199.0** |

Diferencia pareada **-0.6917**, IC95 **[-1.0121, -0.3713]**. Contra v0: **-1.0583**, IC95
[-1.3370, -0.7796]. Las dos: **MEJORA**. Es el efecto mas grande medido, casi el doble del
de v4 sobre v0.

| version | 1o | 2o | 3o | 4o | medio |
|---|---|---|---|---|---|
| v0 | 2 | 14 | 40 | 4 | 2.767 |
| v3 | 10 | 15 | 30 | 5 | 2.483 |
| v4 | 8 | 21 | 30 | 1 | 2.400 |
| **v5** | **26** | 24 | 10 | **0** | **1.708** |

26 primeros puestos de 60, y **ningun cuarto**.

**El mecanismo se confirmo, y eso resuelve el matiz de correlacion.** La hipotesis decia
que la longitud podia ser sintoma y no causa. Al intervenir sobre la politica de comida se
movieron las DOS cosas, que es evidencia causal y no observacional:

| | v4 | v5 |
|---|---|---|
| ventaja de longitud media | negativa en 49 de 60 | **+0.36** |
| partidas siendo mas largos de media | 11 de 60 | **34 de 60** |
| terminamos siendo los mas largos | 15 de 60 | **41 de 60** |

Y la relacion sigue viva DENTRO de v5: las 8 partidas en que quedamos terceros tienen una
ventaja media de **-1.29**, y las 26 que ganamos, de +0.38. Donde seguimos siendo cortos,
seguimos perdiendo.

Latencia: 0 timeouts en 11 914 movimientos, maximo 399 ms de 500.

### S-LONGITUD Por que v4 quedaba tercera {#s-longitud}

Analisis de los 60 JSONL de `torneo-v4-hojas`, que es la primera vez que se mira POR QUE
pierde la version que gano:

- morimos en el turno **180** de media;
- en **47 de 60 partidas** todos los rivales vivos eran iguales o mas largos que nosotros;
- solo en 15 eramos los mas largos.

| puesto | partidas | ventaja de longitud media |
|---|---|---|
| 1o | 8 | **-0.42** |
| 2o | 21 | -1.17 |
| 3o | 30 | **-1.61** |

11 partidas siendo mas largos de media dan puesto 2.00; 49 siendo mas cortos dan 2.49. Esa
brecha de 0.49 es mayor que toda la mejora de v4 sobre v0.

**Somos una snake corta, y eso es causal en nuestro propio codigo:** perdemos todos los
cabezazos, y `evaluate()` penaliza quedar adyacente a cualquier cabeza igual o mas larga,
asi que ser corto encoge el espacio que consideramos seguro. Nos encerramos solos.

La hipotesis y el diseño, en ver docs/decisions/ADR-0026-control-de-longitud.md. El matiz
que va con ella: esto es correlacion, y si el A/B sale NO CONCLUYENTE la lectura es que la
longitud era sintoma y no causa.


### S-HOJAS-R Resultado: MEJORA, y cierra el cuadro {#s-hojas-r}

60 partidas, 15 bloques pareados. **Primer veredicto concluyente del proyecto.**

| | v0 | v4 (hojas) |
|---|---|---|
| puesto medio | 2.767 | **2.400** |
| turnos vividos | 119.2 | **179.8** |

Diferencia pareada **-0.3667**, IC95 **[-0.6594, -0.0739]**. El intervalo **no cruza el
cero** y el efecto supera el delta declarado de 0.10. Veredicto: **MEJORA**.

El reparto de puestos dice donde se gana, y no es donde uno esperaria:

    v0          1o= 2  2o=14  3o=40  4o= 4
    v3 prof. 6  1o= 7  2o=18  3o=33  4o= 2
    v3 prof. 12 1o=10  2o=15  3o=30  4o= 5
    v4 hojas    1o= 8  2o=21  3o=30  4o= 1

v4 no es la que mas gana -esa es la de profundidad 12, con 10 victorias- sino la que
**menos pierde**: un solo cuarto puesto en 60 partidas, contra los 4 de v0 y los 5 de la
version mas profunda. Convierte terceros en segundos. En un torneo que puntua por posicion
eso vale mas que una victoria mas y tres hundimientos.

Contra v3 sola, el cambio de evaluacion da **-0.0833** con IC [-0.429, +0.262]: NO
CONCLUYENTE por si mismo. Es la suma -busqueda mas evaluacion en las hojas- la que cruza
la frontera, que es exactamente lo que el cuadro de ver docs/strategy.md#s-hojas predecia:
ninguna de las dos piezas basta sola.

**El arco completo, en cinco mediciones y 300 partidas:**

1. evaluacion mejor sin busqueda: no ayuda (v1, cuellos);
2. busqueda con evaluacion pobre: ayuda pero no concluye (-0.27, -0.28);
3. mas profundidad de la misma busqueda: no aporta nada (-0.017);
4. **busqueda con evaluacion mejor EN LAS HOJAS: MEJORA (-0.367).**

La leccion que queda escrita: la profundidad y la evaluacion no son alternativas que
compiten por el presupuesto, son complementos que solo valen juntos.


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

### S-BUSQ-R Resultado: ayuda, pero la profundidad no es el techo {#s-busq-r}

Dos corridas independientes de 60 partidas cada una contra `gauntlet-v1`, pareadas por los
mismos 15 bloques que v0:

| | v0 | busqueda, prof. 6 | busqueda, prof. 12 |
|---|---|---|---|
| puesto medio | 2.767 | 2.500 | **2.483** |
| dif. pareada vs v0 | — | **-0.267** | **-0.283** |
| IC95 | — | [-0.562, +0.029] | [-0.573, **+0.006**] |
| victorias | 2 | 7 | **10** |
| turnos vividos | 119.2 | 170.5 | 166.4 |

Veredicto formal en las dos: **NO CONCLUYENTE**, la segunda por seis milesimas. Dos
replicas independientes cayendo en el mismo sitio es evidencia consistente aunque 15
bloques no la formalicen.

**El hallazgo que importa no es ese, sino este.** Comparando las dos busquedas ENTRE SI,
mismos bloques:

    prof. 6 (350 ms)  vs  prof. 12 (200 ms):  -0.0167   IC95 [-0.373, +0.340]

**Duplicar la profundidad no movio nada.** El reparto explica por que: de 6 a 12 niveles se
ganaron 3 victorias y 3 ultimos puestos. Una snake mas arriesgada, no mejor.

    v0          1o= 2  2o=14  3o=40  4o= 4
    prof. 6     1o= 7  2o=18  3o=33  4o= 2
    prof. 12    1o=10  2o=15  3o=30  4o= 5

**El techo no es la profundidad: es la evaluacion de las hojas.** Una busqueda mas honda de
una evaluacion mediocre encuentra lineas mediocres con mas conviccion.


### S-SUPERVIVENCIA-R Resultado: NO ENTRA, y el error estaba en el diagnostico {#s-supervivencia-r}

60 partidas, 15 bloques pareados **contra v5**:

| | v5 | v6 |
|---|---|---|
| puesto medio | **1.708** | 1.767 |
| turnos vividos | **199.0** | 193.7 |
| 1o / 2o / 3o / 4o | 26 / 24 / 10 / 0 | 28 / 18 / 14 / 0 |

Diferencia pareada **+0.0583**, IC95 **[-0.1970, +0.3136]**, delta util 0.1:
**NO CONCLUYENTE**, y del lado malo. v6 **no entra**; `default.json` se queda en v5.

**El mecanismo funciono y el resultado no se movio.** Las causas del final, mismas 60
partidas, `./build/release/bin/causas --nuestra <slug>`:

| causa | v5 | v6 |
|---|---|---|
| sobrevivio (gano) | 26 | **28** |
| hazard con poca salud | 15 | **9** |
| hambre | 12 | **10** |
| sin salida (encerrada) | 3 | 5 |
| otra / eleccion | 3 | 7 |
| zona de cabeza mas larga | 1 | 1 |

Medir la salud en turnos de vida hizo exactamente lo que ADR-0028 predijo: las muertes por
falta de vida bajaron de 27 a 19. Pero las otras subieron de 6 a 12, y el puesto medio
empeoro. Lo que se gano saliendo antes del hazard se perdio en el sitio al que se salio.

**La leccion, y es la que hay que recordar antes de proponer la siguiente heuristica:** en
un juego de cuatro, la causa de muerte es un **sintoma de la posicion, no una palanca
independiente**. Atacar la causa mas frecuente redistribuye las muertes sin mover el
puesto, porque la snake no muere de hambre: muere de estar en el sitio donde solo quedaba
comer mal. El riesgo estaba escrito antes de medir, en
ver docs/decisions/ADR-0028-turnos-de-supervivencia.md#adr-0028-riesgo, asi que esto es el
experimento saliendo negativo, no una explicacion inventada despues.

El codigo de v6 se queda en el arbol tras `survival.version`, en 0 por defecto: cuesta cero
y la hipotesis puede volver a probarse cuando el campo sea otro.

### S-TERCER-RIVAL-R Resultado en arena: NO CONCLUYENTE, y medio experimento tirado {#s-tercer-rival-r}

Primera medicion hecha en la arena. **Es self-play**: las otras tres sillas eran v5, no el
gauntlet (ver docs/decisions/ADR-0031-que-mide-la-arena.md#d-0311). 15 bloques, 19 761
nodos por movimiento -el equivalente a 200 ms en la maquina de referencia-, 44 minutos.

| | v5 | v7 |
|---|---|---|
| puesto medio | **2.500** | 2.583 |
| turnos vividos | **162.6** | 157.6 |
| gano la partida | **15** | 12 |
| murio en cabezazo | 10 | **7** |

Diferencia pareada **+0.0833** contra v7, IC95 [-0.2179, +0.3845]: **NO CONCLUYENTE**, y
del lado malo. No llega para gastarle un torneo de 2-3 horas contra el gauntlet.

**El mecanismo hizo lo suyo y no basto, otra vez.** Simular al tercer rival redujo las
muertes por cabezazo de 10 a 7 -que es exactamente para lo que existe- y aun asi gano
menos partidas. El riesgo estaba escrito antes de medir en
ver docs/decisions/ADR-0034-el-tercer-rival.md#d-0343: con tres rivales simulados el modelo
paranoico supone que las **tres** serpientes se coordinan contra nosotros, y eso encoge lo
que la busqueda considera jugable.

**Y un error de diseño mio que costo la mitad de la corrida.** La rama B era v5 y el campo
tambien: con los cuatro contendientes iguales, las cuatro partidas de un bloque son **la
misma partida** -solo cambia a que silla llamamos nuestra-, asi que los puestos son 1, 2,
3 y 4 y la media sale **2.500 exacta en los quince bloques**. Se ve en los turnos: 306,
306, 306, 306.

No aporto nada sobre fuerza y consumio 60 de las 120 partidas. Lo unico que salva ese
gasto es que resulto ser una comprobacion muy fuerte del arnes, aunque no la planee: la
partida es invariante a que silla marcamos, los puestos suman 10 siempre y la rotacion de
asientos no mete sesgo. `arena_ab.py` ahora avisa antes de empezar cuando una rama coincide
con el campo.

Para un A/B de verdad el campo tiene que ser distinto de las dos ramas. El candidato
natural es `v4-hojas`, que es fuerte -perdio con v5 por 0.69- y no es ninguna de las dos.

### S-AFINADO-R Resultado del afinado: hay señal, y no es la que imprimio el script {#s-afinado-r}

80 iteraciones de SPSA, 5 120 partidas, 1 000 nodos, 106 minutos
(ver docs/decisions/ADR-0036-afinado-por-spsa.md). El script imprimio **2.109** como mejor
puesto medio contra el 2.500 exacto del campo. **Ese numero esta sesgado y no se publica
como resultado**: es el MINIMO de 80 evaluaciones ruidosas, y el minimo de 80 sorteos cae
varios errores tipicos por debajo de la media aunque no hubiera mejorado nada.

Lo que si mide algo es la trayectoria, porque promedia:

| iteraciones | puesto medio |
|---|---:|
| 1-10 | 2.470 |
| 11-20 | 2.459 |
| 21-30 | 2.364 |
| 31-40 | 2.302 |
| 41-50 | 2.295 |
| 51-60 | 2.298 |
| 61-70 | 2.310 |
| 71-80 | **2.263** |

Media de las 20 primeras **2.4645**, de las 20 ultimas **2.2867**. Parecia una mejora de
-0.21, a casi ocho errores tipicos. **No lo era.**

El punto de llegada es estable: el mejor visto, el ultimo y el promedio de las 20 ultimas
iteraciones coinciden **dentro de +-0.05 por parametro**. No hay que elegir entre ellos.

**Lo que aprendio, que tiene sentido y no parece ruido:**

| parametro | v5 | v8 | lectura |
|---|---:|---:|---|
| `head.avoid_equal_or_longer` | 80 | **69** | menos miedo a las cabezas iguales o mayores |
| `head.prefer_shorter` | 8 | **9.5** | y mas ganas de ir a por las menores |
| `length.advantage_weight` | 60 | **72.9** | la longitud pesa aun mas de lo que pesaba |
| `length.hunt_weight` | 10 | **11.4** | y se persigue mas |
| `territory.hazard_value_pct` | 50 | **38** | el territorio dentro del hazard vale menos |
| `food.weight` | 6 | **5.6** | menos comida por comer |
| `food.seek_below_in_hazard` | 75 | **86** | pero comer MUCHO antes dentro del hazard |

Las dos ultimas juntas son lo interesante: el afinador no conoce el concepto de "turnos de
vida" que v6 intento meter a mano (ver docs/experimentos.md#s-supervivencia-r), y aun asi
llego solo a que dentro del hazard hay que comer antes. La hipotesis de v6 no era falsa;
estaba implementada en el sitio equivocado.

### El control lo tumba {#s-afinado-control}

Medido en semillas que el afinado no jugo nunca:

| donde se mide | partidas | v8 | v5 | mejora |
|---|---:|---:|---:|---:|
| semillas DEL AFINADO (base 1000) | 32 | **2.2188** | 2.5000 | -0.28 |
| semillas FRESCAS (base 50000) | 80 | **2.4688** | 2.5000 | **-0.03** |

Con 20 bloques el error tipico ronda 0.116, asi que 2.4688 es **indistinguible de 2.5**.
La mejora entera era memoria de 32 partidas concretas. **v8 NO entra.**

**La causa es una decision mia, y estaba escrita como si fuera una virtud.** El afinador
usaba los MISMOS 8 bloques en las 160 evaluaciones. Numeros aleatorios comunes son lo
correcto para **comparar** dos alternativas fijas -reducen la varianza de la diferencia- y
son una trampa para **optimizar**, porque el optimizador puede explotar una muestra que no
cambia nunca. Eso es exactamente lo que hizo: los pesos que encontro son buenos en esas 32
partidas y en ninguna otra.

Arreglado en `afina.py`, y en dos sitios:

1. **Las semillas rotan entre iteraciones.** Dentro de una iteracion las dos evaluaciones
   siguen compartiendolas -ahi los numeros comunes son correctos y no dejan nada que
   memorizar-, pero cada iteracion juega bloques propios.
2. **Evaluacion de control obligatoria al terminar**, en semillas que ninguna iteracion
   toco, y **ese** es el numero que se publica. Si no baja de 2.5, el script lo dice:
   `NO MEJORA: el control no baja de 2.5`.

Lo que costo saberlo: cuatro minutos. Por eso el paso de verificacion existe antes del
gauntlet y no despues.

**Un resultado colateral que si es bueno:** las 80 partidas de control dieron **2.4688
exacto** en la maquina de referencia y en el contenedor de desarrollo, que son maquinas
distintas con la mitad de nucleos una que la otra. Es la primera comprobacion de extremo a
extremo de que la arena es reproducible entre maquinas, que era una promesa del diseño
(ver docs/decisions/ADR-0030-presupuesto-por-nodos.md#d-0301) y hasta ahora solo una
promesa.

`snake/config/v8-afinado.json` se conserva como la evidencia del sobreajuste, no como
candidata.

### S-DUELO Donde se decide la partida, y nadie lo habia mirado {#s-duelo}

Analisis de las mismas 60 partidas de `torneo-v5-longitud`, esta vez preguntando **en que
fase** se pierde en vez de por que causa.

| | n | donde salimos |
|---|---:|---|
| 1os | 26 | quedamos solos |
| 2os | **24** | **las 24 en un 1v1** |
| 3os | 10 | con tres vivas, turno 166 |

**50 de las 60 partidas llegan a un duelo de dos y ganamos el 54%.** El duelo empieza hacia
el turno 136 y dura 70 turnos de media: un tercio de la partida. Toda la diferencia entre
primero y segundo vive ahi, y sale a cara o cruz.

Y el predictor, por la ventaja de longitud con la que ENTRAMOS al duelo (contra el rival
mas largo, que es la metrica que usa `evaluate`):

| ventaja al empezar el duelo | n | duelos ganados |
|---|---:|---:|
| [-3, 0) | 12 | 75% |
| empate | 4 | 75% |
| [+1, +3] | 18 | 56% |
| >= +4 | 16 | **31%** |

Monotono y al reves de lo que predice v5. Con la ventaja medida contra la MEDIA de rivales
sale el mismo cuadro: por debajo de -1 el puesto medio es 2.600 sin un solo primero, y por
encima de la paridad la curva es plana (1.375 / 1.600 / 1.750 / 1.706).

**La lectura, que es una hipotesis y no un hecho:** la ventaja de longitud no se cobra. El
termino que la premia satura en `length.target_lead` y el unico sitio donde se convierte en
algo es la zona de cabeza, con `head.prefer_shorter` a 8.0 frente a
`head.avoid_equal_or_longer` a 80.0 -diez a uno-, mientras el espacio pesa 100 y el
territorio 120. Pasamos 136 turnos comprando una ventaja y jugamos el duelo como si no la
tuvieramos, cargando el coste del cuerpo grande en un tablero que se encoge.

**El sesgo que juega en contra de esa lectura, y hay que decirlo:** es observacional, con
12 a 18 partidas por celda, y hay seleccion. Un rival que llega al duelo siendo cuatro
segmentos mas corto que nosotros es, justamente, uno que sabe sobrevivir sin comer. Puede
que el 31% no diga «ser largo estorba» sino «el que llega corto al duelo es bueno». Eso
solo lo separa una intervencion, que es ver docs/strategy.md#s-cobrar.

**Y el dato que mas incomoda:** v1, v3, v4, v5, v6, v7 y v8 optimizaron todas la fase de
cuatro. Ninguna toco el final de dos. Los tres rechazos seguidos contra v5 pueden no
significar que v5 sea dificil de superar, sino que llevamos seis experimentos afinando el
tramo ruidoso mientras el que decide sigue sin tocar.

Hipotesis derivadas: ver docs/strategy.md#s-cobrar y ver docs/strategy.md#s-duelo
