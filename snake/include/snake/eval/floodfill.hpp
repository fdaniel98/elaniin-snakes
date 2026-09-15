#pragma once

/// @file floodfill.hpp
/// Espacio alcanzable por dilatacion de bitboard: cada iteracion expande el frente
/// un turno. No es un stub porque `brain_v0` lo necesita para rechazar movimientos
/// hacia regiones mas pequeñas que la propia serpiente.
/// ver docs/context-packs/nueva-heuristica.md

#include <engine/bitboard.hpp>
#include <engine/state.hpp>

namespace snake::eval {

/// Resultado del flood fill: casillas alcanzables y en cuantos turnos se llega a la
/// casilla objetivo (-1 si no se alcanza).
struct Reach {
    int cells{0};
    int turns_to_target{-1};
};

/// Expande desde `start` sobre `free_cells` hasta saturar o agotar `max_turns`.
///
/// `target` (indice de casilla, o -1) permite preguntar en cuantos turnos se alcanza
/// una casilla concreta; es como se implementa el tail-escape: la cola propia cuenta
/// como libre si se llega a ella en al menos los turnos que tarda en liberarse.
/// ver docs/rules.md#r-04
template <int W, int H>
[[nodiscard]] Reach flood(const engine::Bitboard<W, H>& free_cells,
                          int start,
                          int target = -1,
                          int max_turns = W * H) noexcept {
    using Board = engine::Bitboard<W, H>;
    Reach result;
    if (start < 0 || !free_cells.test(start)) {
        return result;
    }

    Board frontier;
    frontier.set(start);
    Board seen = frontier;
    if (target == start) {
        result.turns_to_target = 0;
    }

    for (int turn = 1; turn <= max_turns; ++turn) {
        const Board next = (frontier.expand() & free_cells).without(seen);
        if (next.none()) {
            break;
        }
        if (result.turns_to_target < 0 && target >= 0 && next.test(target)) {
            result.turns_to_target = turn;
        }
        seen |= next;
        frontier = next;
    }
    result.cells = seen.count();
    return result;
}

} // namespace snake::eval
