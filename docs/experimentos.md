---
title: Experimentos de estrategia, medidos
read_when: "antes de proponer una heuristica o una version nueva: aqui esta lo que ya se probo y que dio"
authority: derived
source: docs/results/torneo-* y training-room/compara.py
last_verified: 2026-09-19
size_bytes: 15810
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
| v9 cobrar | `prefer_shorter` 8 -> 40 (ARENA) | 0.000 | termino muerto |
| v10 duelo | evaluacion propia del 1v1 (ARENA) | -0.017 | NO CONCLUYENTE |

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

Primera medicion en la arena, **self-play** (ver docs/decisions/ADR-0031-que-mide-la-arena.md#d-0311),
15 bloques, 19 761 nodos. v5 2.500 contra v7 2.583: diferencia **+0.0833**, IC95
[-0.2179, +0.3845], NO CONCLUYENTE y del lado malo.

**El mecanismo hizo lo suyo y no basto, otra vez:** simular al tercer rival bajo los
cabezazos de 10 a 7 -para lo que existe- y gano menos partidas. El riesgo estaba escrito
antes de medir (ver docs/decisions/ADR-0034-el-tercer-rival.md#d-0343): con tres rivales
simulados el modelo paranoico supone que los tres se coordinan contra nosotros.

**Y un error de diseño que costo media corrida:** rama B y campo eran los dos v5, asi que
las cuatro partidas de un bloque son la MISMA partida y la media sale 2.500 exacta en los
quince bloques. Sirvio de comprobacion del arnes -la partida es invariante a que silla
marcamos y los puestos suman 10-, y `arena_ab.py` ahora avisa cuando una rama coincide con
el campo.

### S-AFINADO-R Resultado del afinado: movido {#s-afinado-r}

Vive en ver docs/experimentos-afinado.md#s-afinado-r (sobreajuste de SPSA y su control).

### S-SHRINK Diagnostico de royale: se muere de salud, y la brecha se abre en el turno 100 {#s-shrink-r}

Las 60 partidas de `torneo-v5-longitud`, por fase y por causa.

**Como perdemos** (34 derrotas): **27 por salud** -hambre o hazard- y 7 por colision. En 16
de las 34 moriamos DENTRO del hazard, con el hazard cubriendo entre el 40% y el 78%.

**Cuando dejamos de poder comer:** desde el ultimo turno en que la comida seguia alcanzable
con la salud que teniamos, sobrevivimos una mediana de **10 turnos** (max 24). En ese
ultimo turno viable la salud mediana era 40 y el margen (salud menos coste del camino) 14,
pero hay casos con salud 88 y comida a coste 1, o salud 94 a coste 7: comida barata que no
se cogio porque `food.seek_below` es 50 y no teniamos hambre.

**Y la comida NO es la palanca.** Comida dentro de la region de Voronoi propia, turnos
>= 100: 1.98 en las ganadas contra 1.94 en las perdidas, y en las perdidas 1.94 nuestra
contra 2.47 del que gano. Lo que separa ganar de perder es el AREA:

| cuota de territorio | ganadas | perdidas | reparto justo |
|---|---:|---:|---:|
| turnos 0-60 | 0.308 | 0.308 | 0.260 |
| turnos 60-100 | 0.364 | 0.362 | 0.307 |
| turnos 100-140 | **0.420** | **0.351** | 0.364 |
| turnos 140-180 | 0.483 | 0.408 | 0.416 |
| turnos 180+ | 0.564 | 0.446 | 0.490 |

**Hasta el turno 100 jugamos identico en las que ganamos y en las que perdemos.** La brecha
se abre exactamente cuando el shrink empieza a pesar, y en las perdidas caemos por debajo
del reparto justo. De ahi sale ver docs/strategy.md#s-shrink.

### S-FORMATO-R El control de longitud tambien gana en standard {#s-formato-r}

Arena, **standard** de cuatro, 15 bloques (60 partidas), 14 821 nodos.
`exp-sin-longitud.json` es `default.json` con `length.version` a 0 y nada mas.

| | candidato sin longitud |
|---|---:|
| puesto medio | **2.917** |
| IC95 | [2.563, 3.270] |
| neutro (4 iguales) | 2.500 |
| primeros | 9 de 60 |

El intervalo **no cruza el neutro**: apagar el control de longitud cuesta +0.417 de puesto
medio. La unica mejora grande del proyecto se midio en royale (ver docs/experimentos.md#s-longitud-r)
y **se sostiene en el formato que de verdad se juega**; `default.json` se queda como esta.

De paso, el reparto de causas en standard de cuatro, que nunca habiamos mirado (51
muertes, self-play): **cabezazo 22, cuerpo propio 14, hambre 14, cuerpo rival 1**. En
royale el cuerpo propio casi no aparecia, y es justo la causa que costo el duelo del
torneo. Es self-play, asi que describe como nos matamos entre copias nuestras, no como nos
mata el campo: eso lo dira `gauntlet-v2`.

