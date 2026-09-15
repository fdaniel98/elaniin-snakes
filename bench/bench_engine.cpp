/// @file bench_engine.cpp
/// Mide `apply()/s`, `legal_moves()/s` y `decide()/s`. Los numeros medidos se publican
/// en docs/performance.md, que es su unico dueño.
///
/// Los resultados con el preset `bench` llevan `-march=native` y se etiquetan
/// `local-only`: no pueden usarse como linea base. La linea base publicable se mide con
/// el preset `bench-deployisa`, que usa la misma ISA que deploy.
/// ver docs/performance.md#p-02

#include <array>
#include <chrono>
#include <vector>

#include <engine/rules.hpp>
#include <engine/state.hpp>

#include <snake/brain.hpp>

#include <benchmark/benchmark.h>

namespace {

using State = engine::State11;
using Board = State::Board;
using engine::Coord;
using engine::Direction;

/// Posicion tipica de media partida: 4 serpientes desplegadas, comida y hazards.
State midgame() {
    State state;
    state.snake_count = 4;
    state.you = 0;
    state.rules.hazard_damage_per_turn = 14;

    const std::array<std::array<Coord, 5>, 4> bodies{{
        {Coord{5, 5}, Coord{4, 5}, Coord{3, 5}, Coord{3, 4}, Coord{3, 3}},
        {Coord{7, 7}, Coord{7, 8}, Coord{7, 9}, Coord{6, 9}, Coord{5, 9}},
        {Coord{2, 8}, Coord{1, 8}, Coord{1, 7}, Coord{1, 6}, Coord{1, 5}},
        {Coord{8, 2}, Coord{9, 2}, Coord{9, 3}, Coord{9, 4}, Coord{8, 4}},
    }};

    for (int i = 0; i < 4; ++i) {
        auto& snake = state.snakes[static_cast<unsigned>(i)];
        snake.head_slot = 0;
        snake.length = 5;
        snake.health = static_cast<std::uint8_t>(80 - i * 10);
        snake.status = engine::Elimination::alive;
        for (int seg = 0; seg < 5; ++seg) {
            snake.cells[static_cast<unsigned>(seg)] = static_cast<std::uint16_t>(
                Board::index_of(bodies[static_cast<unsigned>(i)][static_cast<unsigned>(seg)]));
        }
    }

    state.food.set(Board::index_of(Coord{5, 1}));
    state.food.set(Board::index_of(Coord{0, 10}));
    for (int y = 0; y < 11; ++y) {
        state.hazards.set(Board::index_of(Coord{10, static_cast<std::int8_t>(y)}));
    }
    state.refresh_occupancy();
    return state;
}

void bm_apply(benchmark::State& bench) {
    const State base = midgame();
    const std::array<Direction, 4> moves{
        Direction::up, Direction::left, Direction::down, Direction::right};
    for (auto _ : bench) {
        State copy = base; // copy-make: el estado es trivialmente copiable
        benchmark::DoNotOptimize(engine::apply(copy, std::span<const Direction>(moves)));
    }
    bench.SetItemsProcessed(bench.iterations());
}

void bm_legal_moves(benchmark::State& bench) {
    const State base = midgame();
    for (auto _ : bench) {
        benchmark::DoNotOptimize(engine::legal_moves(base, 0));
    }
    bench.SetItemsProcessed(bench.iterations());
}

void bm_copy_state(benchmark::State& bench) {
    const State base = midgame();
    for (auto _ : bench) {
        State copy = base;
        benchmark::DoNotOptimize(copy);
    }
    bench.SetItemsProcessed(bench.iterations());
}

void bm_decide(benchmark::State& bench) {
    const State base = midgame();
    const snake::Params params;
    for (auto _ : bench) {
        const auto deadline =
            snake::Deadline(snake::Deadline::Clock::now() + std::chrono::milliseconds(350));
        benchmark::DoNotOptimize(snake::decide(base, deadline, params));
    }
    bench.SetItemsProcessed(bench.iterations());
}

} // namespace

BENCHMARK(bm_apply);
BENCHMARK(bm_legal_moves);
BENCHMARK(bm_copy_state);
BENCHMARK(bm_decide);

BENCHMARK_MAIN();
