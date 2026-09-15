---
description: Crea el siguiente ADR numerado con la plantilla del proyecto
argument-hint: "<titulo del ADR>"
allowed-tools: Bash, Read, Write
---

ADRs existentes:

!`ls docs/decisions/`

Crea `docs/decisions/ADR-<siguiente numero con 4 digitos>-<slug de "$ARGUMENTS">.md` con:

1. Front-matter completo (`title`, `read_when`, `authority`, `last_verified`,
   `size_bytes: 0`).
2. Secciones con anchors: Contexto, Decision, Alternativas descartadas (tabla con el
   **por que no** de cada una), Consecuencias.
3. Titulo: "$ARGUMENTS".

Un ADR sin alternativa descartada no es un ADR: es una nota. Al terminar, ejecuta
`./scripts/docs_meta.sh --fix`.
