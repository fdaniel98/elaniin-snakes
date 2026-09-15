---
title: Glosario del dominio
read_when: "cuando aparece un termino del dominio que no reconoces"
authority: derived
last_verified: 2026-09-15
size_bytes: 3066
---

Termino, definicion y donde vive en el codigo. Las reglas del juego no se enuncian aqui:
se enlazan a su anchor en `docs/rules.md`, que es su unico dueño.

## G-01 Terminos del juego {#g-01}

| Termino | Definicion | Donde vive |
|---|---|---|
| Cola apilada | Dos ultimos segmentos en la misma casilla, asi que la cola no se libera (ver docs/rules.md#r-04) | `SnakeBody::tail_is_stacked` |
| Cuello | Segundo segmento del cuerpo; chocar con el es autocolision, no una regla aparte | `SnakeBody::neck` |
| Tail-chasing | Seguir la cola propia para sobrevivir en espacio cerrado | tail-escape en `snake/src/brain_v0.cpp` |
| Zona de cabeza | Casillas a las que la cabeza rival puede llegar el proximo turno | `head_zone()` en `snake/src/brain_v0.cpp` |
| Cabeza a cabeza | Dos cabezas en la misma casilla; pierde la de longitud menor o igual (ver docs/rules.md#r-08) | `apply()` |
| Hazard | Casilla que resta salud al terminar el turno en ella (ver docs/rules.md#r-06) | `GameState::hazards` |
| Shrink | Evento que encoge por un borde el rectangulo seguro de royale (ver docs/rules.md#r-09) | `royale_hazards()`, fase 1 |
| Movimiento por defecto | El que aplica el motor si la respuesta es invalida o ausente (ver docs/rules.md#r-03) | `default_move()` |
| Placement | Posicion final; los empates comparten rango promediado (ver docs/rules.md#r-12) | `placements()` |
| Modo degradado | Cerebro sin tail-escape ni modelo de hazards para variantes no soportadas | `decide_degraded()` |

## G-02 Terminos de ingenieria {#g-02}

| Termino | Definicion | Donde vive |
|---|---|---|
| Bitboard | Tablero como bits de `uint64_t`; la casilla `(x,y)` es el bit `y*W+x` | `engine/include/engine/bitboard.hpp` |
| Ring buffer | Cuerpo circular: avanzar la cabeza es O(1) y no mueve memoria | `SnakeBody` |
| Copy-make | Copiar el estado y mutar la copia, en vez de deshacer movimientos | `apply()` |
| Flood fill | Espacio alcanzable por dilatacion sucesiva del bitboard | `snake/include/snake/eval/floodfill.hpp` |
| Punto de articulacion | Casilla que, al ocuparse, parte una region en dos | fase 5, v1 |
| Fail-safe | Cadena de cuatro escalones de degradacion de la decision | `decide()` |
| Deadline | Instante limite de computo: `timeout - margen_red - margen_seguridad` | `snake/include/snake/deadline.hpp` |
| Gate | Oraculo unico de "hecho": un comando con codigo de salida | `scripts/gate.sh` |
| Veneno | Cambio que DEBE hacer fallar un check concreto del gate | `scripts/gate-selftest.sh` |
| Ledger del loop | JSON con iteraciones, hallazgos y commits de un entregable | `.loop/<fase>/<slug>.ledger.json` |
| Mutante | Cambio de una linea que los tests deben detectar | `scripts/mutants.sh` |
| Bloque (A/B) | Unidad de analisis: una semilla por una rotacion de asientos | fase 4 |
| Campo congelado | Lista versionada de rivales y composiciones, con digest fijo | `gauntlet-v1.json`, fase 3 |
