---
title: Harness - gate, hooks, subagentes, comandos y loop
read_when: "antes de tocar scripts/, .claude/ o config/loop.json"
authority: canonical
last_verified: 2026-09-15
size_bytes: 7164
---

Modificar `scripts/gate.sh`, `config/loop.json` o `.claude/settings.json` exige
aprobacion humana explicita y un ADR. Endurecer el gate siempre esta permitido;
relajarlo en silencio, nunca.

## H-01 El gate {#h-01}

`./scripts/gate.sh` es el oraculo unico de "hecho". Imprime una linea
`CHECK <n> <nombre> PASS|FAIL|SKIP <segundos>` por check y termina con
`N checks PASS, M tests PENDIENTES`.

| Check | Que comprueba | Script |
|---|---|---|
| 0 | ningun `.sh` con CR, todos ejecutables | inline |
| 1 | build release | cmake |
| 2 | build debug con ASan y UBSan | cmake |
| 3 | tests en release y en debug | ctest |
| 4 | `clang-format --dry-run --Werror` | clang-format-18 |
| 5 | `clang-tidy` y generadores de la STL prohibidos | clang-tidy-18 |
| 6 | front-matter, `size_bytes`, presupuestos, anchors, duplicados, `STATE.md` | `scripts/lint-docs.sh` |
| 7 | ISA nativa en los flags EFECTIVOS del preset deploy | `scripts/lint-deploy.sh` |
| 8 | smoke end-to-end: movimiento legal y latencia sobre los fixtures | `scripts/smoke.py` |
| 9 | ledger del loop | `scripts/loop_verify.sh` |
| 10 | `docker build` y contenedor respondiendo | inline |

Codigos de salida: `0` pasa, `1` fallo de check, `2` error de entorno o toolchain, que
**no** es un veredicto sobre el codigo. Si falta docker, el check 10 sale `SKIP` y el
resumen lo dice: un gate verde con SKIP no prueba el deploy.

Dentro del check 6, `scripts/lint_dupes.py` mide los hechos duplicados: pasajes de doce
palabras normalizadas que dos documentos repiten en vez de enlazar. El umbral sale de
`classes.context.thresholds.duplicated_facts_max` de `config/loop.json`, y el porque de
que sea una medida y no un juicio esta en
docs/decisions/ADR-0006-umbral-de-duplicados.md#d-0051. Para apagarlo se borra su bloque
de `lint-docs.sh`, con el ADR que exige la regla de oro 10.

`--fast` ejecuta 0, 1, 3 (solo release), 4, 6, 7 y 9, e imprime
`MODO RAPIDO - no apto para cerrar fase`. Ningun modo ejecuta tests contra un binario que
ese mismo modo no haya construido.

## H-02 Autoprueba del gate {#h-02}

`./scripts/gate-selftest.sh` aplica un veneno por cada check sobre una copia temporal y
falla si el gate no falla **en ese check concreto**. Cubre los checks 0 a 10; el numero exacto
de venenos por check lo fija la tabla `POISONS` del script, que hoy tiene varios para el 6 y
dos para el 9, cada uno con el mensaje concreto que debe aparecer. Sin esto, "el gate pasa en verde" es
una afirmacion, no un hecho.

## H-03 Hooks {#h-03}

Declarados en `.claude/settings.json`. Para desactivar uno, se borra su entrada; para
desactivarlos todos, se renombra el archivo.

| Hook | Matcher | Que hace | Como se apaga |
|---|---|---|---|
| `PostToolUse` | `Edit\|Write\|MultiEdit` | `clang-format -i` sobre el archivo tocado, filtrando por extension **dentro** del comando, porque el hook recibe JSON por stdin y el matcher casa el nombre de la herramienta, nunca rutas | borrar la entrada `PostToolUse` |
| `PreToolUse` | `Edit\|Write\|MultiEdit` | deniega con exit 2 si la edicion mete `-march=native` en `CMakePresets.json` o bajo `deploy/`; es conveniencia, la garantia es el check 7 | borrar la entrada `PreToolUse` |
| `Stop` | - | bloquea el fin de sesion si hubo commits y `STATE.md` no se toco; sale 0 de inmediato si `stop_hook_active` | borrar la entrada `Stop` |
| `SessionStart` | - | imprime la cabecera de `STATE.md` y el orden de lectura; su stdout SI se inyecta al contexto | borrar la entrada `SessionStart` |

Un hook `Stop` que sale 0 escribe en un stdout que el modelo no lee: como recordatorio
informativo no existe, por eso bloquea o calla.

## H-04 Subagentes y comandos {#h-04}

Los subagentes devuelven **tablas o JSON**, nunca prosa libre: su salida entra al hilo
principal y al artefacto de la iteracion del loop.

| Subagente | Para que | Salida |
|---|---|---|
| `rules-auditor` | contrastar el Go de `rules` contra `docs/rules.md` y `engine/src/rules.cpp` | tabla `regla, doc, codigo, veredicto, cita` |
| `perf-analyst` | benchmarks y perfil, antes y despues | tabla con delta y veredicto `MEJORA/REGRESION/RUIDO` |
| `match-analyst` | localizar el turno del error decisivo en una derrota | tabla `turno, estado, movimiento, alternativa, categoria` |
| `context-curator` | auditar la capa de contexto | tabla de hallazgos; reporta divergencias de `size_bytes`, no las reescribe |

| Comando | Que hace |
|---|---|
| `/gate` | corre el gate y resume los fallos |
| `/phase <n>` | objetivo, DoD y context pack de la fase; propone plan y espera aprobacion |
| `/loop <n> <slug>` | ejecuta una iteracion del loop; `/loop status` imprime la tabla |
| `/bench [filtro]` | benchmark y comparacion contra la linea base |
| `/ab <a> <b>` | A/B con el protocolo estadistico; `--max-games` obligatorio |
| `/replay <archivo> [turno]` | reproduce una partida e invoca `match-analyst` |
| `/adr <titulo>` | crea el siguiente ADR numerado |
| `/sync-state` | actualiza `STATE.md` y regenera el bloque `perf-snapshot` |

## H-05 El loop de ingenieria {#h-05}

Ningun entregable **dentro del ambito** se declara hecho tras una sola pasada: sobre cada
uno se ejecutan al menos 3 iteraciones de **clases distintas** (`correctness`,
`robustness`, `perf`, mas `context` si el diff toca `docs/`, `.claude/` o un numero
publicado), cada una con umbral numerico y artefacto verificable.

- Ambito: `closing.deliverable_scope` de `config/loop.json`, hoy `engine/` y `snake/`.
  El harness y la documentacion los cubren el gate y sus venenos, sin ledger; declarar un
  entregable fuera de ambito hace fallar el check 9
  (ver docs/decisions/ADR-0008-ambito-del-loop.md#d-0071).

- Umbrales: `config/loop.json`. Nunca hardcodeados en los scripts; el ledger copia el
  sha256 del archivo y el check 9 lo compara.
- Evidencia: `.loop/<fase>/<slug>.ledger.json`, mas `i<N>.log` crudo y sus metricas.
- `scripts/loop.sh <n> <slug> [clase]` corre la parte determinista y deja el log; el
  subagente de la clase y la decision sobre los hallazgos los pone `/loop`.
- Cierre: 3 clases distintas cubiertas **en todo el ledger** (no necesariamente en las tres
  primeras iteraciones), las 2 ultimas iteraciones no anuladas sin hallazgos y sobre el mismo
  `commit_after`, y gate completo en verde
  (ver docs/decisions/ADR-0005-cierre-del-loop.md#d-0044).
- Antifraude que verifica el check 9: `commit_after(i)` ancestro o igual de
  `commit_before(i+1)`, `duration_ms` minimo por clase, sha256 del log, `payload_sha256` no
  repetido, `checks_added` no vacio en toda iteracion que produce commit, umbrales de la clase
  sobre las metricas, y prueba de mutantes si las tres primeras salen limpias.

Una iteracion en la que el gate falla queda **ANULADA**: sigue en el ledger, con
`annulled: true` y `annulled_reason`, pero no cuenta para el minimo ni para los umbrales
(ver docs/decisions/ADR-0005-cierre-del-loop.md#d-0045).

Un `findings: []` sin comandos ni log no es una iteracion limpia: es una fase fallida.
