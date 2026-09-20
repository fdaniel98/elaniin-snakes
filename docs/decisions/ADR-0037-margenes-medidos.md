---
title: Los margenes de tiempo dejan de ser inventados
read_when: "antes de tocar time.* en snake/config/default.json o de desplegar en otra region"
authority: derived
source: scripts/verifica-despliegue.sh contra el despliegue de us-east1
last_verified: 2026-09-20
size_bytes: 3458
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

## D-0374 Estado {#d-0374}

**ACEPTADA.** `_version` pasa a `v5-longitud-150ms` para que `GET /` distinga este
despliegue del anterior sin tener que mirar el hash de la imagen.
