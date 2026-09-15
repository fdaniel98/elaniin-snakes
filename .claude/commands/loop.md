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

1. Elige la clase segun lo que toque: por defecto 1=`correctness`, 2=`robustness`,
   3=`perf`, 4=`context`, pero si la iteracion anterior dejo un hallazgo mayor o blocker se
   **repite esa misma clase** sobre el commit del arreglo. Lo que exige el cierre son 3 clases
   distintas en todo el ledger, no en las tres primeras iteraciones
   (ver docs/decisions/ADR-0005-cierre-del-loop.md#d-0044).
2. Invoca al subagente de esa clase (`rules-auditor`, hilo principal, `perf-analyst`,
   `context-curator`). Pasale el diff, el log del gate y los artefactos, **nunca** tu
   resumen de tu propio trabajo: quien corrige no audita su clase.
3. Ejecuta `./scripts/loop.sh $1 $2`, que deja `i$1.log` y sus metricas.
4. Escribe el artefacto de la iteracion y actualiza el ledger.
5. **Espera aprobacion humana antes de aplicar arreglos.**

Recuerda: un hallazgo `blocker` obliga a repetir su clase sobre el commit del arreglo, y una
iteracion en la que el gate falla queda ANULADA y no cuenta para el minimo
(ver docs/harness.md#h-05).
