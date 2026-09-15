---
description: Ejecuta una iteracion del loop sobre un entregable, o imprime su estado
argument-hint: "<n> <slug> | status [slug]"
allowed-tools: Bash, Read, Grep, Glob, Task
---

Argumentos: `$ARGUMENTS`

Si el primer argumento es `status`:

!`./scripts/loop_verify.sh --fast 2>&1 | tail -30`

Imprime la tabla de iteraciones por entregable, los hallazgos abiertos por severidad y
que falta exactamente para cerrar.

Si el primer argumento es un numero, ejecuta la iteracion `$1` sobre el entregable `$2`:

1. Determina la clase: 1=`correctness`, 2=`robustness`, 3=`perf`, 4=`context`.
2. Invoca al subagente de esa clase (`rules-auditor`, hilo principal, `perf-analyst`,
   `context-curator`). Pasale el diff, el log del gate y los artefactos, **nunca** tu
   resumen de tu propio trabajo: quien corrige no audita su clase.
3. Ejecuta `./scripts/loop.sh $1 $2`, que deja `i$1.log` y sus metricas.
4. Escribe el artefacto de la iteracion y actualiza el ledger.
5. **Espera aprobacion humana antes de aplicar arreglos.**

Recuerda: un hallazgo `blocker` reinicia el conteo a 0 y obliga a repetir las 3 clases.
