/// @file sonda_overshoot.cpp
/// Cuanto se pasa `decide()` de su presupuesto, y con que frecuencia.
///
/// Existe porque INV-11 dice que el deadline no se excede, y con la busqueda encendida eso
/// dejo de ser trivial. Mide la DISTRIBUCION, no un caso: el p50 clava el deadline y lo que
/// importa es el tail. ver docs/invariants.md#inv-11
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include <engine/rng.hpp>
#include <engine/rules.hpp>

#include <snake/brain.hpp>
#include <snake/search.hpp>

namespace {
using Board = engine::State11::Board;

engine::State11 gen(engine::Rng& rng) {
    engine::State11 s{};
    s.snake_count = 4;
    s.you = 0;
    for (int k = 0; k < 4; ++k) {
        auto& sn = s.snakes[static_cast<unsigned>(k)];
        sn.head_slot = 0;
        sn.health = static_cast<std::uint8_t>(1 + rng.next() % 100);
        sn.status = engine::Elimination::alive;
        sn.eliminated_on_turn = -1;
        sn.length = static_cast<std::uint16_t>(3 + rng.next() % 10);
        int c = static_cast<int>(rng.next() % engine::State11::cells);
        for (int seg = 0; seg < sn.length; ++seg) {
            sn.cells[static_cast<unsigned>(seg)] = static_cast<std::uint16_t>(c);
            const int d = static_cast<int>(rng.next() % 4);
            const auto pos = Board::coord_of(c);
            const engine::Coord n{static_cast<std::int8_t>(pos.x + (d == 0) - (d == 1)),
                                  static_cast<std::int8_t>(pos.y + (d == 2) - (d == 3))};
            if (Board::in_bounds(n)) {
                c = Board::index_of(n);
            }
        }
    }
    for (int i = 0; i < 3; ++i) {
        s.food.set(static_cast<int>(rng.next() % engine::State11::cells));
    }
    s.refresh_occupancy();
    return s;
}
} // namespace

int main(int argc, char** argv) {
    const int presupuesto = argc > 1 ? std::atoi(argv[1]) : 5;
    const int n = argc > 2 ? std::atoi(argv[2]) : 10000;

    snake::Params p;
    snake::warmup(p);
    engine::Rng rng(20260915);

    std::vector<long long> us;
    us.reserve(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        const engine::State11 st = gen(rng);
        const auto t0 = snake::Deadline::Clock::now();
        (void)snake::decide(st, snake::Deadline(t0 + std::chrono::milliseconds(presupuesto)), p);
        us.push_back(static_cast<long long>(std::chrono::duration_cast<std::chrono::microseconds>(
                                                snake::Deadline::Clock::now() - t0)
                                                .count()));
    }
    std::sort(us.begin(), us.end());

    const auto idx = [&](double q) {
        return us[static_cast<std::size_t>(static_cast<double>(n - 1) * q)];
    };
    const long long techo = static_cast<long long>(presupuesto) * 1000;
    const std::size_t fuera =
        us.size() -
        static_cast<std::size_t>(std::lower_bound(us.begin(), us.end(), techo + 1) - us.begin());
    std::printf("presupuesto %4d ms | p50=%6lld p99=%6lld p999=%7lld MAX=%7lld us | "
                "fuera: %zu de %d (%.3f%%)\n",
                presupuesto,
                idx(0.50),
                idx(0.99),
                idx(0.999),
                us.back(),
                fuera,
                n,
                100.0 * static_cast<double>(fuera) / n);
    return 0;
}
