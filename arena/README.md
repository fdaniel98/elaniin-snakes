# arena/ - [FASE 4] self-play in-process

Enfrenta variantes compiladas del cerebro **sin HTTP**, con presupuesto determinista por
nodos (`budget_nodes`) y nunca por reloj. El motivo, en la skill `experiment-protocol`,
apartado de lo que invalida un resultado.

## Contrato previsto

La firma real esta en `arena/include/arena/arena.hpp`. El presupuesto por nodos no vive en
`ArenaConfig` sino en `Params::search::budget_nodes`, que es donde lo lee la busqueda y
donde puede ser distinto por contendiente. El modelo de shrink todavia no es un parametro:
la arena regenera los hazards con `royale_hazards()` y el cerebro los ve en el tablero.

- El payload de `/move` no trae la semilla, asi que en partida real el lado del proximo
  shrink no es conocible (ver docs/rules.md#r-09). Dentro de la arena si lo es, y modelarlo
  ahi seria medir una snake que no se puede desplegar: por eso el cerebro sigue viendo solo
  el tablero.
- El sorteo de comida y hazards **no se materializa**: se indexa por turno, sembrando un
  `Rng` con `semilla + turno` en cada llamada. Las casillas no se pueden fijar por
  adelantado porque dependen de la ocupacion, que depende de la partida; lo que se fija son
  los numeros. ver docs/decisions/ADR-0029-schedule-por-turno.md#d-0291
- Usa el mismo `decide()` que el servidor (ver docs/architecture.md#a-02).

## Lo que mide y lo que no

Aqui solo juegan configuraciones NUESTRAS: las del zoo son contenedores HTTP. Un veredicto
de arena es sobre self-play y **nada entra en `default.json` por el**; la puerta sigue
siendo el A/B por HTTP contra `gauntlet-v1`. La arena sirve para triaje y para afinado.
ver docs/decisions/ADR-0031-que-mide-la-arena.md#d-0311

Y es rapida porque juega con menos presupuesto por movimiento, no porque quite el
transporte: ver docs/performance.md#p-08.

## Estado

Implementada: `arena::play()` juega una partida entera y es reproducible. Falta el driver
de A/B por bloques y el paralelismo entre partidas.
