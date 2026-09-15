---
title: "ADR-0006: hechos duplicados medidos, y techo del loop"
read_when: "antes de tocar config/loop.json, scripts/lint_dupes.py o el check 5 del gate"
authority: canonical
last_verified: 2026-09-15
size_bytes: 3701
---


## D-0050 Contexto {#d-0050}

El loop del entregable `scripts/gate.sh` se quedo BLOQUEADO en su iteracion 6 -el techo-
con un hallazgo abierto: `duplicated_facts = 13` frente a un umbral de 0. El conteo lo
produjo el subagente `context-curator` leyendo los documentos, es decir, un juicio, no una
medicion: `scripts/loop_verify.sh` no podia recalcularlo ni contrastarlo.

Cerrar el loop exigia ademas dos iteraciones limpias mas, que con el techo en 6 no caben.
El humano decidio el 2026-09-15: corregir las duplicaciones y subir el techo, con este ADR.

## D-0051 Decision {#d-0051}

1. **`duplicated_facts` pasa a ser una medida, no una opinion.** `scripts/lint_dupes.py`
   cuenta pasajes de 12 palabras normalizadas consecutivas que aparecen en dos archivos
   markdown distintos, sobre la prosa (sin front-matter, tablas ni bloques de codigo) de
   todos los `.md` versionados salvo `docs/results/**`, `docs/decisions/**` y el prompt
   maestro. El check 6 del gate lo ejecuta con el umbral de `config/loop.json`, y
   `gate-selftest.sh` tiene su veneno (`poison_6f`).
2. **El umbral se queda en 0.** Se propuso relajarlo a 3, pero corregir los diez pasajes
   pendientes costo siete ediciones: en todas, el archivo que repetia el hecho ya citaba
   el anchor del dueño y bastaba con borrar la parafrasis. Relajar un umbral que se
   cumple seria debilitar el gate sin motivo (regla de oro 4).
3. **`closing.max_iterations` sube de 6 a 8.** Es el unico cambio de `config/loop.json`.
   Con seis iteraciones consumidas, cualquier arreglo necesita todavia dos iteraciones
   limpias que compartan `commit_after` para cerrar; con el techo en 6, un entregable que
   encuentra un defecto en su sexta iteracion no puede cerrarse jamas, ni arreglandolo.
4. **Los checks 5 y 7 vuelven a su forma estricta.** El check 5 mira el arbol entero de
   `engine/` y `arena/`, comentarios incluidos; el check 7 no perdona la ISA nativa por
   estar en un comentario del Dockerfile. Los dos comentarios que los hacian saltar ahora
   citan (`docs/invariants.md#inv-08`, `docs/decisions/ADR-0004-deploy.md#d-0030`) en vez
   de nombrar lo prohibido.

## D-0052 Alternativas descartadas {#d-0052}

| Alternativa | Por que no |
|---|---|
| Fijar `duplicated_facts_max = 3`, como se aprobo | La medicion posterior dio 0 duplicados tras siete ediciones. Un umbral por encima de lo que ya se cumple solo sirve para permitir regresiones |
| Dejar el conteo en manos del subagente | Un umbral que el gate no puede recalcular no es un umbral: es una nota. Fue exactamente lo que dejo el loop bloqueado |
| Quitar el techo de iteraciones | El techo es lo que obliga a escalar al humano en vez de iterar indefinidamente. Se sube, no se quita |
| Comparar por significado (embeddings, LLM) | No es determinista ni reproducible entre maquinas, que es lo unico que un gate puede exigir. El juicio semantico se queda en `context-curator`, como aviso |

## D-0053 Consecuencias {#d-0053}

- El check 6 falla si alguien reintroduce un pasaje repetido, con el archivo, el pasaje y
  el conteo en la salida.
- La metrica es literal: dos documentos pueden afirmar el mismo hecho con otras palabras y
  el script no lo vera. `context-curator` sigue siendo quien lo detecta, y su hallazgo se
  arregla o se anota; el script cubre la regresion barata, que es la frecuente.
- Un n-grama de 12 palabras no confunde giros del idioma con hechos: los falsos positivos
  que aparecieron eran duplicaciones reales.
