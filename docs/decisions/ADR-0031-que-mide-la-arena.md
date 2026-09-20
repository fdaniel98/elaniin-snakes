---
title: Que mide la arena, y que no
read_when: "antes de leer un veredicto de arena o de decidir que entra en default.json"
authority: derived
source: arena/src/arena.cpp y docs/experimentos.md
last_verified: 2026-09-20
size_bytes: 3802
---

# ADR-0031 — Que mide la arena, y que no {#adr-0031}

## D-0310 Contexto {#d-0310}

La arena se construyo para que un A/B deje de costar horas. Antes de usarla conviene fijar
que significa exactamente uno de sus veredictos, porque la tentacion de tratarlo como el
del gauntlet va a existir el dia que uno de los dos diga lo que queremos oir.

## D-0311 Lo que la arena NO mide {#d-0311}

**En la arena solo juegan configuraciones nuestras.** Las tres snakes de `gauntlet-v1` son
contenedores que hablan HTTP; su codigo no entra en nuestro binario, y ademas copiarlo esta
descartado por decision del proyecto. Un veredicto de arena es sobre **self-play**.

Eso no es un detalle de implementacion. En un juego de cuatro la fuerza no es un orden
total -hay ciclos y hay efectos kingmaker-, asi que un veredicto **solo vale contra el
campo declarado** (ver docs/experimentos.md#exp-resumen). El campo de la arena somos
nosotros mismos, y una configuracion puede ganarse a si misma por explotar un habito
propio que ningun rival real tiene.

**Regla, y no se negocia: nada entra en `default.json` por un veredicto de arena.** La
puerta sigue siendo el A/B por HTTP contra `gauntlet-v1`, con el protocolo de bloques
pareados. La arena sirve para dos cosas:

1. **Triaje.** Descartar barato una idea que ni siquiera se gana a si misma, antes de
   gastarle dos horas de gauntlet. Un `NO` de arena ahorra tiempo; un `SI` de arena no
   concluye nada.
2. **Afinado.** Optimizar los ~20 pesos de `snake/config/default.json`, que nunca se han
   tocado, necesita miles de partidas y es imposible por HTTP. Lo que salga de ahi es una
   **propuesta**, y la propuesta pasa por el gauntlet como cualquier otra.

## D-0312 El segundo aviso: el presupuesto {#d-0312}

La arena es rapida porque se juega con **menos presupuesto por movimiento**, no porque
quite el transporte: los numeros estan en docs/performance.md#p-08. A presupuesto
equivalente al del despliegue el ahorro baja a unas 4 veces.

Afinar a presupuesto corto es practica habitual en motores de juego y tiene su trampa
conocida: **lo que gana a 500 nodos no siempre gana a 20 000**. Un termino que compensa una
busqueda superficial puede estorbar cuando la busqueda ve lo suficiente por si sola. Por
eso el afinado propone y el gauntlet -que juega con el presupuesto real- dispone.

## D-0313 Lo que la arena SI mide, y bien {#d-0313}

Reproducibilidad. Misma semilla y mismos `Params` dan la misma partida en cualquier maquina
y bajo cualquier carga, con tope de nodos puesto
(ver docs/decisions/ADR-0030-presupuesto-por-nodos.md#d-0301). Eso permite comparar dos
ramas sobre exactamente el mismo tablero inicial y el mismo sorteo de comida por turno
(ver docs/decisions/ADR-0029-schedule-por-turno.md#d-0291), que es mas control del que
tenemos por HTTP, donde no hay reproducibilidad bit a bit.

## D-0314 Alternativas descartadas {#d-0314}

| Alternativa | Por que no |
|---|---|
| Meter las snakes del zoo en el binario | Su codigo es de otros; el proyecto escribe el suyo |
| Reimplementar de memoria una snake "parecida" a las del zoo | Seria un rival inventado por nosotros disfrazado de campo |
| Usar la arena como puerta y el gauntlet como confirmacion | Invierte el orden: la puerta la tiene que tener el campo, no el espejo |
| No construir la arena y seguir solo con HTTP | Deja el afinado de los pesos fuera de alcance para siempre |

## D-0315 Estado {#d-0315}

**ACEPTADA.** El aviso vive tambien en la cabecera de `arena/include/arena/arena.hpp`,
que es donde lo va a leer quien la use.
