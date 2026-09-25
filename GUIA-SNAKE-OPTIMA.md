# Cómo construir una Battlesnake Royale competitiva

Proceso, pautas, algoritmos y herramientas, tal como se usaron en `elaniin-snakes`.

> **Resultado a la fecha de este documento.** La snake desplegada es **v5**: búsqueda
> alfa-beta paranoica con evaluación de territorio en las hojas y control de longitud.
> Contra el campo de evaluación (tres snakes públicas), v5 sacó un **puesto medio de 1.708**
> en 60 partidas: 26 primeros puestos y ningún cuarto. v0, la línea base, sacaba 2.767.
> En producción, 337 movimientos contra la URL desplegada dieron **0 timeouts** y 230 ms de
> latencia máxima sobre un límite de 500.

---

## Índice

1. La idea central
2. El proceso
3. Pautas: lo que se aprendió midiendo
4. Arquitectura y algoritmos
5. Historia de versiones
6. Herramientas
7. Despliegue
8. Qué queda por hacer
9. Checklist para empezar de cero

---

## 1. La idea central

Una snake competitiva **no** sale de acumular heurísticas que "tienen sentido". Sale de tres
cosas que se refuerzan entre sí:

| Pieza | Para qué sirve |
|---|---|
| **Un motor fiel a las reglas oficiales** | Si el simulador se equivoca, la búsqueda optimiza un juego que no existe. |
| **Una búsqueda que mire hacia delante** | Convierte "evitar el peligro de este turno" en "evitar posiciones perdidas dentro de 10 turnos". |
| **Un instrumento de medición honesto** | Sin él no sabes si un cambio mejora, empeora o solo mueve el ruido. |

La tercera es la que más se subestima. De diez candidatas medidas contra una referencia,
**solo dos ganaron su A/B de forma concluyente** (v4 y v5). Sin el instrumento, las otras
ocho se habrían desplegado por intuición, y cinco de ellas tenían el efecto medido del lado
malo.

---

## 2. El proceso

### 2.1 La fuente de verdad son las reglas en Go, no la documentación

Las reglas de Royale se derivaron del código Go de `BattlesnakeOfficial/rules`, con el
commit fijado (`87e094e2`) y cada afirmación citada como `archivo.go:línea`. Varias cosas
que "todo el mundo sabe" resultaron distintas al leer el código:

- **Los segmentos nacen apilados.** Al empezar, los tres segmentos comparten casilla:
  durante los primeros turnos la cola sigue ocupada aunque nadie haya comido.
- **La casilla de cola está libre si y solo si no hay segmentos duplicados al final**
  (`body[n-1] == body[n-2]`). Nunca se deriva de un flag del tipo "comió el turno anterior".
- **El lado del *shrink* de Royale es aleatorio y con repetición.** Lo decide un RNG
  sembrado con la semilla de la partida, que no viaja en el payload, así que en una
  partida real **no se puede predecir** por qué lado se cierra el tablero.
- **La comida aparece con una fórmula peculiar**: `(100 - rand.Intn(100)) < foodSpawnChance`,
  cuya probabilidad efectiva es `(chance − 1)/100`.
- **Ante una respuesta inválida o un timeout**, el motor aplica un movimiento por defecto
  que hay que reproducir exactamente, o el test diferencial no cuadra.

El motor propio se validó con un **test diferencial**: reproducir partidas JSONL del
árbitro oficial turno a turno y exigir estados idénticos.

> ⚠️ `BattlesnakeOfficial/rules` es **AGPL-3.0**. Se usó como referencia para derivar las
> reglas; el motor es código propio escrito desde cero. Lo mismo con las snakes públicas:
> se usan como sparring, nunca se copia su código.

### 2.2 El gate: "está hecho" es un código de salida

`scripts/gate.sh` es el único árbitro de si algo está terminado. Corre, en orden, y se
detiene en el primer fallo:

| # | Check |
|---|---|
| 0 | Higiene de scripts (sin `\r`, ejecutables) |
| 1–2 | Build en release y en debug con ASan + UBSan (`-fno-sanitize-recover=all`) |
| 3 | Tests unitarios en ambos |
| 4–5 | `clang-format` y `clang-tidy` |
| 6 | Lint de documentación: front-matter, anchors bidireccionales, presupuesto de bytes |
| 7 | Lint de deploy sobre flags *efectivos* (sin `-march=native` aunque se herede) |
| 8 | Smoke test end-to-end: servidor real, fixtures, p99 ≤ 50 ms |
| 9 | Lint del loop de ingeniería (ledger verificable) |
| 10 | Deploy real: `docker build` + `docker run` + `/health` |
| 11–12 | Zoo y tests del training room |

Dos reglas lo hacen fiable:

- **El gate solo crece.** Si pasa y sabes que algo está mal, el bug está en el gate: se
  añade el check que falta. Nunca se debilita para que algo pase.
- **`gate-selftest.sh` le inyecta venenos** (un doc sin front-matter, `-march=native`
  heredado, un `.cpp` desformateado…) y falla si el gate no los detecta. Un check que pasa
  porque no comprueba nada es un fallo.

### 2.3 El loop de ingeniería

Ningún entregable se da por hecho tras una pasada. Cada uno pasa por **≥3 iteraciones de
clases de defecto distintas**, registradas en un ledger JSON que el gate valida:

| Iteración | Clase | Umbral de salida |
|---|---|---|
| i1 | **correctness** contra la fuente de verdad | 0 divergencias, toda afirmación con cita |
| i2 | **robustness** | sanitizers limpios, 10 000 estados fuzz sin movimiento ilegal, fail-safe forzado en sus 4 escalones, mutantes muertos ≥ 80% |
| i3 | **perf** | p99 dentro del presupuesto, 0 asignaciones en el hot path |
| i4 | **context** (si toca docs) | anchors, front-matter, presupuestos de bytes |

El ledger es antifraude: los commits se encadenan, los logs llevan sha256, cada iteración
tiene que añadir un check ejecutable y el número de tests no puede bajar.

### 2.4 El protocolo de A/B

Ningún cambio de estrategia entra por intuición. Todo pasa por este protocolo:

1. **La hipótesis se escribe antes de medir**, en `docs/strategy.md`, con la forma en que
   podría fallar. Así un resultado negativo no se reinterpreta a posteriori.
2. **Una sola variable por experimento.** Medir dos cambios juntos no dice cuál hizo qué.
3. **La unidad es el bloque**: una semilla con las 4 rotaciones de asiento. A y B juegan
   las mismas semillas desde los mismos asientos, pero **nunca en la misma partida**.
4. **Métrica primaria única**: diferencia pareada de puesto medio por bloque, con IC95. Todo
   lo demás (turnos, causas de muerte) es descriptivo y va sin p-valores.
5. **Veredicto**: `MEJORA` si el IC95 no cruza el cero y el efecto supera el delta útil
   (0.1); `EMPEORA` en el caso simétrico; si no, `NO CONCLUYENTE`.
6. **El campo tiene que ser distinto de las dos ramas.** Si B es igual al campo, las cuatro
   partidas de un bloque son la misma y la media sale 2.500 exacta.

`NO CONCLUYENTE` no significa "igual". Significa "con estos bloques no se distingue". Con 15
bloques, el protocolo resuelve efectos de más o menos **0.15 de puesto**; para ver 0.05
harían falta del orden de cien.

---

## 3. Pautas: lo que se aprendió midiendo

Cada una de estas pautas salió de un experimento concreto. Están ordenadas por cuánto
tiempo habrían ahorrado si se hubieran sabido al principio.

### La fuerza está en la evaluación, no en la profundidad

La búsqueda a profundidad 6 contra profundidad 12 movió **−0.017**, con el IC cruzando el
cero. Una sonda independiente lo confirmó: pasar de 150 a 300 ms da el doble de nodos y solo
**+0.4 niveles**, y el movimiento elegido se fija de media en la **profundidad ~3**. Más de
la mitad de las profundidades completadas son confirmatorias: terminan eligiendo lo que ya
estaba elegido.

Las dos mejoras grandes del proyecto (v4 y v5) fueron cambios en la **evaluación de las
hojas**, no en la búsqueda.

### La causa de muerte es un síntoma, no una palanca

De las muertes de v5, 27 de 34 fueron por hambre o por hazard con poca vida. v6 atacó eso
directamente, midiendo la salud en turnos de vida, y funcionó: las muertes por falta de
vida bajaron de 27 a 19. **Pero el puesto medio empeoró**, porque las otras causas subieron
de 6 a 12. La snake no muere de hambre: muere de estar en el sitio donde solo quedaba comer
mal. Tapar la causa de muerte dominante solo cambia de qué se muere, no el puesto. v10 repitió
el mismo patrón.

### Una correlación dentro de una versión no es una palanca

Dentro de v5, ser más largo parecía correlacionar con ganar. Al agrupar por ventaja de
longitud, la curva resultó ser un **escalón**, no una pendiente: quedarse muy por detrás es
letal, y por encima de la paridad la curva es plana. Entrando al duelo final con +4 o más,
v5 lo ganaba solo el **31%** de las veces, contra un 75% entrando por detrás. Lo único
causal fue el paso de v4 a v5, que sacó a la snake del tramo malo.

### Un término puede estar muerto por culpa del modelo de búsqueda

Premiar pegar la cabeza a la de un rival más corto (`prefer_shorter`) **no cambió ni una
sola decisión**: con peso 8, 40 o 400, la puntuación de la raíz fue idéntica en 612
posiciones, y el A/B produjo exactamente las mismas 60 partidas. La razón es que en la
búsqueda paranoica el rival es un minimizador y siempre puede apartarse, así que el premio
nunca llega a la raíz. **Antes de ajustar un peso, comprueba que el término alcanza alguna
decisión.**

### Semillas comunes para comparar, no para optimizar

SPSA con las mismas semillas en todas las evaluaciones dio **2.219** de puesto medio en las
semillas de ajuste y **2.469** en semillas nuevas. Había memorizado la muestra. Las semillas
comunes son correctas para *comparar* dos alternativas fijas y una trampa para *optimizar*.
El arreglo: rotar semillas entre iteraciones y publicar solo el resultado sobre un
**holdout** que ninguna iteración tocó.

### Presupuesto por nodos, nunca por reloj, en la arena

Una búsqueda anytime con deadline de reloj devuelve movimientos distintos según la carga
de la máquina. Con un presupuesto fijo de nodos, la búsqueda es una **función pura del
estado**: la misma corrida da el mismo resultado con 1 hilo o con 16, y en máquinas
distintas. Es lo que hace posible el A/B pareado.

### Mide la latencia como la ve el árbitro

`curl` en bucle dio un p99 de 397 ms; el árbitro, en partida real, **207 ms**. La
diferencia es el handshake TLS: `curl` abre una conexión nueva en cada petición, y el
árbitro la reutiliza durante toda la partida. El margen de red se calibró primero en 260 ms
con el dato malo y después en 80 con el dato bueno. Un margen inventado por arriba también
es peligroso: con 260, un timeout anunciado de 300 dejaba **1 ms** de cómputo.

### No hay optimización sin medición

La hipótesis era que la ordenación de movimientos dominaba el coste por nodo. Quitarla
entera hizo la búsqueda un **10% más lenta**: con peor ordenación hay menos poda y se evalúan
más hojas, y cada hoja cuesta un Voronoi. La hipótesis estaba equivocada y el experimento la
tumbó en cinco minutos.

### Los entornos efímeros no sirven para corridas largas

Un A/B de hora y media lanzado en un contenedor de nube murió a los dos minutos de
inactividad de la sesión. Las corridas largas van en una máquina que no se recicla.

---

## 4. Arquitectura y algoritmos

```
engine/        motor C++20 puro: reglas, sin I/O, sin allocs en el hot path
snake/         cerebro (búsqueda + evaluación) y servidor HTTP
arena/         self-play in-process, sin HTTP, presupuesto por nodos
training-room/ orquestador de torneos, SQLite, A/B, afinado
zoo/           snakes públicas como sparring, aisladas en contenedor
scripts/       gate, bench, verificación de despliegue
docs/          reglas, invariantes, experimentos, ADRs
```

### 4.1 El motor (`engine/`)

| Decisión | Por qué |
|---|---|
| **`Bitboard<W,H>` en tiempo de compilación** (`ceil(W·H/64)` palabras; 11×11 = 2 × `uint64_t`) | Ocupación, colisiones y flood fill con `popcount`/`tzcnt` en lugar de bucles sobre casillas. |
| **`GameState` trivialmente copiable**, cuerpos en *ring buffers* de tamaño fijo | Copy-make: el llamante copia el estado y `apply` muta la copia. Copiar cuesta ~10 ns. |
| **`apply` simultáneo y sin filtrar** | Acepta cualquier dirección, incluida la mortal, y reproduce el movimiento por defecto del motor oficial. `legal_moves` es una ayuda para el cerebro, no una regla. |
| **Cero asignaciones dinámicas** en `apply`, `legal_moves` y `decide` | Verificado con un allocator instrumentado. |
| **RNG propio (xoshiro256++)** con vectores de test | `std::uniform_int_distribution` y compañía no están especificados y difieren entre libstdc++ y libc++. |
| **`placements()` con rango compartido promediado** | Dos muertes simultáneas en 3º/4º puntúan 3.5 las dos. Nunca se desempata por asiento. |

Rendimiento medido (1 hilo, ISA de despliegue `x86-64-v2`): **`apply()` 164 ns**,
`legal_moves()` 49 ns, copia de estado 9.4 ns.

### 4.2 El cerebro base (`brain_v0`)

La base sobre la que se construye todo, y el último escalón de seguridad:

1. **Filtro duro**: fuera del tablero y contra cuerpos, con la cola tratada por segmentos
   duplicados.
2. **Zona de cabeza**: evitar las casillas adyacentes a cabezas iguales o más largas.
3. **Flood fill con tail-escape**: la cola propia cuenta como libre si es alcanzable en al
   menos los turnos que tarda en liberarse.
4. **Rechazo de trampas**: descartar movimientos cuyo espacio alcanzable sea menor que la
   propia longitud (salvo que todos lo sean).
5. **Comida con criterio**: solo por debajo de un umbral de salud o cuando hay ventaja que
   ganar sin riesgo. No se come por comer.
6. **Penalización de hazard** según el daño y la salud restante.

### 4.3 La búsqueda

**Alfa-beta paranoica con profundización iterativa.**

- **Paranoica**: se asume que todos los rivales simulados juegan para minimizar nuestra
  puntuación. Es pesimista, pero convierte un juego de 4 en un árbol de 2 jugadores
  donde funciona la poda alfa-beta.
- **Solo los 2 rivales más cercanos se simulan** (`max_rivals = 2`); los demás repiten su
  último movimiento. Cada rival simulado multiplica el árbol por ~3. Simular el tercero
  (v7) redujo las muertes por cabezazo, pero no mejoró el puesto: el modelo paranoico con
  tres rivales coordinados encoge demasiado lo que se considera jugable.
- **Profundización iterativa** hasta `max_depth = 64`, devolviendo siempre la mejor jugada
  de la **última profundidad completada**. Con el tope en 8, las posiciones de 2 vivas
  devolvían 158 de 200 ms sin usar.
- **Dos formas de corte**: el deadline de reloj en producción y un tope de nodos
  (`budget_nodes`) en la arena.

Profundidad típica: **~6 niveles con 4 vivas** (150 ms) y **~12 con 2** (medido a 200 ms).

### 4.4 La evaluación en las hojas (lo que de verdad hace fuerte a la snake)

| Término | Qué mide | Peso |
|---|---|---|
| **Territorio** (Voronoi) | Casillas a las que llegamos antes que nadie, por BFS simultáneo desde todas las cabezas; las casillas de hazard valen el 50% | 120 |
| **Espacio** | Flood fill desde nuestra cabeza; si no cabe ni nuestro cuerpo, penalización fuerte | 100 |
| **Longitud relativa** | Ventaja sobre el rival más largo, **saturada** en +3 (a partir de ahí, el cuerpo de más estorba) | 60 |
| **Zona de cabeza** | Penaliza quedar pegado a una cabeza igual o más larga | 80 |
| **Salud** | En turnos de vida (`salud / coste por turno`), porque 50 de salud son 50 turnos fuera del hazard y 3 dentro | variable |
| **Caza de comida** | Distancia a la comida cuando vamos cortos o con hambre | 6–10 |
| **Rivales vivos** | Menos rivales es mejor puesto | 40 por rival |

La lección de v1 contra v4 es clave: **el mismo Voronoi** como decisión de un turno no
entró (+0.050), y en las **hojas de la búsqueda** dio la primera mejora concluyente del
proyecto (−0.367). La evaluación buena va donde el árbol ya descartó lo táctico.

### 4.5 Fail-safe

Orden estricto de degradación, con un test por escalón:

```
mejor movimiento de la búsqueda
  → movimiento seguro con más espacio
    → cualquier movimiento geométricamente legal
      → "up"
```

Cualquier excepción, deadline vencido, tablero sin instanciación o variante no soportada
salta al siguiente escalón. El servidor **nunca** devuelve 5xx en `/move`. Las variantes no
soportadas (`wrapped`, `constrictor`) entran en un modo degradado seguro con un `WARN` en el
log.

### 4.6 El presupuesto de tiempo

```
cómputo = min(timeout − margen_red − margen_seguridad, techo)
        = min(500 − 80 − 50, 150) = 150 ms
```

| Parámetro | Valor | De dónde sale |
|---|---|---|
| `network_margin_ms` | 80 | p99 de transporte medido por el árbitro: 57 ms, con un 40% de holgura |
| `safety_margin_ms` | 50 | colchón contra picos del sistema |
| `max_compute_ms` | 150 | subirlo a 300 da +0.4 niveles de profundidad; no compensa el riesgo |

Todos los parámetros se leen del request (`game.timeout`, `game.ruleset.settings.*`); nunca
se hardcodean.

---

## 5. Historia de versiones

Medidas contra el gauntlet (tres snakes públicas) o, a partir de v7, en la arena. Puesto
medio: 1 es ganar siempre y 4 es quedar siempre último.

| Versión | Cambio | Diferencia pareada | Veredicto |
|---|---|---|---|
| v0 | Baseline: filtro duro + flood fill + comida por umbral | — (2.767) | referencia |
| v1 | Territorio Voronoi como decisión de un turno | +0.050 | no entra |
| cuellos | Penalizar "salas de una sola puerta" | +0.075 | no entra (se dispara en el 93% de los estados) |
| v3 | Búsqueda paranoica, profundidad 6 → 12 | −0.267 / −0.283 | ayuda, no concluyente |
| **v4** | **Territorio en las hojas de la búsqueda** | **−0.367** | **MEJORA** |
| **v5** | **Ventaja de longitud + política de comida** | **−0.692 vs v4** | **MEJORA · desplegada** |
| v6 | Salud en turnos de vida | +0.058 | no entra |
| v7 | Simular al tercer rival | +0.083 | no concluyente |
| v8 | SPSA sobre 14 pesos | −0.03 en holdout | no entra (sobreajuste) |
| v9 | `prefer_shorter` 8 → 40 | 0.000 | término muerto |
| v10 | Evaluación propia del duelo final | −0.017 [−0.16, +0.13] | no concluyente |

Reparto de puestos de v5 en 60 partidas: **26 primeros, 24 segundos, 10 terceros, 0
cuartos**.

---

## 6. Herramientas

### Arbitraje y rivales

| Herramienta | Uso |
|---|---|
| **`battlesnake` CLI oficial** | Árbitro de verdad. `battlesnake play -g royale -m royale -t 500 -o partida.jsonl`. Verifica los flags con `--help`, no los asumas. |
| **snake-zoo + manifests** | Snakes públicas como rivales, cada una en un contenedor aislado (`--read-only`, `--cap-drop=ALL`, usuario sin privilegios, solo puerto en `127.0.0.1`). Commit fijado por SHA. |
| **`gauntlet-v1`** | El campo congelado de evaluación: digests de imagen y composiciones exactas. Cambiar el campo crea `gauntlet-v2` e invalida las comparaciones anteriores. |

### Medición y experimentos

| Herramienta | Uso |
|---|---|
| **`training-room/tr.py`** | Torneos por HTTP contra el zoo; persiste en SQLite versión, commit, hash del config, semilla y JSONL de cada partida. |
| **`tools/arena_torneo` + `training-room/arena_ab.py`** | A/B in-process sin HTTP: las dos ramas en una sola corrida, con el mismo presupuesto de nodos por contrato. Salida idéntica con 1 o N hilos. |
| **`training-room/compara.py`** | La única ruta estadística: diferencia pareada por bloque, IC95 y veredicto. Avisa de **RAMAS IDÉNTICAS** cuando un cambio no alcanzó ninguna decisión. |
| **`training-room/afina.py`** | SPSA sobre los pesos, con semillas que rotan por iteración y evaluación de control obligatoria sobre un holdout. |
| **`sonda_busqueda`** | Profundidad alcanzada, nodos por movimiento y en qué profundidad se fija la decisión, para un presupuesto dado. |
| **`sonda_arena --calibrar`** | Cuántos nodos caben en X ms **en esta máquina**. Hay que recalibrar en cada máquina donde se corra un A/B. |
| **`causas`** | Clasifica la causa de muerte de nuestra snake en un torneo. |
| **`scripts/bench.sh`** | Google Benchmark con la ISA de despliegue (publicable) o nativa (etiquetada `local-only`). |

### Despliegue y operación

| Herramienta | Uso |
|---|---|
| **`deploy/Dockerfile`** | Multi-stage: build sobre `debian:12` y runtime `distroless/cc-debian12:nonroot`, con `-static-libstdc++ -static-libgcc` y `-march=x86-64-v2`. |
| **`deploy/cloud-run.sh`** | Build, push y deploy con los parámetros que importan (§7). |
| **`scripts/verifica-despliegue.sh`** | RTT puro con `/health` frente a `/move`: una cota superior y la prueba de que el servicio responde. |
| **`scripts/latencias-jsonl.py`** | La latencia **que ve el árbitro**, sacada del JSONL de una partida real. Este es el número que decide el presupuesto. |

### Harness del agente (`.claude/`)

- **Subagentes** con salida tabular fija: `rules-auditor`, `perf-analyst`, `match-analyst`
  y `context-curator`.
- **Comandos**: `/gate`, `/phase`, `/loop`, `/bench`, `/ab`, `/replay`, `/adr` y
  `/sync-state`.
- **Hooks**: `clang-format` al editar, bloqueo de `-march=native` en deploy, inyección del
  estado al abrir sesión y aviso si hay commits sin actualizar `STATE.md`.
- **Capa de contexto**: `CLAUDE.md` + `STATE.md` + `docs/INDEX.md` ≤ 12 KB, y un
  *context pack* por tipo de tarea ≤ 48 KB, para que una sesión nueva retome el trabajo sin
  explorar a ciegas.

---

## 7. Despliegue

Cloud Run en `us-east1`:

| Parámetro | Valor | Por qué |
|---|---|---|
| `--concurrency` | 1 | La búsqueda usa el núcleo entero durante su deadline; dos partidas en la misma instancia se roban CPU. |
| `--no-cpu-throttling` | sí | Sin esto, Cloud Run estrangula la CPU entre requests y el primer movimiento llega tarde. |
| `--min-instances` | 4 | Una por partida simultánea que se quiera cubrir sin arranque en frío. |
| `--cpu` | 1 | La búsqueda es de un hilo. |
| `--timeout` | 30s | Timeout del *servicio*, no de la partida. |

`GET /` devuelve la versión desplegada (`_version` del config) para comprobar que un
redespliegue aterrizó. `GET /health` responde sin invocar al cerebro.

**Un riesgo que no es de latencia.** Antes de arrancar la partida el árbitro hace un `GET /`
con los mismos 500 ms de límite y por una conexión nueva, así que esa petición paga el handshake TLS
completo. Si falla, la partida no empieza. Lo mitiga tener las instancias calientes.

---

## 8. Qué queda por hacer

Las palancas fáciles se agotaron: cinco candidatas seguidas no superaron a v5, con efectos
acotados por debajo de ~0.2. Lo que queda requiere otro enfoque:

1. **Mejorar el instrumento antes que la snake.** Añadir una cuarta snake independiente al
   gauntlet (hoy las tres comparten motor) y correr A/B de 60 bloques. Es la única forma de
   ver efectos de 0.05–0.1, que es donde probablemente está lo que queda.
2. **Cobrar la ventaja quitando territorio, no buscando contacto.** Con un minimizador en el
   árbol, el premio por contacto nunca llega a la raíz; en cambio, reducir el territorio
   Voronoi del rival sí es algo que el minimizador no puede evitar.
3. **Puntos de articulación con umbral.** El detector de cuellos se disparaba en el 93% de
   los estados. Un detector *relativo* —que solo puntúe cuando la pérdida es grande en
   proporción a la longitud— no se ha probado.
4. **Repetir el SPSA** ya con rotación de semillas y holdout, cuando haya un término nuevo
   que ajustar.

Fuera de alcance por ahora: NNUE (necesita un pipeline de datos) y paralelismo (Cloud Run
con 1 vCPU, y la profundidad no es el cuello de botella).

---

## 9. Checklist para empezar de cero

- [ ] Fija el commit de `BattlesnakeOfficial/rules` y deriva cada regla con su cita.
- [ ] Escribe el motor con estado trivialmente copiable y `apply` simultáneo sin filtrar.
- [ ] Valídalo con un test diferencial contra JSONL del árbitro oficial.
- [ ] Monta el gate **antes** de la estrategia, con su selftest de venenos.
- [ ] Construye un v0 completo y jugable, y congélalo como referencia.
- [ ] Monta el instrumento: gauntlet congelado, bloques con rotación de asientos y
      diferencia pareada con IC95.
- [ ] Añade búsqueda paranoica con profundización iterativa y fail-safe de 4 escalones.
- [ ] Pon la evaluación cara (Voronoi) **en las hojas**, no en la decisión de un turno.
- [ ] Añade la arena con presupuesto por nodos para iterar rápido y de forma reproducible.
- [ ] Para cada idea: hipótesis escrita → una variable → sonda de que el término está vivo →
      A/B → veredicto escrito, gane o pierda.
- [ ] Despliega con los márgenes medidos por el árbitro, no por `curl`.
- [ ] Verifica en producción con una partida real y `latencias-jsonl.py`.

---

*Toda cifra de este documento está medida y registrada en el repositorio:
`docs/experimentos.md` (resultados de estrategia), `docs/performance.md` (rendimiento y
latencia), `docs/rules.md` (reglas con citas) y `docs/decisions/` (ADRs).*
