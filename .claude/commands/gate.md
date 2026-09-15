---
description: Corre el gate completo y resume los checks en rojo
argument-hint: "[--fast]"
allowed-tools: Bash, Read, Grep
---

Salida del gate:

!`./scripts/gate.sh $ARGUMENTS`

Interpreta esa salida, no describas lo que harias:

1. Lista los `CHECK <n> <nombre> FAIL` en orden.
2. Para el primero, di la causa concreta y el archivo.
3. Si aparece `MODO RAPIDO`, recuerda que no sirve para cerrar fase.
4. Si el check 10 sale `SKIP`, di explicitamente que el deploy NO esta probado.

Cuando un check falle, arregla la causa, **nunca el check** (regla de oro 4).
