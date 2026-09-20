---
title: Los margenes de tiempo dejan de ser inventados
read_when: "antes de tocar time.* en snake/config/default.json o de desplegar en otra region"
authority: derived
source: scripts/verifica-despliegue.sh contra el despliegue de us-east1
last_verified: 2026-09-20
size_bytes: 7044
---

# ADR-0037 — Margenes medidos {#adr-0037}

## D-0370 Contexto {#d-0370}

`time.network_margin_ms` valia **100 ms** desde la fase 0 porque lo escribi yo, no porque
nadie lo midiera. El prompt maestro lo dice con todas las letras: ese numero se recalibra
con el p99 de RTT real medido en la fase 7. Se midio la vispera del torneo, que es tarde.

Medicion contra el despliegue real (`us-east1`, 40 repeticiones desde la maquina de
referencia):

| | p50 | p95 | p99 |
|---|---:|---:|---:|
| RTT puro (`GET /health`, sin cerebro) | 141.3 ms | 187.1 ms | **259.4 ms** |
| `POST /move` completo | 337.6 ms | 361.0 ms | 397.5 ms |

La resta cuadra con lo declarado: 337.6 - 141.3 = **196 ms de computo** contra los 200 que
`max_compute_ms` concede. El cerebro se comporta; el margen de red estaba mal por 2.6x.

## D-0371 Decision {#d-0371}

`network_margin_ms` 100 -> **260** (el p99 medido) y `max_compute_ms` 200 -> **150**.
Aprobado por el humano el 2026-09-20.

**Lo que decide es la asimetria, no el tamaño del efecto.**

Lo que cuesta: aproximadamente un nivel de profundidad. Y la profundidad esta medida:
pasar de 6 a 12 niveles movio **-0.0167** con IC95 [-0.373, +0.340]
(ver docs/experimentos.md#s-busq-r). Indistinguible de cero.

Lo que compra: 50 ms de margen contra el timeout. Un timeout no es jugar un poco peor;
es que el motor aplica el movimiento por defecto -repetir la direccion deducida de cabeza
y cuello, ver docs/rules.md#r-03 - y eso suele matar. Cambiar algo que no se distingue de
cero por proteccion contra algo que mata sale a cuenta aunque la probabilidad sea baja.

## D-0372 Lo que esta medicion NO dice {#d-0372}

El RTT es **desde la maquina de referencia**, y no se sabe desde donde arbitra el torneo.
Si sus servidores estan cerca de Virginia, el RTT real sera de decenas de milisegundos y
estos 150 ms dejan tiempo sin usar; si arbitra desde una conexion como la de la maquina de
referencia, se parecera a lo medido. Se eligio el caso malo a proposito.

`network_margin_ms` importa mas alla del numero concreto: es lo que hace que el deadline
se adapte solo si `game.timeout` del request no es 500 (regla de oro 5). Con 260 puesto,
un timeout mas corto encoge el computo en vez de reventar.

La medicion que falta, y que es la unica que vale de verdad: partidas completas contra la
URL con el arbitro oficial, leyendo las latencias que reporta. `curl` mide el transporte;
solo una partida mide la snake.

## D-0373 Alternativas descartadas {#d-0373}

| Alternativa | Por que no |
|---|---|
| Dejar 200 ms y confiar | El margen declarado ya se sabe falso por 2.6x; confiar no es una decision |
| Bajar a 120 ms | Mas seguro todavia, pero empieza a costar profundidad sin evidencia de que haga falta |
| Subir `safety_margin_ms` en vez del computo | Es el mismo tiempo por otro nombre, y el que se mide por separado es el de red |
| Desplegar mas cerca del arbitro | No se sabe donde arbitra, y a estas horas cambiar de region es mas riesgo que ganancia |

## D-0374 El 260 estaba inflado, y la correccion importa {#d-0374}

La partida real contra la URL desplegada dio latencias **muy** por debajo de lo que
predecia `curl`: 192 ms de mediana y 207 de maximo vistos por el arbitro, con 148 de
computo dentro. El transporte real es de **~44 ms**, no de 259
(ver docs/performance.md#p-09). La diferencia es la reutilizacion de conexion: `curl` paga
un handshake TLS por invocacion y el arbitro lo paga una vez por partida.

Eso deja `network_margin_ms = 260` **demasiado alto**, y no es inocuo. Con `timeout` 500 da
igual -el techo de 150 es el que manda-, pero si alguna partida anunciara un timeout mas
corto, `500 -> 300` daria `300 - 260 - 50 = -10`, que `Deadline::from_timeout` acota a
**1 ms**: la snake jugaria por ordenacion estatica, sin buscar. Un margen inventado por
arriba es tan peligroso como uno inventado por abajo, solo que falla en otro sitio.

## D-0376 Se baja el margen de red a 80, y solo ese {#d-0376}

`network_margin_ms` pasa de **260 a 80**. El transporte p99 que reporta el arbitro es de
57 ms (207 de total menos los 150 de computo), asi que 80 deja un 40% de holgura sobre el
dato real en vez de un 4.5x sobre un dato contaminado por el handshake.

**A `timeout` 500 el cambio no altera nada**: `500 - 80 - 50 = 370` y `500 - 260 - 50 =
190` se recortan los dos al techo de 150. Lo que compra es el caso que D-0374 describe:

| `timeout` anunciado | margen 260 | margen 80 |
|---|---|---|
| 500 | 150 ms | 150 ms |
| 400 | 90 ms | 150 ms |
| 300 | **1 ms** (juega a ciegas) | 150 ms |
| 250 | **1 ms** | 120 ms |

El JSONL de la partida real anuncia `timeout: 500` en los 36 turnos, asi que hoy esto es
un seguro, no una mejora. Se contrata porque su prima es cero y el siniestro -jugar sin
buscar una partida entera- no tiene arreglo en caliente.

`_version` pasa a `v5-longitud-150ms-m80`, que es como se comprueba desde `GET /` que el
redespliegue aterrizo.

**El check nuevo**: `tests/test_brain_v0.cpp` seccion «un timeout mas corto que 500 sigue
dejando buscar», que exige presupuesto >= 100 ms y `presupuesto + 57 < timeout` para 250,
300, 400 y 500. Ningun fixture lleva `timeout` propio, asi que el gate no veia este caso.

## D-0377 Lo que NO se toca: el techo de computo {#d-0377}

Se evaluo subir `max_compute_ms` de 150 a 300, que es lo unico que aportaria fuerza real.
**No se hace.** `bin/sonda_busqueda` sobre 37 posiciones de media partida con 4 serpientes
vivas (ver docs/performance.md#p-10):

| presupuesto | profundidad media | nodos/movimiento | donde se fija el mejor movimiento |
|---|---|---|---|
| 150 ms | 6.2 | 22 675 | profundidad 2.9 |
| 300 ms | 6.6 | 45 692 | profundidad 3.0 |

El doble de tiempo da el doble de nodos y **+0.4 niveles**, y el movimiento elegido se fija
igual en la profundidad ~3: 54 de 99 profundidades a 300 ms son confirmatorias, no cambian
la decision. Concuerda con el A/B previo de 6 a 12 niveles, que dio -0.0167 con el IC95
cruzando el cero (ver docs/experimentos.md#s-busq-r).

Lo que si cambiaria es la exposicion: 150 ms mas por movimiento a cambio de una ganancia no
medible. `arena_torneo` ademas fuerza el mismo `budget_nodes` a las dos ramas por contrato
(ver docs/decisions/ADR-0030-presupuesto-por-nodos.md#d-0302), asi que un A/B de N contra
2N nodos no se puede correr sin romper el invariante que hace pareada la comparacion.

## D-0375 Estado {#d-0375}

**ACEPTADA Y VERIFICADA EN PRODUCCION**: 337 movimientos contra la URL desplegada, 0 por
encima del timeout, maximo 230 ms de 500 (ver docs/performance.md#p-11).
`network_margin_ms` = 80 (D-0376). `max_compute_ms` se queda en 150 (D-0377):
a 207 ms de maximo sobre un timeout de 500 hay 293 ms de aire, y la profundidad extra que
compraria subirlo no es medible.
