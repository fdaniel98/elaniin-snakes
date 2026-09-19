---
title: La longitud deja de ser un adorno y pasa a ser la ventaja sobre el rival mas largo
read_when: "antes de tocar la politica de comida, el peso de la longitud, o de discutir por que la snake no come"
authority: derived
source: docs/results/torneo-v4-hojas (60 partidas, analisis de los JSONL)
last_verified: 2026-09-19
size_bytes: 4001
---

# ADR-0026 — La longitud pasa a ser la ventaja sobre el rival mas largo {#adr-0026}

## Contexto {#adr-0026-contexto}

v4 gano su A/B y sigue quedando tercera 30 de 60 veces. El analisis de sus JSONL dice por
que, y no es lo que el diagnostico de v0 decia:

- morimos en el turno **180** de media;
- en **47 de 60 partidas**, todos los rivales vivos eran iguales o mas largos que nosotros;
- solo en 15 eramos los mas largos.

La relacion con el puesto es monotona:

| puesto | partidas | ventaja de longitud media |
|---|---|---|
| 1o | 8 | **-0.42** |
| 2o | 21 | -1.17 |
| 3o | 30 | **-1.61** |

Agrupando: 11 partidas siendo mas largos de media dan puesto **2.00**; 49 siendo mas cortos
dan **2.49**. Esa brecha de 0.49 es **mayor que toda la mejora de v4 sobre v0** (0.367).

**El mecanismo es nuestro codigo, no la estadistica.** Siendo mas cortos perdemos todos los
cabezazos, y `evaluate()` penaliza quedar adyacente a cualquier cabeza igual o mas larga:
ser corto **encoge el espacio que consideramos seguro**. Nos encerramos solos.

La causa es heredada: la politica de comida es la de v0 -«solo si la salud baja del umbral;
no comer por comer»- y el peso de la longitud era `mi_largo * 2.0`, simbolico.

## Decision {#adr-0026-decision}

Con `length.version >= 1`:

1. **Lo que puntua es la VENTAJA**, `mi_largo - largo_del_rival_mas_largo`, no la longitud
   absoluta. En un juego de cuatro, ser largo no sirve de nada si el de al lado es mas
   largo.
2. **El termino SATURA** en `target_lead` (3 por defecto). Un cuerpo enorme tambien
   encierra; sin saturacion la snake preferiria crecer siempre, que es el error contrario
   al que esto arregla.
3. **La comida atrae cuando vamos cortos**, no solo con hambre, con su propio peso
   (`hunt_weight`) separado del de la salud.

El peso de la longitud absoluta (`* 2.0`) se conserva **solo** para `version = 0`, que es
v0 y no se toca.

## Por que ahora y no antes {#adr-0026-por-que-ahora}

«No comer por comer» era razonable para una snake que decidia un turno: ir a por comida es
meterse en un sitio, y sin mirar adelante eso es un riesgo a ojo. Con una busqueda que ve
diez turnos, **el arbol ya calcula si esa comida te mete en un callejon**. Crecer deja de
ser una apuesta ciega y pasa a ser algo evaluable, que es exactamente el tipo de decision
que la busqueda existe para tomar.

## Coste, medido {#adr-0026-coste}

Cero. La BFS de comida pasa a ejecutarse en muchas mas hojas, y la profundidad media sale
igual (`tools/sonda_tope.cpp`, presupuesto 200 ms):

| vivas | sin control de longitud | con control |
|---|---|---|
| 2 | 10.18 | 10.18 |
| 3 | 6.11 | 6.08 |
| 4 | 10.89 | 10.89 |

## El matiz honesto {#adr-0026-matiz}

**Es correlacion.** Una snake que va ganando come mas porque controla mas espacio, asi que
la longitud puede ser sintoma y no causa. Lo que inclina la balanza hacia causa es el
mecanismo del contexto -que esta en nuestro codigo- y no el numero. Si el A/B sale NO
CONCLUYENTE, la lectura correcta es que era sintoma, y eso tambien es informacion.

## Alternativas descartadas {#adr-0026-alternativas}

- **Subir el peso de la longitud absoluta.** No distingue ir por delante de ir por detras,
  que es lo unico que decide un cabezazo.
- **Bajar `food.seek_below` para comer antes.** Sigue siendo una regla ciega por salud;
  no mira si vamos ganando o perdiendo la carrera de longitud.
- **Termino sin saturacion.** Convierte la snake en glotona y el cuerpo largo en trampa.

## Estado {#adr-0026-estado}

**SIN MEDIR.** Se enciende con `snake/config/v5-longitud.json` y se mide CONTRA v4, con el
protocolo de ver docs/experimentos.md#exp-resumen.
