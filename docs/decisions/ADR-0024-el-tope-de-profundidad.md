---
title: El tope de profundidad sube de 8 a 64, porque 8 era un freno de mano
read_when: "antes de tocar search.max_depth o de discutir por que la busqueda no llega mas hondo"
authority: derived
source: tools/sonda_tope.cpp, 40 posiciones por escenario, presupuesto 200 ms
last_verified: 2026-09-18
size_bytes: 3159
---

# ADR-0024 — El tope de profundidad sube de 8 a 64 {#adr-0024}

## Contexto {#adr-0024-contexto}

`search.max_depth` se puso en 8 al escribir la busqueda, sin medir: un numero redondo
puesto como tope de seguridad. La sonda que mide cuanto tiempo deja sin usar dice esto,
con el presupuesto ya reducido a 200 ms (ver docs/decisions/ADR-0023-presupuesto-de-computo.md):

| vivas | tope 8 | tope 64 | tiempo usado con tope 8 |
|---|---|---|---|
| **2** | **8.00** | **12.18** | **42 ms de 200** |
| 3 | 5.95 | 7.47 | 193 ms de 200 |
| 4 | 6.03 | 12.08 | 176 ms de 200 |

La fila que importa es la primera. **Con dos serpientes vivas, las 39 posiciones de la
muestra tocaban el tope y se devolvian 158 de los 200 ms sin usar.** El final de dos es
exactamente donde se decide el primer puesto contra el segundo, y ahi la snake pensaba un
quinto de lo que podia.

Con cuatro vivas el tope tambien mordia: 6.03 contra 12.08 niveles, **al mismo coste en
tiempo** (176 ms en los dos casos). Las ramas en las que alguien muere terminan pronto, asi
que profundizar en ellas es casi gratis; el tope las cortaba igual que a las caras.

## Decision {#adr-0024-decision}

`search.max_depth` pasa de **8 a 64**.

El coste en tiempo es **cero**: quien corta sigue siendo el deadline, y la busqueda sigue
devolviendo la mejor jugada de la ultima profundidad COMPLETADA. Lo unico que cambia es
que deja de plantarse antes de que suene el reloj.

**Pila:** `sizeof(engine::State11)` son 1096 bytes y cada nivel usa ~2.4 KB (el estado por
valor de `negamax` mas la copia de `peor_respuesta`), o sea **~154 KB a profundidad 64**
sobre los 8 MB de una pila normal. Con margen de sobra, y el numero esta aqui para que
quien suba el tope otra vez rehaga la cuenta.

## Alternativas descartadas {#adr-0024-alternativas}

- **Dejarlo en 8.** Es regalar el 79 % del presupuesto en los finales de dos.
- **Quitar el tope.** Un tope de seguridad tiene que existir: sin el, un bug en la
  condicion de parada se convierte en un desbordamiento de pila en produccion en vez de en
  un movimiento tardio.
- **Un tope distinto segun cuantas vivas queden.** Suena mas fino y no hace falta: el
  deadline ya diferencia solo. Añadir la rama seria complejidad sin numero que la respalde.
- **128 o mas.** La sonda no midio mas alla de 64 y 64 ya deja el deadline como unico
  limitante en las tres muestras. Subirlo sin medir seria repetir el error que este ADR
  arregla.

## Verificacion {#adr-0024-verificacion}

Dos tests nuevos:

1. con presupuestos de 1, 5, 50 y 200 ms sobre los 15 fixtures, el deadline se respeta y el
   movimiento es legal — o sea que subir el tope no rompe INV-11 ni la pila;
2. un final de dos con 200 ms tiene que pasar de profundidad 8. Con el tope viejo ese test
   daba exactamente 8, que es la regresion que este ADR evita que vuelva.
