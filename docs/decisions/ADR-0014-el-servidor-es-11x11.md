---
title: "ADR-0014: el servidor juega 11x11 y lo dice, en vez de fingir que despacha por tamaño"
read_when: "antes de tocar el servidor o de dar por hecho que el cerebro sirve para cualquier tablero"
authority: canonical
last_verified: 2026-09-18
size_bytes: 2655
---


## D-0130 Contexto {#d-0130}

El motor esta instanciado en 7x7, 11x11 y 19x19 (ver docs/invariants.md#inv-05), pero el
cerebro no: `decide()` y `parse_state()` son `State11`. En un tablero de otro tamaño el
servidor no falla ni miente: cae al ultimo escalon del fail-safe y responde `up` en cada
turno, con su `WARN` en el log.

El prompt maestro dice otra cosa: que el servidor «despacha segun `board.width/height` y
cae al fail-safe con `WARN` si no hay instanciacion». Hoy no despacha, porque lo que falta
no es el despacho sino el cerebro para esos tamaños.

Y ningun fixture es de 7x7, asi que el gate nunca lo habria enseñado.

## D-0131 Decision {#d-0131}

El servidor soporta **11x11**, que es el formato objetivo. Cualquier otro tamaño entra en
el fail-safe con `WARN`, que es lo que ya hacia, ahora dicho en voz alta en el contrato
del modulo y probado por el check 8 con un payload de 19x19 entre los adversos.

Decidido por el humano el 2026-09-18: **concentrarse en 11x11**.

Esto **corrige el §8.1 del prompt maestro**, que no es canonico por estar escrito
(regla de oro 9). Templatizar el cerebro queda para la fase 4, que es cuando la arena
necesitara de verdad otros tamaños; hacerlo ahora obligaria a tocar `brain_v0.cpp`, que es
la referencia fija contra la que se mide todo lo demas.

## D-0132 Alternativas descartadas {#d-0132}

| Alternativa | Por que no |
|---|---|
| Templatizar el cerebro ya, para 7, 11 y 19 | Es el grueso del trabajo de una fase entera para tableros que solo aparecen en la arena y en los tests, y toca el archivo que `snake/CLAUDE.md` declara intocable |
| Calcular un movimiento legal con el motor para otros tamaños | Mas honesto que `up` a ciegas, pero sigue siendo una snake que no juega: gasta codigo en parecer que soporta algo que no soporta |
| Dejarlo sin documentar | Es lo que habia: un contrato que dice una cosa y un servidor que hace otra, sin ningun fixture que lo enseñe |

## D-0133 Consecuencias {#d-0133}

- `snake/CLAUDE.md` lo dice en el contrato del servidor, y deja de heredar la promesa del
  §8.1 que no se cumple.
- El check 8 manda un tablero de 19x19 entre sus payloads adversos: exige 200 con un
  movimiento de las cuatro literales, no que juegue bien.
- La fase 4 hereda el trabajo: si la arena quiere 7x7 o 19x19, templatizar el cerebro es
  parte de su alcance, no un descubrimiento a mitad.
