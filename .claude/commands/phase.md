---
description: Imprime objetivo, DoD y context pack de una fase, y propone plan
argument-hint: "<numero de fase>"
allowed-tools: Bash, Read, Grep, Glob
---

Fase solicitada: **$1**

Estado actual:

!`sed -n '1,20p' STATE.md`

Ledger del loop:

!`./scripts/loop_verify.sh --fast 2>&1 | tail -20`

Haz esto:

1. Imprime el objetivo de la fase $1, su DoD y que context pack de
   `docs/context-packs/` aplica.
2. Propone un plan breve: orden de entregables, que commit incluye que, y que clases del
   loop aplican a cada entregable.
3. **Espera aprobacion humana antes de escribir codigo.**
4. Si te piden cerrar la fase $1 y el check 9 no esta en verde, **rechaza** y di que falta:
   sin ledger `CLOSED` para cada entregable no se cierra ninguna fase.
