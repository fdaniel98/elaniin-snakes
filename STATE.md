# Estado del proyecto

## Estado actual

Fase: 1 (Motor) — **PARCIAL**: la DoD se cumple, el loop no cierra
Gate: checks 0-8 y 10 en verde en el contenedor (el 10 SKIP, sin registro de imagenes);
el check 9 falla a proposito porque el ledger esta BLOQUEADO
Loop: engine/src/rules.cpp → **BLOQUEADO** en el techo de 8 iteraciones, en `.loop/1/`
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

- [ ] **El loop toco el techo de 8 iteraciones sin cerrar.** Las auditorias del criterio
      14 encontraron 11 hallazgos, reparados en `ab0ccf4`; cerrar exige dos rondas
      limpias mas. El motivo completo, en `blocked_reason` de
      `.loop/1/rules.ledger.json`. Tres salidas, y la decision es humana: subir el techo
      con su ADR, aceptar el cierre con una ronda limpia en vez de dos (tambien con ADR,
      porque relaja la regla), o correr i9 e i10 en la maquina de referencia.

- [ ] **Gate completo y autoprueba en la maquina de referencia.** El contenedor no tiene
      registro de imagenes, asi que el check 10 y su veneno no se han ejecutado.
- [ ] **Linea base de la fase 1 en la maquina de referencia:** `./scripts/bench.sh` en
      WSL2. Lo medido hasta ahora, en ver docs/performance.md#p-06, es de otra maquina.
- [ ] **El arnes de mutantes mentia y eso alcanza a la fase 0:** los ledgers de la fase 0
      publicaron ratio 1.0 con el fallo dentro (arreglado en `d877270`). Decidir si se
      re-mide o se anota como medicion invalidada.

## Decisiones humanas

Cada una con su ADR, que es donde vive el contenido: `docs/decisions/`. Las de la fase 1
son el Rng propio del shrink (ver docs/decisions/ADR-0010-rng-del-shrink.md#d-0091) y el
corpus del diferencial (ver docs/decisions/ADR-0011-corpus-del-diferencial.md#d-0101).

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
