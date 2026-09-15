---
title: Mapa de lectura y presupuesto de bytes
read_when: "al empezar cualquier sesion, para decidir que NO leer"
authority: canonical
last_verified: 2026-09-15
size_bytes: 3189
---

## I-01 Que leer segun la tarea {#i-01}

Orden obligatorio en una sesion nueva: `CLAUDE.md` -> `STATE.md` -> este indice -> el
context pack de la tarea. Los packs listan los archivos de codigo que hace falta abrir.

| Ruta | read_when | authority |
|---|---|---|
| `docs/rules.md` | antes de tocar reglas, escribir un fixture o discutir mecanicas | canonical |
| `docs/rules-parametros.md` | antes de parsear el request o de dar por cierto un default | canonical |
| `docs/invariants.md` | antes de cambiar el motor o el cerebro | canonical |
| `docs/glossary.md` | cuando aparece un termino del dominio que no reconoces | derived |
| `docs/architecture.md` | antes de mover codigo entre modulos o añadir dependencias | canonical |
| `docs/harness.md` | antes de tocar gate, hooks, subagentes, comandos o el loop | canonical |
| `docs/performance.md` | antes de afirmar cualquier numero de rendimiento | canonical |
| `docs/strategy.md` | al proponer una version nueva del cerebro | speculative |
| `docs/SOURCES.md` | al afirmar algo sobre una herramienta externa | canonical |
| `docs/decisions/` | al reabrir una decision ya tomada | canonical |
| `docs/context-packs/` | al empezar una tarea de uno de los cinco tipos | derived |

## I-02 Presupuesto de bytes {#i-02}

Convencion: `tokens ~ bytes/4`. Es una metrica barata y determinista, no un conteo real
de tokens.

| Presupuesto | Que incluye | Techo |
|---|---|---|
| Arranque | `CLAUDE.md` + `STATE.md` + `docs/INDEX.md` | 12288 bytes |
| Por tarea | arranque + su context pack + los archivos que el pack lista | 49152 bytes |

Lo comprueba `scripts/docs_meta.sh --budget` (check 6 del gate). Pasarse se corrige
recortando o dividiendo, **nunca** subiendo el techo: subirlo exige aprobacion humana y
un ADR.

`size_bytes` de cada doc lo calcula y reescribe `scripts/docs_meta.sh --fix` con `wc -c`.
No se edita a mano; el gate falla si el valor commiteado difiere del real.

## I-03 Anchors {#i-03}

Sintaxis fijada, que es lo que parsea `scripts/lint-docs.sh`:

- definicion: `^#{2,4} .+ \{#([a-z0-9-]+)\}$`
- cita desde codigo o markdown: `(?:ver|see) (docs/[\w/.-]+\.md)#([a-z0-9-]+)`

El codigo cita la doc por anchor: `// ver docs/rules.md#r-07`. **Los anchors no se
renombran.** Si uno debe morir, se deja una linea de redireccion en su archivo y se
registra en el ADR correspondiente.

El lint es bidireccional: falla por cita a anchor inexistente y lista los huerfanos.
Cubre `**/*.{cpp,hpp}` y **todos** los `.md` del repo, incluidos `CLAUDE.md`,
`*/CLAUDE.md` y `STATE.md`.

## I-04 Quien es dueño de que {#i-04}

Un hecho, un lugar. Duplicar contenido entre archivos esta prohibido: se enlaza.

| Tipo de contenido | Unico dueño |
|---|---|
| Reglas del juego | `docs/rules.md` |
| Numeros medidos | `docs/performance.md` |
| Decisiones con alternativa descartada | `docs/decisions/ADR-*.md` |
| Progreso entre sesiones | `STATE.md` |
| Iteraciones del loop | `.loop/<fase>/` |
| Fuentes externas y sus SHA | `docs/SOURCES.md` |
