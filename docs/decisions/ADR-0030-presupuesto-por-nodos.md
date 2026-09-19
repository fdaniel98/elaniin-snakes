---
title: Presupuesto por nodos para la arena, con el reloj de red de seguridad
read_when: "antes de tocar el corte de la busqueda, de calibrar budget_nodes o de montar un A/B en arena"
authority: derived
source: snake/src/search.cpp y docs/decisions/ADR-0023-cuando-parar-de-buscar.md
last_verified: 2026-09-19
size_bytes: 3452
---

# ADR-0030 — Presupuesto por nodos {#adr-0030}

## D-0300 Contexto {#d-0300}

La busqueda es *anytime*: profundiza mientras le quede reloj y devuelve la ultima
profundidad completada. Eso es lo correcto en partida real, donde el presupuesto es
tiempo, y es **inservible para medir**: la misma posicion con la maquina cargada devuelve
una profundidad menor y, a veces, otro movimiento.

En un A/B eso no es ruido que se promedie. Si la maquina va mas cargada mientras juega la
rama B -porque el arbitro, los contenedores de los rivales o cualquier otra cosa lo
decidieron-, B juega peor por un motivo que no es su evaluacion. El protocolo pareado
controla la semilla, los asientos y el campo, y se le colaria por debajo una variable que
no controla nadie.

## D-0301 Decision {#d-0301}

`search.budget_nodes` acota los nodos por movimiento. En 0 -el default, y lo que corre en
servidor- no hay tope y manda el reloj, como hasta ahora. Distinto de 0, la busqueda corta
al llegar al tope.

Los nodos se miran **antes** que el reloj. Con un tope de nodos y un deadline que no se
alcanza, el reloj no participa: `search()` es entonces funcion del estado y de los
parametros, y de nada mas.

**El reloj no se quita.** INV-11 dice que la busqueda no excede el deadline, y eso vale
tambien con tope de nodos puesto: un `budget_nodes` mal calibrado no puede convertirse en
un timeout en partida real. Lo que se hace es **marcarlo**: si quien corta es el reloj,
`SearchResult::corto_el_reloj` queda en cierto. La arena lo exige falso y aborta la
corrida si alguna vez es cierto.

Esa marca es el punto de la decision. La alternativa obvia -ignorar el reloj cuando hay
tope de nodos- daba la misma reproducibilidad y quitaba la red de seguridad. Y la otra
alternativa -no marcar nada- convierte una corrida sucia en una corrida que parece limpia,
que es peor que una que falla.

## D-0302 Calibracion {#d-0302}

Un numero de nodos **no es comparable entre commits que cambian el coste del nodo**. Se
calibra contra el presupuesto real de despliegue en la maquina de referencia -cuantos
nodos cabian en `time.max_compute_ms`- y se re-calibra despues de cada cambio de
rendimiento. Un A/B cuyas dos ramas usen calibraciones distintas no mide la evaluacion.

Queda pendiente hasta que la arena pueda medir throughput; hasta entonces `budget_nodes`
es 0 en todos los configs y no afecta a nada de lo ya medido.

## D-0303 Alternativas descartadas {#d-0303}

| Alternativa | Por que no |
|---|---|
| Presupuesto por reloj tambien en la arena | Es el problema, no la solucion: la carga de la maquina entra en el resultado |
| Profundidad fija en vez de nodos | Una profundidad no cuesta lo mismo con 2 vivas que con 4, ni en el medio juego que en el final. El presupuesto dejaria de parecerse al real |
| Ignorar el reloj cuando hay tope de nodos | Quita la red de INV-11 para ganar lo que la marca ya da |
| Contar nodos pero no marcar el corte | Una corrida sucia que parece limpia |

## D-0304 Estado {#d-0304}

**ACEPTADA.** Implementada; `budget_nodes` 0 por defecto, sin efecto en servidor.
Calibracion pendiente de la arena.
