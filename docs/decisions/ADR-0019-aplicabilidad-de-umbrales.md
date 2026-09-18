---
title: "ADR-0019: un umbral se cumple o se declara inaplicable; omitirlo deja de ser una salida"
read_when: "antes de escribir un ledger de un entregable que no sea C++ de ruta caliente"
authority: canonical
last_verified: 2026-09-18
size_bytes: 4162
---


## D-0180 Contexto {#d-0180}

Los umbrales de `config/loop.json` se escribieron en la fase 0 para `engine/` y `snake/`:
escalones del fail-safe, p99 de `/move`, asignaciones en la ruta caliente, mutantes del
motor. Con la ADR-0017 entro `training-room/`, que es un orquestador de Python y no tiene
ruta caliente ni fail-safe de cuatro escalones.

Al ir a escribir su ledger aparecio algo peor que la falta de encaje. `verify_metrics`
comprueba cada umbral **solo si la metrica esta presente**:

```
if $t.p99_move_ms_max != null and $m.p99_move_ms != null and $m.p99_move_ms > ...
```

O sea que la forma comoda de cerrar cualquier loop era no reportar la metrica incomoda.
No es un agujero de este entregable: lo tenian todos, desde la fase 0, y ninguno de los
venenos lo cubria. El gate llevaba tres fases en verde con esa puerta abierta.

## D-0181 Decision {#d-0181}

Cada clase declara en `config/loop.json` sus `metricas_exigidas`. El check 9 pasa a
exigir, para cada iteracion de una clase obligatoria, que **toda** metrica exigida este:

- presente en `metrics`, y entonces se compara contra su umbral como hasta ahora; o
- declarada inaplicable para ese entregable en `applicability.<slug>.<clase>.no_aplica`,
  **con un motivo no vacio**.

Omitirla sin declararla hace fallar el check 9. Un entregable puede ademas exigir
metricas propias con `metricas_extra`: `training-room` exige `partidas_por_minuto`,
que es lo que la ADR-0018 dice que mide su clase `perf`.

Aprobado por el humano el 2026-09-18, enunciadas las alternativas de reinterpretar las
metricas existentes y de sacar el entregable del ambito.

## D-0182 Alternativas descartadas {#d-0182}

| Alternativa | Por que no |
|---|---|
| Reinterpretar las metricas sin tocar el config: reportar partidas por minuto en el campo `p99_move_ms` | Dos cosas distintas con el mismo nombre. Cualquiera que compare ledgers entre entregables leeria milisegundos donde hay partidas por minuto, y el agujero de la omision seguiria abierto |
| Sacar `training-room/` del ambito | Revertir la ADR-0017 por comodidad. El instrumento es justo donde un bug miente en silencio (ver docs/decisions/ADR-0017-el-instrumento-lleva-loop.md#d-0160) |
| Umbrales distintos por entregable, sin declarar nada | Un umbral por entregable sin motivo escrito es un umbral que se ajusta hasta que pase. La declaracion obliga a escribir POR QUE no aplica, y eso se puede discutir |
| Dejar la omision como esta | Es la regla de oro 4 al reves: el gate en verde con una puerta por la que se sale sin ser visto |

## D-0183 Consecuencias {#d-0183}

- El check 9 se **endurece** para todos los entregables, no solo para el nuevo. Aplicado a
  los ledgers ya cerrados, **cuatro de los cinco no pasaban**: el agujero no era teorico,
  se uso sin querer desde la fase 0. Tres se resuelven declarando aplicabilidad, porque lo
  que faltaba de verdad no aplicaba -el p99 de `/move` no es una metrica del motor ni del
  cerebro, y `scripts/gate.sh` es un script de shell sin fixtures ni ruta caliente.
- El cuarto **no** se resuelve declarando nada: el ledger de `snake/src/server.cpp`
  (fase 2) no reporta `fuzz_states`, y fuzzear payloads HTTP si aplica a un servidor. Su
  iteracion de `robustness` cerro con 42 payloads adversos, no con los 10 000 estados que
  pide el umbral, y el umbral no salto porque la metrica no estaba. Queda anotado en
  `STATE.md` como hallazgo abierto: el gate estuvo en verde sobre una fase declarada
  COMPLETA a la que le faltaba una comprobacion de su propia DoD.
- El check 9 solo verifica los entregables de la fase activa, asi que el gate no falla hoy
  por esto. Que no falle no lo convierte en resuelto.
- `gate-selftest.sh` gana un veneno: quitar una metrica exigida de un ledger, sin
  declararla inaplicable, tiene que hacer fallar el check 9.
- Escribir `no_aplica` es una decision visible en el diff, no un descuido invisible.
