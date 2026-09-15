---
title: Harness - gate, hooks, subagentes, comandos y loop
read_when: "antes de tocar scripts/, .claude/ o config/loop.json"
authority: canonical
last_verified: 2026-09-15
size_bytes: 5811
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
| 6 | front-matter, `size_bytes`, presupuestos, anchors, `STATE.md` | `scripts/lint-docs.sh` |
| 7 | ISA nativa en los flags EFECTIVOS del preset deploy | `scripts/lint-deploy.sh` |
| 8 | smoke end-to-end: movimiento legal y latencia sobre los fixtures | `scripts/smoke.py` |
| 9 | ledger del loop | `scripts/loop_verify.sh` |
| 10 | `docker build` y contenedor respondiendo | inline |

Codigos de salida: `0` pasa, `1` fallo de check, `2` error de entorno o toolchain, que
**no** es un veredicto sobre el codigo. Si falta docker, el check 10 sale `SKIP` y el
resumen lo dice: un gate verde con SKIP no prueba el deploy.

`--fast` ejecuta 0, 1, 3 (solo release), 4, 6, 7 y 9, e imprime
`MODO RAPIDO - no apto para cerrar fase`. Ningun modo ejecuta tests contra un binario que
ese mismo modo no haya construido.

## H-02 Autoprueba del gate {#h-02}

`./scripts/gate-selftest.sh` aplica un veneno por cada check sobre una copia temporal y
falla si el gate no falla **en ese check concreto**. Cubre los checks 0 a 10, con cuatro
venenos distintos para el check 6 y dos para el 9. Sin esto, "el gate pasa en verde" es
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

Ningun entregable se declara hecho tras una sola pasada: sobre cada uno se ejecutan al
menos 3 iteraciones de **clases distintas** (`correctness`, `robustness`, `perf`, mas
`context` si el diff toca `docs/`, `.claude/` o un numero publicado), cada una con
umbral numerico y artefacto verificable.

- Umbrales: `config/loop.json`. Nunca hardcodeados en los scripts; el ledger copia el
  sha256 del archivo y el check 9 lo compara.
- Evidencia: `.loop/<fase>/<slug>.ledger.json`, mas `i<N>.log` crudo y sus metricas.
- `scripts/loop.sh <n> <slug> [clase]` corre la parte determinista y deja el log; el
  subagente de la clase y la decision sobre los hallazgos los pone `/loop`.
- Cierre: 3 clases cubiertas, las 2 ultimas iteraciones sin hallazgos y sobre el mismo
  `commit_after`, y gate completo en verde.
- Antifraude que verifica el check 9: encadenamiento de commits, `duration_ms` minimo por
  clase, sha256 del log, `payload_sha256` no repetido, `checks_added` no vacio en toda
  iteracion que produce commit, y prueba de mutantes si las tres primeras salen limpias.

Un `findings: []` sin comandos ni log no es una iteracion limpia: es una fase fallida.
