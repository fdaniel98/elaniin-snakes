/// @file sonda_cuellos.cpp
/// Cuantas veces se dispara la penalizacion de cuellos, y cuanto penaliza.
///
/// Existe porque el A/B de `cuellos` salio NO CONCLUYENTE con los turnos vividos
/// practicamente iguales (119.2 -> 120.1), y eso admite dos lecturas opuestas: o la
/// heuristica no se activo nunca, o se activa siempre y no discrimina. Publicar la
/// primera cuando era la segunda habria sido escribir una leccion falsa.
///
/// Respuesta: se dispara en el 92.9% de los estados. ver docs/experimentos.md#s-cuellos-r
#include <cstdio>

#include <engine/rng.hpp>
#include <engine/rules.hpp>
#include <engine/state.hpp>

#include <snake/eval/floodfill.hpp>
#include <snake/params.hpp>

using State = engine::State11;
using Board = State::Board;

int main() {
    engine::Rng rng(20260918);
    int total = 0, difiere = 0, grande = 0;
    long long perdido_total = 0;
    for (int i = 0; i < 20000; ++i) {
        State s{};
        s.snake_count = 4;
        s.you = 0;
        for (int k = 0; k < 4; ++k) {
            auto& sn = s.snakes[static_cast<unsigned>(k)];
            sn.head_slot = 0;
            sn.health = 100;
            sn.status = engine::Elimination::alive;
            sn.eliminated_on_turn = -1;
            sn.length = static_cast<std::uint16_t>(3 + rng.next() % 9);
            int c = static_cast<int>(rng.next() % State::cells);
            for (int seg = 0; seg < sn.length; ++seg) {
                sn.cells[static_cast<unsigned>(seg)] = static_cast<std::uint16_t>(c);
                int d = static_cast<int>(rng.next() % 4);
                auto p = Board::coord_of(c);
                engine::Coord n{static_cast<std::int8_t>(p.x + (d == 0) - (d == 1)),
                                static_cast<std::int8_t>(p.y + (d == 2) - (d == 3))};
                if (Board::in_bounds(n)) {
                    c = Board::index_of(n);
                }
            }
        }
        s.refresh_occupancy();
        Board libres = Board::full().without(s.bodies);
        int cabeza = s.snake(0).head();
        libres.set(cabeza);
        const int esp = snake::eval::flood(libres, cabeza).cells;
        const int peor = snake::eval::worst_case_space(libres, cabeza, 24);
        ++total;
        if (peor < esp) {
            ++difiere;
            perdido_total += esp - peor;
        }
        if (esp - peor > 10) {
            ++grande;
        }
    }
    std::printf("estados=%d  con cuello=%d (%.1f%%)  perdida media=%.2f casillas  "
                "cuellos grandes(>10)=%d (%.2f%%)\n",
                total,
                difiere,
                100.0 * difiere / total,
                difiere ? double(perdido_total) / difiere : 0.0,
                grande,
                100.0 * grande / total);
    return 0;
}
