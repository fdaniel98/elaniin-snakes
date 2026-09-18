#pragma once

/// @file voronoi.hpp
/// [v1] Control de territorio por BFS simultaneo desde todas las cabezas.
///
/// Por que existe, con el numero delante: `brain_v0` mide el espacio con un flood fill
/// desde su propia cabeza, o sea las casillas que EXISTEN. En el torneo de 200 partidas
/// contra `gauntlet-v1`, **132 de 178 muertes nuestras no tenian ninguna direccion que
/// sobreviviera ese turno**: no se pierde el duelo del turno, se pierde la posicion mucho
/// antes. Un hueco que el rival sella primero nunca fue nuestro, y el flood fill lo
/// contaba igual. ver docs/strategy.md#s-v1
///
/// Aqui el frente avanza un turno a la vez desde TODAS las cabezas vivas. Una casilla es
/// de quien llega estrictamente antes; en empate de distancia gana la mas larga, porque en
/// una colision de cabezas la mas larga sobrevive (ver docs/rules.md#r-08). Las casillas
/// que empatan entre serpientes de la misma longitud no son de nadie: matan a las dos.
///
/// Los hazards no bloquean -se puede pasar por ellos- pero se cuentan a la baja: pisar
/// hazard cuesta salud, asi que ese territorio vale menos que el limpio.

#include <array>
#include <cstddef>
#include <cstdint>

#include <engine/bitboard.hpp>
#include <engine/state.hpp>
#include <engine/types.hpp>

namespace snake::eval {

/// Casillas que cada serpiente alcanza antes que las demas.
template <int MaxSnakes> struct Territory {
    std::array<int, static_cast<std::size_t>(MaxSnakes)> cells{};
    /// Casillas que dos o mas alcanzan a la vez sin que ninguna gane el empate.
    int contested{0};
    /// El mismo territorio descontando lo que valen menos las casillas con hazard, en
    /// centesimas de casilla: `cells` dice cuantas son, esto cuanto valen.
    std::array<int, static_cast<std::size_t>(MaxSnakes)> weighted{};
};

/// Reparto del tablero por distancia minima desde cada cabeza.
///
/// `blocked` son las casillas que siguen ocupadas tras el movimiento (cuerpos menos las
/// colas que se liberan); el llamante la construye igual que para el flood fill, de modo
/// que las dos heuristicas vean el mismo tablero.
///
/// `hazard_value_pct` es lo que vale una casilla con hazard frente a una limpia, en
/// porcentaje. 100 = valen igual.
///
/// `from_id`/`from_cell` mueven la cabeza de UNA serpiente antes de repartir, sin tocar el
/// estado. Es como el cerebro pregunta "si voy ahi, cuanto territorio me queda": copiar el
/// estado entero y aplicar el turno costaria mas y exigiria inventar los movimientos de
/// los demas, que es justo lo que v1 no hace todavia.
template <int W, int H, int MaxSnakes>
[[nodiscard]] Territory<MaxSnakes> voronoi(const engine::GameState<W, H, MaxSnakes>& state,
                                           const engine::Bitboard<W, H>& blocked,
                                           int hazard_value_pct = 50,
                                           int from_id = -1,
                                           int from_cell = -1) noexcept {
    using Board = engine::Bitboard<W, H>;
    constexpr int k_cells = W * H;

    Territory<MaxSnakes> out;

    // Distancia y dueño por casilla. dueño -1 = sin asignar, -2 = disputada sin dueño.
    std::array<std::int16_t, static_cast<std::size_t>(k_cells)> dist{};
    // `owner` es int16 y no int8 a proposito: int8_t es `signed char`, y leer un
    // char con signo hacia un int es justo el patron que clang-tidy prohibe
    // (bugprone-signed-char-misuse) porque en otras plataformas char no tiene signo
    // y el -2 se convertiria en 254. Son 121 bytes mas de pila: irrelevante.
    std::array<std::int16_t, static_cast<std::size_t>(k_cells)> owner{};
    for (int i = 0; i < k_cells; ++i) {
        dist[static_cast<std::size_t>(i)] = -1;
        owner[static_cast<std::size_t>(i)] = -1;
    }

    // Frente de cada serpiente como bitboard: expandirlo es una dilatacion, o sea cuatro
    // desplazamientos de palabra, sin cola y sin asignaciones dinamicas.
    std::array<Board, static_cast<std::size_t>(MaxSnakes)> front{};
    int alive = 0;
    for (int s = 0; s < static_cast<int>(state.snake_count); ++s) {
        const auto& snake = state.snakes[static_cast<std::size_t>(s)];
        if (!engine::is_alive(snake.status)) {
            continue;
        }
        ++alive;
        const int source = (s == from_id && from_cell >= 0) ? from_cell : snake.head();
        front[static_cast<std::size_t>(s)].set(source);
        dist[static_cast<std::size_t>(source)] = 0;
        owner[static_cast<std::size_t>(source)] = static_cast<std::int16_t>(s);
    }
    if (alive == 0) {
        return out;
    }

    for (int turn = 1; turn <= k_cells; ++turn) {
        bool any = false;
        std::array<Board, static_cast<std::size_t>(MaxSnakes)> next{};
        for (int s = 0; s < static_cast<int>(state.snake_count); ++s) {
            const auto& snake = state.snakes[static_cast<std::size_t>(s)];
            if (!engine::is_alive(snake.status)) {
                continue;
            }
            next[static_cast<std::size_t>(s)] = front[static_cast<std::size_t>(s)]
                                                    .expand()
                                                    .without(front[static_cast<std::size_t>(s)])
                                                    .without(blocked);
        }

        // Los empates se resuelven ANTES de asignar. Hacerlo serpiente a serpiente le
        // daria la casilla a la primera del bucle, que es el desempate por indice que el
        // proyecto prohibe en todas partes. ver docs/rules.md#r-12
        for (int cell = 0; cell < k_cells; ++cell) {
            if (dist[static_cast<std::size_t>(cell)] >= 0) {
                continue;
            }
            int ganador = -1;
            int mejor_longitud = -1;
            bool empate = false;
            for (int s = 0; s < static_cast<int>(state.snake_count); ++s) {
                if (!next[static_cast<std::size_t>(s)].test(cell)) {
                    continue;
                }
                const int len = static_cast<int>(state.snakes[static_cast<std::size_t>(s)].length);
                if (len > mejor_longitud) {
                    ganador = s;
                    mejor_longitud = len;
                    empate = false;
                } else if (len == mejor_longitud) {
                    empate = true;
                }
            }
            if (ganador < 0) {
                continue;
            }
            any = true;
            dist[static_cast<std::size_t>(cell)] = static_cast<std::int16_t>(turn);
            owner[static_cast<std::size_t>(cell)] =
                empate ? std::int16_t{-2} : static_cast<std::int16_t>(ganador);
        }

        // El frente siguiente es lo conquistado en este turno. Una casilla disputada no
        // da paso a nadie: las dos serpientes moririan ahi.
        for (int s = 0; s < static_cast<int>(state.snake_count); ++s) {
            Board conquistado;
            for (int cell = 0; cell < k_cells; ++cell) {
                if (dist[static_cast<std::size_t>(cell)] == turn &&
                    owner[static_cast<std::size_t>(cell)] == static_cast<std::int16_t>(s)) {
                    conquistado.set(cell);
                }
            }
            front[static_cast<std::size_t>(s)] = conquistado;
        }
        if (!any) {
            break;
        }
    }

    for (int cell = 0; cell < k_cells; ++cell) {
        const int o = owner[static_cast<std::size_t>(cell)];
        if (o == -2) {
            ++out.contested;
            continue;
        }
        if (o < 0) {
            continue;
        }
        ++out.cells[static_cast<std::size_t>(o)];
        out.weighted[static_cast<std::size_t>(o)] +=
            state.hazards.test(cell) ? hazard_value_pct : 100;
    }
    return out;
}

} // namespace snake::eval
