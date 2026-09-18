---
title: El arranque en frio se paga antes de jugar, no en el turno 0
read_when: "antes de tocar snake::warmup, el arranque del servidor, o los tests de deadline de brain_v0"
authority: derived
source: medicion propia en la maquina de referencia (WSL2, repo en /mnt/c)
last_verified: 2026-09-18
size_bytes: 4206
---

# ADR-0021 — El arranque en frio se paga antes de jugar, no en el turno 0 {#adr-0021}

## Contexto {#adr-0021-contexto}

El gate completo en la maquina de referencia fallo dos tests de deadline que en el
contenedor de desarrollo pasaban:

| build | test | medido |
|---|---|---|
| release | `deadline de 5 ms no se excede en ningun fixture` | **9 ms** en el PRIMER fixture (`01-spawn-turno0-apilado.json`); el resto, por debajo de 1 ms |
| debug (ASan+UBSan) | `fuzz: 10000 estados` | 2 violaciones sobre 10 000 (0,02 %) |

El primer numero no es el algoritmo. El mismo fixture, llamado por segunda vez, tarda
menos de un milisegundo: los 9 ms son traer a memoria las paginas de codigo del binario
—que en esa maquina vive en `/mnt/c`, el filesystem de Windows visto desde WSL2— y
resolver los simbolos la primera vez.

Lo importante es que **eso no es un artefacto del test**. En Cloud Run, una instancia
recien arrancada paga exactamente ese coste en su primer `/move`, que es el turno 0 de
una partida real. Con `min-instances` por debajo del numero de partidas simultaneas
(ver docs/decisions/ADR-0020-cloud-run.md) hay arranques en frio de verdad el dia del
torneo.

## Decision {#adr-0021-decision}

1. `snake::warmup(params)` ejecuta `decide()` y `decide_degraded()` sobre un estado
   sintetico con deadline holgado y devuelve los microsegundos que costo.
2. El servidor la llama **antes de `listen()`** y otra vez en **`POST /start`**, que es
   el unico momento de una partida en el que sobra tiempo. Ambas escriben `warmup_us=`
   en el log, asi que el coste real en produccion queda medido y no estimado.
3. Los tests de deadline llaman a `warmup()` primero, porque miden el presupuesto del
   ALGORITMO. El coste en frio pasa a tener **su propio test** con su propio umbral
   (`< 350 000 us`, el presupuesto entero de un movimiento): lo que se comprobaba antes
   de pasada ahora se comprueba a proposito.

En la misma tanda se tapo el agujero que hacia esto peligroso de verdad: el bucle de
evaluacion de `decide_impl` calculaba flood fill, cuellos y Voronoi para los cuatro
candidatos **sin mirar el reloj ni una vez**, y la comprobacion del bucle de puntuacion
llegaba DESPUES de `score_candidate`, que hace una BFS de hasta 121 turnos buscando
comida. Con v0 eso costaba poco y no se noto; con v1 o v2 encendidos es el bloque mas
caro de la funcion. Ahora el reloj se mira antes de cada candidato en las dos pasadas, y
si se acaba dentro de las heuristicas caras se puntua **todo** con v0 en vez de dejar a
unos candidatos con territorio y a otros con cero.

## Alternativas descartadas {#adr-0021-alternativas}

- **Subir el umbral del test de 5 ms a 15 ms.** Es lo que el proyecto llama arreglar el
  check en vez de la causa (regla de oro 4). Ademas habria escondido el dato que importa:
  que una instancia en frio llega tarde a su primer movimiento.
- **Quitar la medicion de reloj y comprobar solo que devuelve movimiento legal.** Pierde
  INV-11 entera.
- **Calentar dentro de `decide()` con un `static bool`.** Mete una rama y estado global en
  el hot path para resolver algo que ocurre una vez por proceso, y el primer request
  seguiria pagandolo.
- **Fijar las paginas con `mlockall`.** Resuelve el paginado pero no la resolucion de
  simbolos ni las cachés, necesita privilegios que el runtime distroless `nonroot` no
  tiene, y es mucho mas maquinaria que ejecutar la funcion una vez.

## Lo que queda abierto {#adr-0021-abierto}

Las 2 violaciones sobre 10 000 del fuzz en debug no las explica el arranque en frio: son
ruido del planificador con ASan en una maquina cargada. Si tras este cambio siguen
apareciendo, la pregunta es si un test de reloj de 5 ms puede ser determinista en esa
maquina, y la respuesta **no** es bajarlo en silencio: seria un ADR propio, con aprobacion
humana, midiendo antes cuantas violaciones da la maquina en reposo.
