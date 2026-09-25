---
title: Experimentos del duelo 1v1
read_when: "antes de proponer cualquier cambio que toque el final de dos, o de mirar un replay y sacar conclusiones"
authority: derived
source: docs/results/duelo-snork-* y torneo-v5-longitud
last_verified: 2026-09-24
size_bytes: 15699
---

# Experimentos del duelo

## Resumen {#exp-duelo}

Donde se decide la partida y donde llevamos seis intentos sin mover la aguja. Contra
`snork-tree` (1v1 estandar, 240 partidas de linea base) v5 gana ~26% de forma estable.
El resto de experimentos esta en docs/experimentos.md#exp-resumen.

### S-DUELO Donde se decide la partida, y nadie lo habia mirado {#s-duelo}

Analisis de las 60 partidas de `torneo-v5-longitud`, por FASE en vez de por causa:
**50 de 60 llegan a un duelo de dos y ganamos el 54%**. El duelo empieza hacia el turno 136
y dura 70 turnos: un tercio de la partida, y toda la diferencia entre primero y segundo.
Los 24 segundos puestos salieron los 24 en un 1v1.

Duelos ganados segun la ventaja de longitud con la que ENTRAMOS, contra el rival mas largo:

| ventaja | n | ganados |
|---|---:|---:|
| [-3, 0) | 12 | 75% |
| empate | 4 | 75% |
| [+1, +3] | 18 | 56% |
| >= +4 | 16 | **31%** |

Monotono y al reves de lo que predice v5. **Lectura (hipotesis, no hecho):** la ventaja de
longitud no se cobra; el unico sitio donde se convierte en algo es la zona de cabeza, con
`head.prefer_shorter` a 8.0 contra `head.avoid_equal_or_longer` a 80.0. **El sesgo en
contra:** es observacional, 12-18 partidas por celda y con seleccion -quien llega corto al
duelo es, justamente, quien sabe sobrevivir sin comer-. Lo que salio de aqui (v9 a v13) se
midio entero y ninguna entro; lo vivo es ver docs/strategy.md#s-trampa-duelo.

### S-COBRAR-R Resultado: el termino esta muerto, no es ruido {#s-cobrar-r}

A/B de arena, v5 contra v9 (`head.prefer_shorter` 8 -> 40), 15 bloques, 14 821 nodos:
**las dos ramas jugaron las mismas 60 partidas**, movimiento a movimiento (1.742 las dos,
causas y turnos identicos). Una sonda sobre 612 posiciones con `prefer_shorter` a 8, 40 y
400 da la misma puntuacion de raiz en las 612.

**Por que, y es lo que hay que recordar:** el termino premia terminar con la cabeza pegada
a un rival mas corto, pero en la busqueda paranoica ese rival es un **minimizador** y
apartarse siempre esta entre sus respuestas. El simetrico `avoid_equal_or_longer` si vive
-al rival le conviene acercarse-, asi que la asimetria no la pone el peso, la pone el
modelo. Cobrar la ventaja no es premiar el contacto, es **quitarle sitio** al rival.

**v9 no entra** y mas bloques no la moverian. `compara.py` marca ahora **RAMAS IDENTICAS**
para que un termino muerto no vuelva a leerse como ruido.

### S-DUELO-R Resultado: NO CONCLUYENTE, y el tamaño del efecto ya esta acotado {#s-duelo-r}

Mismo protocolo que ver docs/experimentos-duelo.md#s-cobrar-r: v5 contra v10 (`duel.version` 1),
campo `v4-hojas`, 15 bloques, 14 821 nodos, maquina de referencia.

| | v5 | v10 |
|---|---|---|
| puesto medio | 1.742 | 1.725 |
| turnos vividos | 200.2 | 193.7 |
| sobrevivio (gano) | 32 | 33 |
| hambre / hazard | 9 / 10 | 6 / 12 |

Diferencia pareada **-0.0167**, IC95 **[-0.161, +0.127]**, delta util 0.1: **NO
CONCLUYENTE**. v10 **no entra**; `default.json` sigue en v5 y `duel.version` en 0.

Por bloque: 5 mejores, 4 peores, 6 iguales. Que haya bloques identicos cuadra con la sonda previa: v10 cambia el movimiento en el
3.9% de las posiciones de duelo, asi que muchas partidas no llegan a divergir.

**Lo que si dice, y vale:** el IC95 acota el efecto. Si el modo duelo ayuda, ayuda **como
mucho 0.16 de puesto**, y puede que empeore 0.13. No es la palanca grande que sugeria el
analisis de ver docs/experimentos-duelo.md#s-duelo. Y en las causas se repite el patron de v6
(ver docs/experimentos.md#s-supervivencia-r): menos hambre, mas hazard, puesto igual. La
muerte se redistribuye; la posicion no cambia.

**Lo que no dice:** el analisis del duelo salio de partidas contra el gauntlet; esta
medicion es contra `v4-hojas` en la arena. El duelo contra un v4 no tiene por que parecerse
al duelo contra las snakes del zoo. Y 15 bloques no resuelven efectos por debajo de ~0.15:
para ver 0.05 harian falta del orden de cien bloques.

**Balance de la serie contra v5:** v6, v7, v8, v9 y v10, cinco candidatas, ninguna entra.
v9 era un termino muerto; las otras cuatro tienen efectos compatibles con cero y acotados
por debajo de ~0.2. Lo que se mueve a este tamaño de muestra ya se movio en v5.

### S-DESESPERACION Un duelo real del torneo: la busqueda se rinde y se mete en un bolsillo {#s-desesperacion}

Primera partida REAL analizada, `dee2b0c8` del torneo: **1v1 estandar** -no royale-,
Makarov (la nuestra) contra Chimuelo, 245 turnos. Frames del motor oficial pasados por
`training-room/partida.py`; el cerebro de este arbol repite 98 de los 100 primeros
movimientos, asi que lo que se ve es lo que se jugo.

- Hasta el turno ~100 ibamos parejos o por delante en territorio, pero el rival crecia mas
  (14 contra 11 en el 90, 24 contra 19 en el 230).
- Entre el 228 y el 237 el rival levanta un muro por la columna x=6 y nos deja la franja
  derecha, que ocupa buena parte de nuestro propio cuerpo.
- **Desde el turno 238 la busqueda da la partida por perdida**: puntuacion de raiz
  -100000 a -100210, con 76 casillas alcanzables. Bajo el supuesto paranoico, el rival mas
  largo puede adivinar cada casilla a la que vamos y ganar el cabezazo.
- A partir de ahi solo elige la muerte mas lenta. En el 241, con cuerpo 20 y **66 casillas
  alcanzables**, elige `right`: un bolsillo de 3. Muere en el 245 contra su cuello, que en
  el tablero se ve como «choca consigo misma».

La causa no es un bug ni un timeout: latencias de 168 ms hasta el final. Es el modelo. Con
movimientos simultaneos, «perdida» significa que el rival *podria* adivinar; la busqueda
lo trata como certeza y prefiere una muerte segura en 4 turnos a una apuesta con 66
casillas por delante.

Hipotesis derivada: ver docs/strategy.md#s-desesperacion

### S-TRAMPA-DUELO-R Resultado: la trampa umbralada no entra, y el duelo sigue abierto {#s-trampa-duelo-r}

Primer A/B contra un rival EXTERNO: 1v1 estandar por HTTP con el arbitro oficial contra
`snork-tree`, 40 bloques (80 partidas) por rama, pinning de CPU para las dos.

| | v5 | v14 |
|---|---|---|
| puesto medio | 1.738 | 1.812 |
| duelos ganados | 21 de 80 | 15 de 80 |
| turnos vividos | 381.8 | 347.2 |
| movimientos >= 350 ms | 13 de 30 546 | 32 de 27 779 |

Diferencia **+0.075** (del lado malo), IC95 [-0.054, +0.204]: NO CONCLUYENTE y apuntando a
peor, igual que v13. **v14 no entra**; codigo conservado y apagado en `duel.trap_version`.

Dos cosas que el numero no dice y los logs si:

- **No es el coste medio.** p50 y p99 son identicos a v5 (148 y 156 ms): el umbral hace su
  trabajo y en la inmensa mayoria de las hojas no se calcula nada. Lo que crece son los
  **picos**: 26 movimientos tocaron el techo de 500 ms contra 9 de v5. Pero solo 8 de las
  65 derrotas tuvieron algun timeout, asi que los timeouts no explican las 6 derrotas de
  mas.
- **La linea base es solida.** Tres corridas de v5 contra snork-tree dieron 1.750, 1.738 y
  1.800 de puesto medio: perdemos el duelo contra snork Tree de forma estable, ~26% de
  victorias. El diagnostico de las derrotas (22 de 32 encerrados, mediana de 13 turnos
  desde el ultimo turno con territorio >= longitud) sigue en pie; lo que falla es el
  remedio, no el diagnostico.

### S-SUPERVIVENCIA-DUELO-R Resultado: copiamos el comportamiento del que gana y seguimos perdiendo {#s-supervivencia-duelo-r}

1v1 estandar por HTTP contra `snork-tree`, 40 bloques por rama, pinning de CPU.

| | v5 | v15 |
|---|---|---|
| puesto medio | 1.738 | 1.788 |
| duelos ganados | 21 de 80 | 17 de 80 |
| turnos vividos | 381.8 | 337.7 |
| timeouts del RIVAL | 11 | 27 |

Diferencia **+0.050** (lado malo), IC95 [-0.094, +0.194]: NO CONCLUYENTE, y con el campo
mas enfermo a nuestro favor. **v15 no entra**; apagada en `duel.survival_version`.

**Lo que hay que recordar no es el veredicto, es esto.** v15 SI cambio el comportamiento en
la direccion buscada, medido sobre los turnos >= 250:

| turnos >= 250 | v5 | v15 | snork |
|---|---|---|---|
| turnos con la cola a <= 2 pasos | 16% | **25%** | 23% |
| cola alcanzable | 79% | **87%** | — |
| espacio medio | 49.8 | **52.9** | — |
| regiones ya separadas | 42% | **28%** | — |

Reprodujimos el tail-chasing de las snakes fuertes y perdimos igual, con partidas **mas
cortas**. La correlacion de los replays -el que gana se pega a su cola y juega el centro- no
era causa: es lo que puede hacer el que ya va ganando.

**Lo que queda en pie:** si sobrevivimos mejor dentro de nuestra region y aun asi morimos
antes, el duelo se pierde mientras las dos regiones TODAVIA se tocan, que es donde se
decide el reparto. Eso no lo arregla un termino de evaluacion.

### S-TABLA-DUELO-R Resultado: en el duelo ya vemos 17 niveles, y calcular mas no es el problema {#s-tabla-duelo-r}

`bin/sonda_tt`, 10 posiciones de duelo generadas jugando partidas de verdad con el propio
cerebro y parando entre el turno 60 y el 120, presupuesto 150 ms:

| | profundidad media | nodos/movimiento | aciertos de tabla |
|---|---:|---:|---:|
| v5 (sin tabla) | 17.20 | 14 773 | 0 |
| tabla | **17.40** | 13 773 | 109 |
| orden barato | 16.90 | 15 071 | 0 |
| tabla + orden barato | 17.20 | 16 148 | 140 |

**Todo cae dentro del +-5% de nodos y +-0.5 niveles.** Dos cosas que explican por que, y
son de este juego y no de nuestro codigo:

1. **Aqui casi no hay transposiciones.** En ajedrez dos ordenes de las mismas jugadas dan
   el mismo tablero; en Battlesnake el CUERPO es el historial de movimientos, asi que dos
   caminos distintos dejan cuerpos distintos y nunca son la misma posicion. Lo poco que
   acierta la tabla es la reutilizacion entre profundidades de la profundizacion iterativa.
2. **El flood fill de la ordenacion no es el cuello.** Sustituirlo por contar salidas mueve
   los nodos un 2% y pierde profundidad: ordena peor.

**Y el numero que cambia el diagnostico:** en el duelo la busqueda llega a **17 niveles**
de media, no a los 6.2 que se midieron con cuatro serpientes (ver docs/performance.md#p-10).
La trampa que nos mata se tiende ~13 turnos antes, o sea DENTRO del horizonte. No perdemos
por no ver lo suficiente.

**v16 no entra**; apagada en `search.tt_version` y `search.order_version`, con la sonda
conservada. Lo descartado aqui es una familia entera: subir profundidad en el duelo no es
la palanca, igual que no lo eran los terminos de evaluacion (v12 a v15).

### S-DESESPERACION-R Resultado en 1v1: cambia COMO se muere, no QUIEN gana {#s-desesperacion-r}

Arena, 1v1 estandar, v11 contra v5 (campo v5, asi que la rama A sale 1.500 exacto), 60
bloques, 14 821 nodos, maquina de referencia.

| | v5 | v11 |
|---|---|---|
| puesto medio | 1.500 | 1.492 |
| bloques distintos | | 3 de 60 (2 mejores, 1 peor) |
| muertes: cuerpo propio / cabezazo | **42** / 16 | 16 / **42** |

Diferencia pareada **-0.0083**, IC95 **[-0.037, +0.020]**: NO CONCLUYENTE, y con un
intervalo tan estrecho que acota el efecto a casi cero. v11 **no entra**.

Las muertes «contra si misma» se convierten en cabezazos: contra un rival que busca, la
partida que la busqueda da por perdida suele estarlo. La causa de muerte es un sintoma.

**Donde se decide entonces**, con una sonda de 8 duelos v5 contra v5: el perdedor se rinde
siempre **sin ir por delante en longitud** (diferencia al rendirse: -5, -1 x6, 0 x2) y a
los 100 turnos ya iba por detras en 6 de 8. Coincide con la partida real, donde ibamos 5
por detras. En un duelo, ser mas corto regala el cabezazo, y el supuesto paranoico lo
convierte en derrota segura. La palanca es llegar al final por delante, no la tactica final.

**Royale** (15 bloques contra `v4-hojas`): **0.000**, IC95 [-0.075, +0.075]. Neutra en los
dos formatos: **v11 cerrada**.

### S-CAMPO-DUELO Resultado: no somos malos en duelos, somos malos contra snork Tree {#s-campo-duelo}

v5 en 1v1 estandar por HTTP contra los cuatro rivales del zoo, 20 bloques cada uno
(40 partidas), pinning de CPU:

| rival | duelos ganados | sin las partidas con timeout del rival |
|---|---:|---:|
| `jaxhodg` | 100% (40 de 40) | 100% |
| `hovering-hobbs` | 60% | **56%** |
| `snork-flood` | 52% | **54%** |
| `snork-tree` | 26% (de 80) | **25%** |

**Los seis experimentos del duelo se midieron contra el unico rival que nos saca una
distancia grande.** Contra los otros tres v5 esta entre el 54% y el 100%, y
`hovering-hobbs` -que en royale de cuatro nos gana el 64% (ver docs/experimentos-duelo.md#s-duelo)-
en el 1v1 puro pierde.

Consecuencias, y la primera es de metodo:

1. Un candidato que mejore contra el campo real puede salir NO CONCLUYENTE contra Tree si
   la distancia con Tree es demasiado grande para un termino suelto. Todo A/B de duelo a
   partir de aqui se mide contra un **campo de cuatro**, no contra una snake.
2. Los veredictos de v12 a v16 siguen siendo validos como lo que son -no mejoran contra
   Tree- y dejan de valer como «no mejoran el duelo».
3. Tree es un arbol minimax con su propia evaluacion; Flood es la misma base con un cerebro
   de flood fill y contra el vamos parejos. La distancia no es "snork", es el arbol.

### S-DERRUMBE Donde se pierde de verdad: 41 de 48 muertes son contra nuestro propio cuerpo {#s-derrumbe}

Los 56 duelos 1v1 perdidos contra `snork-tree` en el torneo de standard
(ver docs/experimentos.md#s-standard-r), mirados turno a turno.

**Como mueren:** longitud 28.5 contra 27.0 de Tree -somos mas largos-, **salud 90**,
**espacio libre 0** mientras Tree tiene 48. No es hambre ni cabezazo: nos quedamos sin
sitio con la salud intacta.

**No es el reparto del tablero.** En el primer turno en que las dos regiones dejan de
tocarse, las partidas ganadas y las perdidas son indistinguibles: region nuestra 50 contra
52, la de Tree 30 contra 28, y nos toca la MENOR en el 43% de las perdidas y el 41% de las
ganadas. El lado que nos toca no decide.

**Es un derrumbe de 20 turnos.** Espacio libre por turnos que faltan para el final:

| faltan | ganadas | perdidas | espacio/longitud (gan / per) |
|---:|---:|---:|---|
| 60 | 64 | 66 | 2.00 / **2.50** |
| 40 | 50 | 57 | 2.13 / 1.83 |
| 20 | 54 | 39 | 1.92 / 1.33 |
| 10 | 49 | 32 | 1.48 / **0.89** |
| 5 | 48 | 5 | 1.38 / 0.21 |
| 0 | 48 | 0 | 1.42 / 0.00 |

A 60 turnos del final **estamos mejor en las que perdemos**. El desplome pasa en los ultimos
20 turnos, cuando espacio/longitud cruza el 1.

**El banco de 48 posiciones** (`tests/posiciones-criticas`, extraidas con
`training-room/posiciones.py`) es el primer turno en que ese ratio baja de 1.2 y ya no se
recupera. Jugadas hasta el final con `bin/sonda_posiciones`:

| config | turnos de media | sobrevive |
|---|---:|---:|
| **v18, umbral 1.6** | **28.2** | **11 de 48** |
| v15 (supervivencia) | 27.0 | 10 |
| v16 (tabla) | 19.2 | 6 |
| v14 (trampa) | 18.0 | 4 |
| v5 (desplegada) | 17.8 | 3 |
| v13 (territorio duelo) | 17.6 | 4 |
| v11 (desesperacion) | 17.2 | 3 |

**Y la causa: 41 de las 48 muertes de v5 son `cuerpo_propio`.** Con la region libre del
tamaño del cuerpo, sobrevivir es recorrerla sin dejar huecos, y una evaluacion que cuenta
AREA no distingue una region recorrible de una que no lo es. La profundidad no lo arregla:
el cuerpo mide 28 y la busqueda llega a 17 niveles
(ver docs/experimentos-duelo.md#s-tabla-duelo-r).

**Lo que la sonda NO es:** aqui la rival la juega nuestro propio cerebro, no Tree, y la
posicion de partida ya esta perdida. Mide salir de estas posiciones, no ganar el torneo. El
veredicto sigue saliendo del torneo contra `gauntlet-v2`.

