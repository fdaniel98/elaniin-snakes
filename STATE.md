# Estado del proyecto

## Estado actual

Fase: 1 (Motor) — **pendiente del gate completo en la maquina de referencia**
Gate: verde en el contenedor salvo el check 10, que necesita registro de imagenes
Loop: engine/src/rules.cpp → CLOSED (6 it.) en `.loop/1/`
Snake activa: v0-baseline

<!-- BEGIN:perf-snapshot -->
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
<!-- END:perf-snapshot -->

Esa tabla la regenera `./scripts/sync_state.sh` desde `docs/performance.md`, su unico
dueño; editarla a mano es un fallo que el gate detecta.

## Entregables de la fase con loop obligatorio

<!-- BEGIN:loop-deliverables -->
| slug | archivo | clases obligatorias |
|---|---|---|
| rules | engine/src/rules.cpp | correctness, robustness, perf |
<!-- END:loop-deliverables -->

Que entra aqui lo decide el ambito del loop
(ver docs/decisions/ADR-0008-ambito-del-loop.md#d-0071). Los ledgers de la fase 0 se
conservan en `.loop/0/`; el cerebro no es entregable de esta fase y no lleva ledger nuevo.

## Bloqueado / pendiente de decision humana

- [ ] **Correr el gate completo y la autoprueba en la maquina de referencia.** La fase 1
      se construyo en el contenedor de la nube, donde el registro de imagenes esta
      bloqueado: el check 10 (`docker build` mas contenedor respondiendo) y su veneno no
      se han ejecutado. En WSL2: `./scripts/gate.sh` y `./scripts/gate-selftest.sh`.
- [ ] **Publicar la linea base de la fase 1 en la maquina de referencia.** Los numeros de
      docs/performance.md#p-06 son del contenedor, con la mitad de nucleos: no sustituyen
      a la tabla canonica ni se comparan con ella. Hace falta `./scripts/bench.sh` en
      WSL2 para saber si la fase movio el rendimiento.
- [ ] **El arnes de mutantes mentia, y eso alcanza a la fase 0.** Restaurar con `mv`
      dejaba la mutacion de una cabecera dentro del binario, asi que los mutantes
      posteriores a `m3` morian por el anterior. Los ledgers de la fase 0 publicaron
      ratio 1.0 con esa lista y ese fallo. Esta arreglado desde el commit d877270, pero
      el numero de la fase 0 sigue publicado: decidir si se re-mide o se anota como
      medicion invalidada.

## Decisiones humanas del 2026-09-15

Cinco, todas con su ADR, que es donde vive el contenido: ajustes del check 9
(ver docs/decisions/ADR-0005-cierre-del-loop.md#d-0043), duplicados medidos y techo
(ver docs/decisions/ADR-0006-umbral-de-duplicados.md#d-0052, que tambien recoge el
rechazo a estrechar los checks 5 y 7), umbrales por clase
(ver docs/decisions/ADR-0007-umbrales-por-clase.md#d-0061), ambito del loop
(ver docs/decisions/ADR-0008-ambito-del-loop.md#d-0071) y arranque en frio
(ver docs/decisions/ADR-0009-entorno-y-arranque-en-frio.md#d-0081).

## Hallazgos abiertos del loop

- [ ] La tabla de causas de muerte que pide el Training Room de la fase 3 no va a tener
      contraste externo: ver docs/rules.md#r-12.
- [ ] El reparto de puestos de `placements()` sigue siendo una convencion propia. El
      diferencial verifica el turno de eliminacion y que el reparto es valido -suma
      n(n+1)/2, ningun rango fuera de rango-, pero el desempate promediado no se deriva
      de la fuente porque la fuente no lo define.
- [ ] La secuencia de lados del shrink es nuestra por decision
      (ver docs/decisions/ADR-0010-rng-del-shrink.md#d-0091): una partida de la arena no
      reproducira nunca una oficial casilla por casilla. La forma del schedule si esta
      verificada contra partidas reales (ver docs/SOURCES.md#s-02).
- [ ] `cold_start_ms_max` sigue sin veneno propio en `gate-selftest.sh`. Deuda declarada
      en docs/decisions/ADR-0009-entorno-y-arranque-en-frio.md#d-0083.
- [ ] El repositorio sigue sin remoto: toda la historia vive en un solo disco.
- [ ] `royale_hazards()` no tiene llamante todavia y su precondicion -cadencia >= 1- no
      la comprueba nadie: la arena de la fase 4 tendra que validarla antes de llamar.

## Desviaciones del arbol de archivos

Seis, todas menores y justificadas: ver docs/architecture.md#a-06.

## Siguiente accion concreta

Correr `./scripts/gate.sh` y `./scripts/gate-selftest.sh` en la maquina de referencia, que
es lo unico que falta para cerrar la fase 1, y decidir que se hace con el ratio de
mutantes de la fase 0 que el arreglo del arnes invalida.
