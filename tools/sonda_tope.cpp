/// @file sonda_tope.cpp
/// Mide si `search.max_depth` deja tiempo sin usar. ver docs/strategy.md#s-busq
#include <chrono>
#include <cstdio>

#include <engine/rng.hpp>
#include <engine/rules.hpp>

#include <snake/brain.hpp>
#include <snake/search.hpp>
using Board = engine::State11::Board;

static engine::State11 gen(engine::Rng& rng, int vivas) {
    engine::State11 s{};
    s.snake_count = static_cast<std::uint8_t>(vivas);
    s.you = 0;
    for (int k = 0; k < vivas; ++k) {
        auto& sn = s.snakes[static_cast<unsigned>(k)];
        sn.head_slot = 0;
        sn.health = static_cast<std::uint8_t>(40 + rng.next() % 60);
        sn.status = engine::Elimination::alive;
        sn.eliminated_on_turn = -1;
        sn.length = static_cast<std::uint16_t>(6 + rng.next() % 8);
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

int main() {
    for (const int vivas : {2, 3, 4}) {
        std::printf("\n=== %d serpientes vivas ===\n", vivas);
        for (const int terr : {0, 1}) {
            snake::Params p;
            p.search.version = 1;
            p.territory.version = terr;
            snake::warmup(p);
            engine::Rng rng(20260918);
            long long prof = 0, us_acum = 0, tope_us = 0;
            int n = 0, tocan_tope = 0;
            for (int i = 0; i < 40; ++i) {
                auto st = gen(rng, vivas);
                if (engine::legal_moves(st, st.you) == engine::move_mask_none) {
                    continue;
                }
                const auto t0 = snake::Deadline::Clock::now();
                const snake::Deadline d(t0 + std::chrono::milliseconds(200));
                const auto r = snake::search(st, d, p);
                const auto us =
                    static_cast<long long>(std::chrono::duration_cast<std::chrono::microseconds>(
                                               snake::Deadline::Clock::now() - t0)
                                               .count());
                prof += r.depth;
                us_acum += us;
                tope_us = std::max(tope_us, us);
                if (r.depth >= p.search.max_depth) {
                    ++tocan_tope;
                }
                ++n;
            }
            std::printf("  territorio=%d -> profundidad %.2f | %6lld us medios (de 200000) | "
                        "%d de %d tocan el tope\n",
                        terr,
                        double(prof) / n,
                        us_acum / n,
                        tocan_tope,
                        n);
        }
    }
    return 0;
}
