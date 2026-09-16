---
title: "ADR-0008: el loop se aplica a engine/ y snake/, no al harness ni a la doc"
read_when: "al declarar un entregable con loop o al discutir cuanto proceso merece una tarea"
authority: canonical
last_verified: 2026-09-15
size_bytes: 3404
---


## D-0070 Contexto {#d-0070}

La fase 0 aplico el loop a tres entregables: `engine/src/rules.cpp`,
`snake/src/brain_v0.cpp` y `scripts/gate.sh`. Los dos primeros salieron baratos y
encontraron defectos de los que cuestan partidas. El tercero consumio ocho iteraciones y
las dos unicas decisiones que hubo que escalar al humano para cerrar la fase; ninguna de
las dos era sobre la snake, sino sobre contradicciones internas de las reglas del loop
aplicadas a documentos:
ver docs/decisions/ADR-0005-cierre-del-loop.md#d-0044 y
ver docs/decisions/ADR-0007-umbrales-por-clase.md#d-0060.

El proceso tiene que costar menos que lo que protege. Con un `.md` no lo cumple.

## D-0071 Decision {#d-0071}

`config/loop.json` declara `closing.deliverable_scope: ["engine/", "snake/"]`. Un
entregable del bloque `loop-deliverables` de `STATE.md` cuyo archivo caiga fuera de esos
prefijos hace **fallar** el check 9, con el mensaje de que lo saque del bloque.

Fuera de ese ambito -harness, scripts, documentacion- la verificacion sigue siendo el
gate y sus venenos: front-matter, anchors, presupuestos, duplicados, formato, `clang-tidy`
y `gate-selftest.sh`. Lo que desaparece es el ledger, no la comprobacion.

Aprobado por el humano el 2026-09-15.

## D-0072 Alternativas descartadas {#d-0072}

| Alternativa | Por que no |
|---|---|
| Dejar el loop para todo | Es lo que hubo en la fase 0: la mayor parte del proceso se gasto en documentos, y las contradicciones que hubo que resolver no mejoraron ni una linea de la snake |
| Quitar el loop entero | Las iteraciones sobre `rules.cpp` y `brain_v0.cpp` encontraron defectos reales -un mutante superviviente, hallazgos de los sanitizers-. Donde un bug cuesta partidas, se paga |
| Dejarlo a criterio de quien abre la fase | Un ambito que se decide tarea a tarea no es un ambito: acaba siendo "lo que apeteciera ese dia", y el gate no puede comprobarlo |
| Escribirlo solo en la documentacion | Una regla que el gate no comprueba se incumple sin que nadie se entere |

## D-0073 Consecuencias {#d-0073}

- `scripts/gate.sh` sale del bloque `loop-deliverables`. Su ledger, cerrado con ocho
  iteraciones, se conserva en `.loop/0/` como historia de lo que encontro.
- `gate-selftest.sh` gana `poison_9d`: declarar un entregable fuera de ambito tiene que
  hacer fallar el check 9.
- Sacar un entregable del ambito dejo **inertes** tres venenos del check 9, que elegian a
  quien envenenar con `find .loop -name '*.ledger.json' | head -1`: en un checkout sobre
  NTFS eso devolvia el ledger del gate, que ya nadie verifica, y el gate pasaba con el
  veneno puesto. Ahora los tres leen el primer entregable del bloque `loop-deliverables`
  de `STATE.md`, que es exactamente lo que el check 9 mira. Lo encontro la autoprueba en
  la maquina de referencia; en otra maquina el mismo `find` devolvia otro orden y los tres
  pasaban.
- Ampliar el ambito -meter `arena/` o `training-room/` cuando existan- es editar
  `deliverable_scope`, con la aprobacion y el ADR que exige la regla de oro 10.
- La DoD de cada fase sigue exigiendo ledger `CLOSED` para sus entregables; lo que cambia
  es cuales son entregables con loop.
