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

/// La REGION alcanzable desde `start`, como bitboard (incluye `start`).
///
/// `flood` devuelve cuantas casillas hay; esto devuelve cuales. Hace falta para dos
/// preguntas que el conteo no responde: si nuestra cola cae dentro de la region -y
/// entonces se puede dar vueltas detras de ella indefinidamente- y si la region del rival
/// toca la nuestra -y entonces el duelo todavia es un juego y no dos solitarios-.
/// ver docs/strategy.md#s-supervivencia-duelo
template <int W, int H>
[[nodiscard]] engine::Bitboard<W, H> region(const engine::Bitboard<W, H>& free_cells,
                                            int start) noexcept {
    using Board = engine::Bitboard<W, H>;
    Board vista;
    if (start < 0 || !free_cells.test(start)) {
        return vista;
    }
    Board frontier;
    frontier.set(start);
    vista = frontier;
    for (int turn = 1; turn <= W * H; ++turn) {
        const Board next = (frontier.expand() & free_cells).without(vista);
        if (next.none()) {
            break;
        }
        vista |= next;
        frontier = next;
    }
    return vista;
}

/// Espacio que queda en el PEOR caso si una sola casilla se cierra.
///
/// El flood fill dice cuanto hueco hay ahora. Esto dice cuanto quedaria si el rival
/// tapase la casilla que mas duele: recorre las casillas alcanzables desde `start`, quita
/// cada una, y se queda con el menor componente que contendria a `start`. Una region que
/// depende de un cuello de botella devuelve el tamaño del lado en el que uno se queda.
///
/// Por que importa, con el numero delante: 132 de 178 muertes del torneo de v0 no tenian
/// ninguna direccion que sobreviviera ese turno. La trampa no se tiende en el turno en
/// que se muere, se tiende al entrar en una region con una sola puerta.
/// ver docs/strategy.md#s-v1
///
/// Coste: un flood fill por casilla candidata a cuello. Se acota con `max_cuellos` para
/// no pagar 121 floods en un tablero abierto, donde ademas no hay cuellos que encontrar.
template <int W, int H>
[[nodiscard]] int worst_case_space(const engine::Bitboard<W, H>& free_cells,
                                   int start,
                                   int max_cuellos = 24) noexcept {
    using Board = engine::Bitboard<W, H>;
    if (start < 0 || !free_cells.test(start)) {
        return 0;
    }
    const int total = flood(free_cells, start).cells;
    if (total <= 1) {
        return total;
    }

    // Solo son candidatas a cuello las casillas alcanzables, sin contar la de partida:
    // cerrar una casilla a la que no se llega no cambia nada.
    Board alcanzable;
    {
        Board frontier;
        frontier.set(start);
        alcanzable = frontier;
        for (int turn = 1; turn <= W * H; ++turn) {
            const Board next = (frontier.expand() & free_cells).without(alcanzable);
            if (next.none()) {
                break;
            }
            alcanzable |= next;
            frontier = next;
        }
    }

    int peor = total;
    int probadas = 0;
    for (int cell = 0; cell < W * H && probadas < max_cuellos; ++cell) {
        if (cell == start || !alcanzable.test(cell)) {
            continue;
        }
        // Una casilla con 2 vecinos libres o menos no puede partir nada que importe; con
        // 4, tampoco suele. Se prueban las de grado 2 y 3, que son los cuellos de verdad.
        Board una;
        una.set(cell);
        const int grado = (una.expand().without(una) & free_cells).count();
        if (grado < 2 || grado > 3) {
            continue;
        }
        ++probadas;
        Board sin = free_cells;
        sin.reset(cell);
        const int queda = flood(sin, start).cells;
        if (queda < peor) {
            peor = queda;
        }
    }
    return peor;
}

} // namespace snake::eval
