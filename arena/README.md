# arena/ - [FASE 4] self-play in-process

Enfrenta variantes compiladas del cerebro **sin HTTP**, con presupuesto determinista por
nodos (`budget_nodes`) y nunca por reloj. El motivo, en la skill `experiment-protocol`,
apartado de lo que invalida un resultado.

## Contrato previsto

```cpp
struct ArenaConfig {
    std::uint64_t seed;          // semilla de la partida; 0 esta prohibido
    int budget_nodes;            // presupuesto por movimiento, calibrado contra deploy
    ShrinkModel shrink;          // ignore | pessimistic_edges | exact_schedule_arena
};

ArenaResult play(const ArenaConfig&, std::span<const Params> contenders);
```

- `exact_schedule_arena` esta restringido **por contrato** a la arena, porque el payload
  de `/move` no trae la semilla (ver docs/rules.md#r-09).
- El sorteo de comida y hazards **no se materializa**: se indexa por turno, sembrando un
  `Rng` con `semilla + turno` en cada llamada. Las casillas no se pueden fijar por
  adelantado porque dependen de la ocupacion, que depende de la partida; lo que se fija son
  los numeros. ver docs/decisions/ADR-0029-schedule-por-turno.md#d-0291
- Usa el mismo `decide()` que el servidor (ver docs/architecture.md#a-02).

Estado: no implementado. Lo abre la fase 4.
