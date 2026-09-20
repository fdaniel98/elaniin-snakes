---
title: Numeros medidos
read_when: "antes de afirmar cualquier cosa sobre rendimiento, y despues de cada bench"
authority: canonical
last_verified: 2026-09-18
size_bytes: 11891
---

Este archivo es el **unico dueño** de todo numero medido. `STATE.md` no tiene numeros
propios: su bloque `perf-snapshot` se regenera desde aqui con `scripts/sync_state.sh`, y
el gate falla si alguien lo edita a mano.

Lo que no se ha ejecutado se escribe `no medido`. Nunca se estima un numero.

## P-01 Presupuesto de latencia {#p-01}

Fijo por contrato; cambiarlo exige aprobacion humana explicita y un ADR.

| Concepto | Valor | Origen |
|---|---|---|
| `timeout` anunciado por el arbitro | 500 ms | `game.timeout` del request (ver docs/rules-parametros.md#r-20) |
| Margen de red | **260 ms, MEDIDO** | `time.network_margin_ms`; p99 de RTT contra el despliegue real |
| Margen de seguridad | 50 ms | `time.safety_margin_ms` |
| Techo de computo | **150 ms** | `time.max_compute_ms` |
| p99 de `POST /move` local sobre fixtures | 50 ms como techo | check 8 del gate |
| Maximo de `POST /move` local | 150 ms como techo | check 8 del gate |

Pasar de 350 ms de computo sigue siendo fallo duro.

**El margen de red dejo de ser una estimacion el 2026-09-20.** Valia 100 ms porque se
escribio a ojo en la fase 0; medido contra el despliegue de `us-east1` con
`scripts/verifica-despliegue.sh`, el p99 de RTT puro es **259.4 ms**, o sea 2.6 veces mas.
El techo de computo bajo de 200 a 150 en consecuencia
(ver docs/decisions/ADR-0037-margenes-medidos.md#d-0371).

| | p50 | p95 | p99 |
|---|---:|---:|---:|
| RTT puro (`GET /health`, sin cerebro) | 141.3 ms | 187.1 ms | 259.4 ms |
| `POST /move` completo, computo 200 ms | 337.6 ms | 361.0 ms | 397.5 ms |

La resta cuadra: 337.6 - 141.3 = 196 ms de computo contra los 200 concedidos. Medido desde
la maquina de referencia, que no es desde donde arbitra el torneo: es el caso malo, elegido
a proposito.

## P-02 Como se miden los numeros publicables {#p-02}

Un numero medido con `-march=native` **no** puede usarse como linea base: no es el binario
que se despliega. Por eso hay dos presets de medicion.

| Preset | Flags | Etiqueta | Uso |
|---|---|---|---|
| `bench-deployisa` | `-O3 -march=x86-64-v2 -mtune=generic` | publicable | linea base y comparaciones |
| `bench` | `-O3 -march=native` | `local-only` | exploracion en la maquina de desarrollo |

`scripts/bench.sh` usa el primero por defecto y etiqueta el segundo como `local-only`.

## P-03 Maquina de referencia {#p-03}

| Campo | Valor |
|---|---|
| Host | Windows 11 con WSL2 (Ubuntu 24.04), repo en `/mnt/c` |
| CPU logicas visibles | 8 |
| Memoria visible | 15.9 GB |
| Compilador | clang 18.1.3 |
| Fecha | 2026-09-15 |

Dos corridas con campos distintos en esta tabla **no se comparan**.

<!-- BEGIN:perf-canonical -->
| metrica | valor | commit | fecha |
|---|---|---|---|
| `apply()/s` (1 hilo, bench-deployisa) | 6.10 M/s (164 ns) | 8082d1d | 2026-09-15 |
| `legal_moves()/s` (1 hilo, bench-deployisa) | 20.43 M/s (48.9 ns) | 8082d1d | 2026-09-15 |
| `decide()/s` (1 hilo, bench-deployisa) | 1.71 M/s (585 ns) | 8082d1d | 2026-09-15 |
| copias de estado/s | 106.35 M/s (9.40 ns) | 8082d1d | 2026-09-15 |
| `POST /move` p50 (local, 13 fixtures x 20) | 0.43 ms | 8082d1d | 2026-09-15 |
| `POST /move` p99 (local, 13 fixtures x 20) | 0.79 ms | 8082d1d | 2026-09-15 |
| `POST /move` maximo (local, 13 fixtures x 20) | 23.88 ms | 8082d1d | 2026-09-15 |
| asignaciones dinamicas en `apply`/`legal_moves`/`decide` | 0 / 0 / 0 | 8082d1d | 2026-09-15 |
<!-- END:perf-canonical -->

## P-04 Historico {#p-04}

Una fila por medicion publicada, con su commit. Las salidas crudas de Google Benchmark
van a `docs/results/bench-*.json`, que queda fuera del lint de docs.

| Fecha | Cambio | Metrica | Antes | Despues | Veredicto |
|---|---|---|---|---|---|
| 2026-09-15 | linea base inicial de la fase 0 (commit 8082d1d) | todas | - | ver tabla canonica | LINEA BASE |
| 2026-09-17 | fase 1 en el contenedor de la nube (commit cb2c4f1) | todas | - | ver P-06 | SIN COMPARAR: otra maquina |

El maximo de `POST /move` (23.88 ms) esta 30 veces por encima del p99 (0.79 ms): es la
primera peticion, que paga el arranque del servidor y la carga del config. Queda como
hallazgo abierto de la clase `perf`, no como regresion: el presupuesto de la fase 0 son
50 ms de p99 y 150 ms de maximo, y ambos se cumplen con margen.

## P-07 Soak del servidor {#p-07}

La DoD de la fase 2 pide 0 timeouts en 200 partidas locales y el p99 publicado. Lo de
abajo son las **200 partidas en la maquina de referencia** (ver P-03), que es la medicion
que vale.

Que mide cada cosa, porque se confunden con facilidad:

| Numero | De donde sale | Para que sirve |
|---|---|---|
| timeouts | stderr del arbitro | lo unico que los cuenta bien: el JSONL no exporta ni la serpiente eliminada ni el ultimo turno (ver docs/rules-parametros.md#r-20) |
| latencia del arbitro | `latency` del JSONL | la forma de la distribucion, en milisegundos enteros |
| computo interno | campo `us=` del log del servidor | lo que cuesta nuestro codigo, con resolucion de microsegundos |

| metrica | valor | commit | fecha |
|---|---|---|---|
| partidas jugadas / pedidas | 200 / 200 | bb99e84 | 2026-09-18 |
| timeouts | 0 | bb99e84 | 2026-09-18 |
| respuestas que el arbitro rechazo | 0 | bb99e84 | 2026-09-18 |
| computo interno p50 / p95 / p99 / maximo | 0.048 / 0.065 / 0.08 / 0.388 ms | bb99e84 | 2026-09-18 |
| latencia del arbitro p50 / p95 / p99 / maximo | 0 / 1 / 6 / 169 ms | bb99e84 | 2026-09-18 |
| arranque en frio p50 / p99 / maximo | 1 / 5 / 9 ms | bb99e84 | 2026-09-18 |
| movimientos medidos | 11903 | bb99e84 | 2026-09-18 |

El numero que hay que mirar de estos no es el p99: es que **el maximo del arbitro, 169 ms,
son 434 veces el maximo de nuestro codigo, 0.388 ms**. Los 168 ms restantes no son el
cerebro decidiendo: son transporte y planificacion en una maquina con ocho nucleos
haciendo otras cosas. Queda lejisimos del timeout de 500 ms, y aun asi es un tercio del
presupuesto gastado en algo que no controlamos desde el codigo.

Importa para la fase 7, donde el margen de red de 100 ms se recalibra con RTT real: el
margen no lo consume solo la red, tambien el sistema operativo debajo. Aqui queda medido y
sin tocar, porque optimizar lo que no se ha perfilado seria justo lo que la regla de oro 2
prohibe.

## P-05 Politica de playout {#p-05}

`playouts/s` no significa nada sin decir que se juega. El que mide `bm_playout`:

| Aspecto | Valor |
|---|---|
| Eleccion de movimiento | uniforme entre las direcciones que devuelve `legal_moves` |
| Si no queda ninguna legal | `up`, que es el ultimo escalon del fail-safe |
| Serpientes | 4, desde la posicion de media partida del benchmark |
| Aleatoriedad | `Rng` del repo con semilla fija 20260917 |
| Comida y hazards | congelados: ni spawn ni shrink (ver docs/invariants.md#inv-09) |
| Tope | 200 turnos, para que la medida no dependa de la suerte de una partida |

El tope casi nunca se alcanza: con eleccion uniforme la partida se acaba sola en unos 32
turnos, y ese numero se publica junto al playout porque es lo que dice cuantos `apply()`
hay dentro de cada uno.

## P-06 Maquina del contenedor de la nube {#p-06}

La fase 1 se construyo en el contenedor de la nube, que **no es** la maquina de referencia
de P-03: sus numeros van aparte y no sustituyen a la tabla canonica ni se comparan con
ella.

| Campo | Valor |
|---|---|
| Host | contenedor Linux, Intel Xeon a 2.10GHz |
| CPU logicas visibles | 2 |
| Memoria visible | 7 GB |
| Compilador | clang 18.1.3 |
| Fecha | 2026-09-17 |

| metrica | valor | commit | fecha |
|---|---|---|---|
| `apply()/s` (1 hilo, bench-deployisa) | 5.42 M/s (184.6 ns) | cb2c4f1 | 2026-09-17 |
| `legal_moves()/s` (1 hilo, bench-deployisa) | 18.07 M/s (55.3 ns) | cb2c4f1 | 2026-09-17 |
| `playouts/s` (1 hilo, bench-deployisa, politica de P-05) | 80.6 k/s (12.40 us) | cb2c4f1 | 2026-09-17 |
| turnos por playout | 31.9 | cb2c4f1 | 2026-09-17 |
| `decide()/s` (1 hilo, bench-deployisa) | 1.18 M/s (851.5 ns) | cb2c4f1 | 2026-09-17 |
| copias de estado/s | 88.44 M/s (11.3 ns) | cb2c4f1 | 2026-09-17 |
| `POST /move` p50 (local, 15 fixtures x 20) | 0.40 ms | cb2c4f1 | 2026-09-17 |
| `POST /move` p99 (local, 15 fixtures x 20) | 0.66 ms | cb2c4f1 | 2026-09-17 |
| `POST /move` maximo (local, 15 fixtures x 20) | 2.10 ms | cb2c4f1 | 2026-09-17 |
| arranque en frio | 2.82 ms | cb2c4f1 | 2026-09-17 |

Dos maquinas con la mitad de nucleos y otra frecuencia dan numeros distintos: la caida de
`apply()` frente a P-03 **no es una regresion medida**, es otra maquina. Para saber si la
fase 1 movio el rendimiento hay que correr `./scripts/bench.sh` en la de referencia.

## P-08 Throughput de la arena {#p-08}

`tools/sonda_arena.cpp`, un hilo, mismo contenedor de P-06 (2 CPU logicas), commit de la
fase 4. **No es la maquina de referencia.**

| `budget_nodes` | hilos | partidas/min | nodos/movimiento | un A/B de 60 partidas |
|---:|---:|---:|---:|---:|
| 500 | 1 | 17.8 | 499 | 3.4 min |
| 2 000 | 1 | 4.8 | 1 986 | 12.5 min |
| 8 000 | 1 | 1.6 | 7 904 | 37 min |
| **20 774** | **2** | **0.94** | 20 389 | **64 min** |

Turnos medios por partida: 204 con presupuesto corto, 238 con el largo. El coste es
**lineal en el presupuesto**, que es lo que se esperaba: la arena no tiene transporte que
amortizar, solo busqueda.

**Calibracion** (`sonda_arena --calibrar`, misma maquina): en 200 ms de
`time.max_compute_ms` caben **20 774 nodos** de mediana (minimo 18 595, maximo 27 987,
sobre 12 posiciones del turno 25 con cuatro vivas). Ese es el presupuesto que hace que la
arena piense como el despliegue, y **vale para esta maquina y este commit**: hay que
re-calibrar en la maquina donde se vaya a correr el A/B
(ver docs/decisions/ADR-0030-presupuesto-por-nodos.md#d-0302).

**En la maquina de referencia** (`sonda_arena --calibrar`, 8 CPU logicas, commit 9edc7fa):
**19 761 nodos** de mediana en 200 ms, con minimo 18 197 y maximo 24 010. Esta un 5% por
debajo del contenedor, que tiene la mitad de nucleos pero no comparte reloj con nadie: dos
maquinas distintas dando el mismo orden de magnitud es lo que se esperaba, y no dice nada
sobre cual es mas rapida. **El A/B de la maquina de referencia se corre con 19 761.**

La ultima fila es la que importa: a presupuesto equivalente al del despliegue, y con solo
**dos** nucleos, un A/B de 60 partidas sale en poco mas de una hora, contra las 2-3 horas
del mismo A/B por HTTP. El paralelismo escala casi lineal -8 partidas en 8m32s de reloj
consumieron 16m39s de CPU- asi que en una maquina con ocho hilos utiles son unos 16
minutos. La salida es **identica con 1 hilo y con 4**: las partidas se escriben por indice
y no segun termina cada una.

Lo que esto dice, y conviene leerlo antes de prometerse nada: **la arena no es rapida
porque quite el HTTP, es rapida porque se juega con menos presupuesto**. Un torneo de 60
partidas por HTTP tardo unas 2-3 horas; a 2 000 nodos la arena hace lo mismo en 12
minutos, pero 2 000 nodos son del orden de la decima parte de lo que cabe en los 200 ms de
`time.max_compute_ms`. A presupuesto equivalente el ahorro se queda en unas 4 veces.

**Donde se va el tiempo, medido y no supuesto.** La hipotesis era la ordenacion de
movimientos: hace hasta cuatro flood fills por llamada y se llama tres veces por nodo.
Quitandolos enteros, las mismas 20 partidas a 2 000 nodos pasaron de 251 s a **276 s**, un
10% mas LENTAS con menos trabajo por nodo. Con el numero de nodos fijado por el
presupuesto, peor ordenacion significa menos poda y mas hojas alcanzadas, y cada hoja
cuesta un Voronoi. **Manda la evaluacion de las hojas, no la ordenacion** -que es el mismo
sitio donde v4 encontro la unica mejora grande del proyecto-. La hipotesis estaba
equivocada y el experimento la mato en cinco minutos; queda escrita para que nadie la
repita.
