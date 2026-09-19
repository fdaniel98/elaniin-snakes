---
title: Experimentos de estrategia, medidos
read_when: "antes de proponer una heuristica o una version nueva: aqui esta lo que ya se probo y que dio"
authority: derived
source: docs/results/torneo-* y training-room/compara.py
last_verified: 2026-09-19
size_bytes: 4288
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

