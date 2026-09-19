---
title: Los tests de deadline admiten 25 ms de holgura, porque miden la maquina y no el cerebro
read_when: "antes de tocar k_holgura_reloj_ms o de investigar un test de deadline que falla a veces"
authority: derived
source: tools/sonda_overshoot.cpp
last_verified: 2026-09-19
size_bytes: 3356
---

# ADR-0027 — 25 ms de holgura en los tests de deadline {#adr-0027}

## Contexto {#adr-0027-contexto}

Desde que la busqueda esta encendida por defecto, los tests que comprueban INV-11 con
presupuestos de 1 y 5 ms fallan **a veces**: en release unas corridas y en debug otras, sin
patron. Han costado cuatro rondas de investigacion en esta sesion, y las cuatro terminaron
en el mismo sitio: el codigo no se pasa, la maquina se para.

Lo medido con `tools/sonda_overshoot.cpp` en una maquina tranquila:

| presupuesto | p50 | MAX | fuera de presupuesto |
|---|---|---|---|
| 5 ms | 3005 us | 4478 us | **0 de 3000** |
| 20 ms | 18005 us | 18132 us | **0 de 3000** |
| 50 ms | 48005 us | 48080 us | **0 de 3000** |
| 200 ms | 198005 us | 198264 us | **0 de 500** |

El p50 clava el deadline interno con **5 us** de margen. Con la maquina cargada aparecen
paradas de 1 a 3 ms, que es el mismo host que dio 9 ms de arranque en frio
(ver docs/decisions/ADR-0021-arranque-en-frio.md) y 2 violaciones de 10 000 en la fase 2.

## El problema de fondo {#adr-0027-problema}

**Un test de reloj no puede distinguir «nuestro codigo se paso» de «el planificador nos
quito la CPU».** Con un presupuesto de 200 ms esa ambiguedad no molesta, porque una parada
de 2 ms es el 1%. Con uno de 5 ms es el 40%, y el test deja de medir el cerebro para medir
la maquina.

Un test que falla al azar es peor que no tenerlo: enseña a mirar el rojo y encogerse de
hombros.

## Decision {#adr-0027-decision}

Las comprobaciones de deadline admiten `k_holgura_reloj_ms = 25`.

**Por que 25 y no 5 ni 100:** un rebasamiento ALGORITMICO no son 25 ms. Si la busqueda
dejara de mirar el reloj, tardaria el presupuesto entero -200 ms- o el que le permitiera
`max_depth`. La holgura separa una parada del sistema (1-3 ms medidos) de un defecto real
(dos ordenes de magnitud mayor) con un factor de 8 por arriba y de 8 por abajo.

**El presupuesto de PRODUCCION se queda estricto.** El test que comprueba los 200 ms
derivados del timeout real exige `<= 250 ms` sin holgura adicional: esos 50 ms de margen ya
la contienen, y es el unico regimen en el que la snake corre de verdad.

## Alternativas descartadas {#adr-0027-alternativas}

- **Dejarlo en cero y convivir con el rojo aleatorio.** Es la peor: un gate que falla al
  azar deja de ser un arbitro.
- **Subir los presupuestos de los tests a 50 ms.** Deja de probar el regimen apretado, que
  es donde un fallo de la logica de parada se veria primero.
- **Reintentar el test cuando falla.** Esconde exactamente lo que habria que ver si algun
  dia el rebasamiento SI fuera nuestro.
- **Medir el tiempo de CPU en vez del de reloj.** Es lo correcto en teoria y no sirve aqui:
  lo que el arbitro cuenta contra los 500 ms es reloj de pared, no CPU.

## Lo que esto NO tapa {#adr-0027-no-tapa}

`tools/sonda_overshoot.cpp` sigue midiendo la distribucion completa sin holgura ninguna, y
es donde hay que mirar si se sospecha un rebasamiento de verdad. Los numeros de la tabla de
arriba se rehacen con un comando y sin interpretar nada.
