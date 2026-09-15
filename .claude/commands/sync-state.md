---
description: Actualiza STATE.md con gate, loop y el bloque perf regenerado
argument-hint: ""
allowed-tools: Bash, Read, Edit
---

Gate (rapido, solo para el estado; no cierra fase):

!`./scripts/gate.sh --fast 2>&1 | tail -15`

Estado del loop:

!`./scripts/loop_verify.sh --fast 2>&1 | tail -15`

Haz esto, en este orden:

1. Actualiza a mano en `STATE.md`: fase, resultado del gate con fecha y commit, estado del
   loop por entregable, hallazgos abiertos y la siguiente accion concreta.
2. Ejecuta `./scripts/sync_state.sh` para regenerar el bloque `perf-snapshot` desde
   `docs/performance.md`. **No escribas numeros a mano en STATE.md**: el gate lo detecta.
3. Ejecuta `./scripts/docs_meta.sh --fix` si tocaste algo bajo `docs/`.
4. La "siguiente accion concreta" tiene que ser una frase ejecutable, no un proposito.
