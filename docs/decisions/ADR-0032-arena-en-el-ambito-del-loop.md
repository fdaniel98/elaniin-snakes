---
title: La arena entra en el ambito del loop
read_when: "antes de tocar config/loop.json o de decidir que entregable lleva loop"
authority: derived
source: docs/decisions/ADR-0008-ambito-del-loop.md y ADR-0017
last_verified: 2026-09-20
size_bytes: 2674
---

# ADR-0032 — La arena entra en el ambito del loop {#adr-0032}

## D-0320 Contexto {#d-0320}

`config/loop.json` lista en `closing.deliverable_scope` que directorios llevan loop
obligatorio: `engine/`, `snake/` y `training-room/`. `arena/` no estaba, porque cuando se
escribio esa lista la arena era un README.

Declarar `arena` como entregable de la fase 4 en `STATE.md` con la lista sin tocar hace
fallar el check 9, y con razon: el lint no se inventa que un directorio nuevo deba llevar
loop.

## D-0321 Decision {#d-0321}

`arena/` entra en `deliverable_scope`. El motivo es el mismo que llevo `training-room/` a
la lista (ver docs/decisions/ADR-0017-el-instrumento-lleva-loop.md#d-0161): **el
instrumento con el que se mide lleva loop**. Una arena con un defecto no produce una
medicion mala y visible, produce una medicion creible y equivocada, que es peor.

Se añade tambien su bloque en `applicability`. Tres metricas de `robustness` y tres de
`perf` no aplican a la arena y se declara por que, una por una, como exige ADR-0019: no
decide movimientos -se los pide a `decide()`-, no responde peticiones HTTP, y su
presupuesto es por nodos con un deadline inalcanzable a proposito, asi que "violaciones
de deadline" no significa nada aqui. Su metrica propia es `partidas_por_minuto`.

Esto **endurece** el gate: hay un entregable mas que no puede cerrarse sin ledger. Es lo
que la regla de oro 4 autoriza; lo que exige aprobacion humana es lo contrario.

## D-0322 Consecuencia inmediata {#d-0322}

Mientras la fase 4 este en curso y `.loop/4/arena.ledger.json` no exista, **el check 9
falla y el gate esta en rojo**. Es el estado correcto: §11 dice que una fase no termina
sin ledger `CLOSED` por entregable, y el gate lo esta diciendo. No se apaga el check ni se
quita el entregable de `STATE.md` para ponerlo en verde: eso seria relajar el gate, que es
justo lo que la regla de oro 10 prohibe sin aprobacion.

## D-0323 Alternativas descartadas {#d-0323}

| Alternativa | Por que no |
|---|---|
| Dejar `arena/` fuera del ambito | El instrumento que mide sin loop es como el gate sin selftest |
| Quitar `arena` de los entregables de `STATE.md` | Pone el check en verde mintiendo sobre lo que falta |
| Meter `arena/` dentro de `snake/` para heredar el ambito | Mueve codigo para esquivar un lint |

## D-0324 Estado {#d-0324}

**ACEPTADA**, pendiente de que el loop de la fase 4 se abra y se cierre.
