---
title: Los checks 4 y 5 pasan a cubrir arena/
read_when: "antes de añadir un modulo nuevo al arbol"
authority: derived
source: scripts/gate.sh
last_verified: 2026-09-20
size_bytes: 2259
---

# ADR-0035 — El ambito de los checks 4 y 5 {#adr-0035}

## D-0350 Contexto {#d-0350}

Los checks 4 (`clang-format`) y 5 (`clang-tidy`) no recorren el arbol: llevan una **lista
de directorios escrita a mano** -`engine snake tests bench` y `engine/src snake/src`-.
`arena/` nacio en la fase 4 y quedo fuera de las dos. Un modulo entero, con su bucle de
partida, sin formatear ni revisar, y el gate en verde.

Es la regla de oro 4 en su forma literal: el gate pasaba y algo estaba mal, asi que el bug
estaba en el gate.

## D-0351 Decision {#d-0351}

`arena` entra en los dos checks. **Endurece**, que es lo que la regla 4 autoriza; lo que
exige aprobacion humana es reducir.

Y se añade `poison_4b`, que desformatea `arena/src/arena.cpp`. El veneno que ya existia
desformatea `engine/src/rules.cpp` y prueba el CHECK; no prueba su **ambito**. Sin el
veneno nuevo, ampliar la lista seria una afirmacion mia en vez de un hecho comprobado, que
es exactamente lo que el selftest existe para evitar.

## D-0352 Lo que queda fuera, a sabiendas {#d-0352}

`tools/` sigue sin formatear ni revisar, y se nota: `tools/causas.cpp` tiene decenas de
diferencias con `clang-format`. No entra ahora porque meterlo obliga a reformatear un
fichero que no tiene nada que ver con esta fase, y eso no va en este commit. Queda como
hallazgo abierto en `STATE.md`.

La leccion, que es la unica parte de esto que sirve para el futuro: **una lista de
directorios escrita a mano envejece en silencio**. Cada modulo nuevo del arbol de §5 tiene
que entrar en las dos listas el mismo dia, o repetiremos esto.

## D-0353 Alternativas descartadas {#d-0353}

| Alternativa | Por que no |
|---|---|
| Recorrer todo el arbol en vez de una lista | Arrastraria `third_party/`, que es codigo ajeno y no se reformatea |
| Añadir tambien `tools/` ahora | Mezcla en este commit el reformateo de un fichero que no toca esta fase |
| Dejarlo como estaba | El gate diria verde sobre un modulo que nadie revisa |

## D-0354 Estado {#d-0354}

**ACEPTADA.** Comprobado con `./scripts/gate-selftest.sh 4`.
