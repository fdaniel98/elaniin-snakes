# arena/ - [FASE 4] self-play in-process

Enfrenta variantes compiladas del cerebro **sin HTTP**, con presupuesto determinista por
nodos (`budget_nodes`), nunca por reloj: una busqueda anytime con deadline de reloj
devuelve movimientos distintos segun la carga de la maquina, y eso destruye la
reproducibilidad que un A/B necesita.

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
- El sorteo de comida y hazards se resuelve entero por adelantado; el porque, en la skill
  `experiment-protocol`.
- Usa el mismo `decide()` que el servidor (ver docs/architecture.md#a-02).

Estado: no implementado. Lo abre la fase 4.
