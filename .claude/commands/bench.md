---
description: Benchmark y comparacion contra la linea base publicada
argument-hint: "[filtro de benchmark]"
allowed-tools: Bash, Read, Edit
---

Linea base actual:

!`sed -n '/BEGIN:perf-canonical/,/END:perf-canonical/p' docs/performance.md`

Resultado de esta corrida:

!`./scripts/bench.sh --quick`

Haz esto:

1. Compara cada metrica con la linea base y da delta absoluto y relativo.
2. Aplica el umbral `regression_pct` de `config/loop.json`: por debajo es `RUIDO`.
3. Si hay numeros nuevos que publicar, escribelos en `docs/performance.md` (unico dueño) y
   ejecuta `./scripts/sync_state.sh`.
4. Todo numero medido con `-march=native` va etiquetado `local-only` y no sirve de linea
   base (ver docs/performance.md#p-02).

Filtro pedido: `$ARGUMENTS`
