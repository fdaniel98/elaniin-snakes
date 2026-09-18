---
title: "ADR-0017: el Training Room entra al ambito del loop porque un instrumento roto miente"
read_when: "antes de tocar closing.deliverable_scope o de decidir si un entregable lleva ledger"
authority: canonical
last_verified: 2026-09-18
size_bytes: 3618
---


## D-0160 Contexto {#d-0160}

`closing.deliverable_scope` son `engine/` y `snake/`
(ver docs/decisions/ADR-0008-ambito-del-loop.md#d-0071). El criterio de aquella decision
fue: donde un bug cuesta partidas, se paga el loop; donde no, basta el gate y sus venenos.

La fase 3 no toca ninguno de los dos prefijos, y `loop_verify.sh` falla con el bloque
`loop-deliverables` vacio, que es como se escribio a proposito para que ninguna fase cierre
por no declarar nada (ver §14.7 del prompt maestro). O sea: la fase 3 no puede cerrarse sin
tocar este ambito.

Lo que la fase 2 enseño es que el criterio de la ADR-0008 estaba incompleto. Dos de sus
hallazgos no fueron bugs de la snake, sino del instrumento que la medía:

- `soak.sh` informaba «0 timeouts» habiendo jugado **cero** partidas, porque contaba
  timeouts sin comprobar que hubiera partidas que contar.
- `mutants.sh` restauraba los archivos con `mv`, que conserva el `mtime` anterior: una
  mutacion en una cabecera se quedaba dentro del binario y «mataba» a todos los mutantes
  posteriores. El ratio real era 0.6364 y el publicado 1.0, y hubo que re-medir la fase 0.

Los dos los encontro el loop. El gate estaba en verde en ambos casos, porque un instrumento
que miente produce exactamente la salida que el gate espera.

## D-0161 Decision {#d-0161}

`closing.deliverable_scope` pasa a `["engine/", "snake/", "training-room/"]`.

El criterio de la ADR-0008 se amplia, y queda enunciado asi: **lleva loop lo que, roto,
produce una conclusion falsa sin producir un fallo**. El motor y el cerebro, porque un bug
ahi cuesta partidas. El Training Room, porque un bug ahi hace aceptar una estrategia peor
creyendo que mejoro, y eso contamina todas las decisiones que vengan despues.

`zoo/` y `scripts/zoo.sh` se quedan **fuera**. Son sobre todo invocaciones de `docker` con
sus banderas de aislamiento: cuando fallan, fallan ruidosamente y el gate lo ve.

Aprobado por el humano el 2026-09-18, enunciada la alternativa de incluir tambien el zoo.

## D-0162 Alternativas descartadas {#d-0162}

| Alternativa | Por que no |
|---|---|
| Incluir tambien `zoo/` | Mas cobertura, pero el zoo no puede mentir en silencio: un contenedor que no arranca no arranca. El criterio de la ADR-0008 sigue aplicandose ahi |
| No extender el ambito y relajar `loop_verify.sh` para que acepte el bloque vacio | Es debilitar el gate para que una fase pase, que es la regla de oro 4 al reves. El bloque vacio se rechaza precisamente para que «no declare nada» no sea una salida |
| Decidirlo entregable a entregable | Ya descartado en ver docs/decisions/ADR-0008-ambito-del-loop.md#d-0072: un ambito que se decide cada vez no es un ambito, y el gate no puede comprobarlo |

## D-0163 Consecuencias {#d-0163}

- La fase 3 declara `training-room` como entregable con loop, con las cuatro clases:
  `correctness`, `robustness`, `perf` y `context` -esta ultima porque publica numeros.
- `thresholds_sha256` de todo ledger nuevo cambia, porque `config/loop.json` cambia. Los
  ledgers ya cerrados de las fases 0, 1 y 2 conservan el suyo: apuntan al archivo que
  estaba vigente cuando se cerraron, que es lo que el antifraude 8 comprueba.
- `gate-selftest.sh` no necesita veneno nuevo: `poison_9d` ya comprueba que declarar un
  entregable fuera de ambito hace fallar el check 9, y sigue siendo cierto con el zoo.
