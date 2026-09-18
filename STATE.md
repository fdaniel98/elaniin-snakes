# Estado del proyecto

## Estado actual

Fase: 2 (Servidor endurecido) — pendiente de las 200 partidas en la maquina de referencia
Gate: la fase 1 cerro con 12 checks PASS y 25 venenos cazados alli
Loop: snake/src/server.cpp → en `.loop/2/`; el de la fase 1 quedo CLOSED en `.loop/1/`
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

La regenera `./scripts/sync_state.sh` desde su dueño, `docs/performance.md`.

## Entregables de la fase con loop obligatorio

<!-- BEGIN:loop-deliverables -->
| slug | archivo | clases obligatorias |
|---|---|---|
| server | snake/src/server.cpp | correctness, robustness, perf |
<!-- END:loop-deliverables -->

Que entra aqui lo decide ver docs/decisions/ADR-0008-ambito-del-loop.md#d-0071.

## Bloqueado / pendiente de decision humana


- [ ] **Linea base de la fase 1 en la maquina de referencia:** `./scripts/bench.sh` en
      WSL2. Lo medido hasta ahora, en ver docs/performance.md#p-06, es de otra maquina.

## Decisiones humanas

Cada una con su ADR, que es donde vive el contenido: `docs/decisions/`. Las de la fase 1
son el Rng propio del shrink (ver docs/decisions/ADR-0010-rng-del-shrink.md#d-0091) y el
corpus del diferencial (ver docs/decisions/ADR-0011-corpus-del-diferencial.md#d-0101).

## Hallazgos abiertos del loop

- [ ] El servidor sigue siendo agotable por conexiones a medio abrir: con el pool de 64
      hilos hacen falta 64 a la vez, y entonces una peticion legitima espera hasta el
      read timeout de 2 s. El pool aleja el accidente y el timeout acota el caso peor; no
      lo elimina. En la fase 7 el servicio va detras de un balanceador que absorbe eso.

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

Correr `./scripts/soak.sh --games 200` en la maquina de referencia: es la DoD de la fase 2
y lo unico que falta. Los numeros del contenedor, con la mitad de nucleos, no sirven para
publicar (ver docs/performance.md#p-06).
