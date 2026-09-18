---
title: "ADR-0013: las auditorias del criterio 14 no son iteraciones del loop"
read_when: "antes de cerrar un loop, de auditar una fase, o de tocar el check 9"
authority: canonical
last_verified: 2026-09-18
size_bytes: 3555
---


## D-0120 Contexto {#d-0120}

El ADR-0012 bajo la cola limpia de dos iteraciones a una para desatascar el loop de
`engine/src/rules.cpp`. No basto, y el motivo por el que no basto es el que importa:

La auditoria siguiente encontro **seis hallazgos mas**, dos de ellos imposibles de ver
leyendo el codigo -el auditor compilo una replica de `getSnakeUpdate` en Go y otra de
`movimiento_aceptado` en C++ y las corrio sobre 21 cuerpos de respuesta-. Con eso hacian
falta diez iteraciones y el techo son ocho.

El patron, en tres rondas: **cada auditoria encuentra cosas**. Es su trabajo. Y como el
criterio 14 las hace obligatorias y solo tienen sentido sobre el trabajo terminado, caen
siempre al final. Contarlas como iteraciones convierte «la auditoria funciono» en «el loop
no cierra», y empuja hacia el unico final comodo: que la auditoria no encuentre nada.

## D-0121 Decision {#d-0121}

Las auditorias del criterio 14 salen del recuento de iteraciones y pasan a un bloque
propio del ledger:

```json
"auditorias": [
  { "agent": "rules-auditor", "commit_auditado": "...", "log_sha256": "...",
    "metrics": {...}, "findings": [...] }
]
```

Siguen siendo obligatorias, siguen teniendo artefacto con su sha256, y sus hallazgos
siguen exigiendo arreglo trazado a un commit que toque un archivo citado. Lo unico que
cambia es que no gastan presupuesto de iteraciones.

Aprobado por el humano el 2026-09-18.

**Este ADR revierte el ADR-0012**: `clean_tail_iterations` vuelve de 1 a **2**. Aquella
relajacion existia para compensar este problema de contabilidad; resuelto el problema,
sobra. Ver docs/decisions/ADR-0012-cola-limpia-del-loop.md#d-0111.

`scripts/loop_verify.sh` pasa a **exigir** el bloque: un ledger sin auditorias, o con un
hallazgo de auditoria abierto, falla el check 9. Es endurecer, que la regla de oro 4 si
autoriza.

## D-0122 Alternativas descartadas {#d-0122}

| Alternativa | Por que no |
|---|---|
| Subir el techo de 8 a 12 | Camino corto que deja intacto el motivo: las auditorias seguirian gastando las ultimas iteraciones y el techo volveria a estorbar en la fase siguiente |
| Quedarse con la cola limpia en 1 (ADR-0012) | Relaja la regla de cierre para todo entregable futuro a cambio de un problema que era de contabilidad, no de rigor |
| Auditar antes de terminar, para que no caiga al final | El criterio 14 pide auditar el trabajo terminado; auditar a mitad obliga a re-auditar tras cada arreglo y no prueba lo mismo |
| Dejar de contar los hallazgos de auditoria como hallazgos | Es mentir en el ledger para que el check pase: justo lo que el antifraude persigue |

## D-0123 Consecuencias {#d-0123}

- El sha256 de `config/loop.json` cambia otra vez, asi que los cuatro ledgers lo copian.
- El ledger de la fase 1 se reordena: sus seis iteraciones de clase se quedan donde
  estaban, las tres auditorias pasan a `auditorias`, y la cola limpia de dos iteraciones
  se rehace sobre el commit final, que es el que de verdad se entrega.
- El check 9 comprueba **mas** que antes: antes no miraba si habia auditorias.
- Lo que este ADR no arregla: nada garantiza que una cuarta auditoria no encuentre una
  septima cosa. Lo que cambia es que encontrarla deja de ser un motivo para no cerrar, y
  pasa a ser lo que siempre debio ser: un hallazgo, su arreglo y su linea en el ledger.
