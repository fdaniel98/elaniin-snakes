---
title: "ADR-0012: la cola limpia del loop baja de dos iteraciones a una"
read_when: "antes de cerrar un loop, y antes de volver a tocar config/loop.json"
authority: canonical
last_verified: 2026-09-17
size_bytes: 3011
---


## D-0110 Contexto {#d-0110}

El loop de `engine/src/rules.cpp` llego al techo de 8 iteraciones sin cerrar, y no por
falta de trabajo: por la forma de la regla.

El criterio 14 de la DoD obliga a auditar el propio trabajo con `rules-auditor` y
`context-curator` **antes** de declarar la fase completa. Esas dos auditorias son, por
construccion, las ultimas iteraciones del loop. Si encuentran algo -y encontraron once
hallazgos reales, entre ellos que el replay rechazaba un movimiento que el arbitro si
aplica- las dos ultimas iteraciones tienen hallazgos, y la condicion de cierre pide
exactamente lo contrario.

Para cerrar hacian falta dos rondas limpias mas, i9 e i10, por encima del techo. Es decir:
**cumplir el criterio 14 y cerrar el loop eran incompatibles** salvo que las auditorias no
encontraran nada, que es justo el caso en el que no sirven para nada.

## D-0111 Decision {#d-0111}

`clean_tail_iterations` pasa de **2 a 1** en `config/loop.json`.

Aprobado por el humano el 2026-09-17, enunciada la alternativa de subir el techo.

Esto **relaja** lo que se comprueba, al reves que la regla de oro 4, y por eso queda
escrito aqui y no en una nota: a partir de ahora un loop cierra con **una** iteracion final
sin hallazgos en vez de dos. Lo que NO cambia: el minimo de 3 iteraciones, las tres clases
obligatorias, el techo de 8, el reinicio por hallazgo `blocker` y que la iteracion final
siga siendo una auditoria con su artefacto y sus comandos.

## D-0112 Alternativas descartadas {#d-0112}

| Alternativa | Por que no |
|---|---|
| Subir el techo de 8 a 10 | Deja intacta la cola de dos, pero convierte el techo en un numero que se sube cada vez que estorba. El problema no era la cantidad de iteraciones: era que las auditorias obligatorias caen siempre al final |
| Dejar el ledger BLOQUEADO y cerrarlo a mano en la maquina de referencia | Aplaza la contradiccion a la fase siguiente, donde el criterio 14 vuelve a aplicarse igual |
| Correr las auditorias antes y no al final | El criterio 14 pide auditar el trabajo terminado. Auditar a mitad no es lo mismo, y obligaria a re-auditar despues de cada arreglo |
| Que los hallazgos de la auditoria no cuenten como hallazgos | Seria mentir en el ledger para que el check pase, que es exactamente lo que el antifraude persigue |

## D-0113 Consecuencias {#d-0113}

- El sha256 de `config/loop.json` cambia, asi que **todo ledger** tiene que copiar el
  nuevo, incluidos los de la fase 0.
- Un loop puede cerrar con una sola ronda limpia, asi que la iteracion final pesa mas que
  antes: si esa auditoria es floja, ya no hay una segunda que la respalde.
- El riesgo queda acotado por lo que no se toco: el gate completo en verde sigue siendo
  necesario para cerrar fase, y las tres clases obligatorias siguen ahi.
