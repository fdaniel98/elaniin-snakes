#pragma once

/// @file state.hpp
/// Estado de partida trivialmente copiable: sin punteros a heap, sin `std::vector`,
/// sin `std::string`. El cuerpo de cada serpiente vive en un ring buffer de tamaño
/// fijo para que avanzar la cabeza sea O(1) y no mueva memoria.
///
/// La estrategia de busqueda es copy-make: el llamante copia el estado (memcpy) y
/// `apply()` muta la copia. ver docs/invariants.md#inv-04

#include <array>
#include <cstddef>
#include <cstdint>

#include <engine/bitboard.hpp>
#include <engine/ruleset.hpp>
#include <engine/types.hpp>

namespace engine {

/// Serpiente con cuerpo en ring buffer. El segmento logico 0 es la cabeza y el
/// `length - 1` la cola.
template <int Capacity> struct SnakeBody {
    /// Indices de casilla (`y * W + x`), no coordenadas: la conversion la hace el bitboard.
    std::array<std::uint16_t, static_cast<std::size_t>(Capacity)> cells{};
    /// Posicion de la cabeza dentro del ring.
    std::uint16_t head_slot{};
    std::uint16_t length{};
    std::uint8_t health{};
    Elimination status{Elimination::alive};
    /// Turno en el que fue eliminada, o -1 si sigue viva. Es lo unico de lo que
    /// puede derivarse el orden final. ver docs/rules.md#r-12
    std::int32_t eliminated_on_turn{-1};

    [[nodiscard]] constexpr int slot_of(int logical) const noexcept {
        const int slot = static_cast<int>(head_slot) + logical;
        return slot % Capacity;
    }

    /// Indice de casilla del segmento logico `i` (0 = cabeza).
    [[nodiscard]] constexpr int segment(int i) const noexcept {
        return static_cast<int>(cells[static_cast<unsigned>(slot_of(i))]);
    }

    [[nodiscard]] constexpr int head() const noexcept { return segment(0); }

    [[nodiscard]] constexpr int tail() const noexcept {
        return segment(static_cast<int>(length) - 1);
    }

    /// Cuello: segundo segmento. Con la serpiente recien nacida coincide con la cabeza.
    /// ver docs/rules.md#r-04
    [[nodiscard]] constexpr int neck() const noexcept { return length >= 2 ? segment(1) : head(); }

    /// La casilla de la cola NO se libera si los dos ultimos segmentos estan apilados.
    /// Se deriva del propio cuerpo, nunca de un flag "comio el turno anterior":
    /// en el spawn los tres segmentos estan apilados y en constrictor la cola nunca avanza.
    /// ver docs/rules.md#r-04
    [[nodiscard]] constexpr bool tail_is_stacked() const noexcept {
        return length >= 2 &&
               segment(static_cast<int>(length) - 1) == segment(static_cast<int>(length) - 2);
    }

    /// Antepone una cabeza nueva y descarta la cola (movimiento estandar).
    /// ver docs/rules.md#r-03
    constexpr void advance(int new_head_cell) noexcept {
        head_slot =
            static_cast<std::uint16_t>((static_cast<int>(head_slot) + Capacity - 1) % Capacity);
        cells[head_slot] = static_cast<std::uint16_t>(new_head_cell);
    }

    /// Duplica el ultimo segmento y sube la longitud, como `growSnake`.
    /// ver docs/rules.md#r-07
    constexpr void grow() noexcept {
        if (length == 0 || static_cast<int>(length) >= Capacity) {
            return;
        }
        const int last = segment(static_cast<int>(length) - 1);
        const int next_slot = slot_of(static_cast<int>(length));
        cells[static_cast<unsigned>(next_slot)] = static_cast<std::uint16_t>(last);
        ++length;
    }

    /// Coloca una serpiente nueva con todos sus segmentos apilados en una casilla,
    /// igual que `PlaceSnakesFixed`. ver docs/rules.md#r-11
    constexpr void spawn(int cell, int len, int hp) noexcept {
        head_slot = 0;
        length = static_cast<std::uint16_t>(len);
        health = static_cast<std::uint8_t>(hp);
        status = Elimination::alive;
        for (int i = 0; i < len; ++i) {
            cells[static_cast<unsigned>(i)] = static_cast<std::uint16_t>(cell);
        }
    }
};

/// Estado completo de una partida.
template <int W, int H, int MaxSnakes = 4> struct GameState {
    using Board = Bitboard<W, H>;
    static constexpr int width = W;
    static constexpr int height = H;
    static constexpr int cells = W * H;
    static constexpr int max_snakes = MaxSnakes;
    /// Una serpiente no puede ocupar mas casillas que el tablero.
    static constexpr int body_capacity = cells;

    using Snake = SnakeBody<body_capacity>;

    std::int32_t turn{};
    std::uint8_t snake_count{};
    /// Indice de nuestra serpiente dentro de `snakes`.
    SnakeId you{};

    std::array<Snake, static_cast<std::size_t>(MaxSnakes)> snakes{};

    Board food{};
    Board hazards{};
    /// Ocupacion por cuerpos vivos, incluidas las cabezas. Se recalcula con
    /// `refresh_occupancy()` y el invariante INV-02 lo comprueba en los tests.
    Board bodies{};

    Ruleset rules{};

    [[nodiscard]] constexpr const Snake& snake(SnakeId id) const noexcept {
        return snakes[static_cast<unsigned>(id)];
    }

    [[nodiscard]] constexpr Snake& snake(SnakeId id) noexcept {
        return snakes[static_cast<unsigned>(id)];
    }

    [[nodiscard]] constexpr int alive_count() const noexcept {
        int n = 0;
        for (int i = 0; i < static_cast<int>(snake_count); ++i) {
            if (is_alive(snakes[static_cast<unsigned>(i)].status)) {
                ++n;
            }
        }
        return n;
    }

    /// Recalcula `bodies` desde los cuerpos de las serpientes vivas.
    constexpr void refresh_occupancy() noexcept {
        bodies.clear();
        for (int i = 0; i < static_cast<int>(snake_count); ++i) {
            const Snake& s = snakes[static_cast<unsigned>(i)];
            if (!is_alive(s.status)) {
                continue;
            }
            for (int seg = 0; seg < static_cast<int>(s.length); ++seg) {
                bodies.set(s.segment(seg));
            }
        }
    }
};

using State11 = GameState<11, 11, 4>;
using State7 = GameState<7, 7, 4>;
using State19 = GameState<19, 19, 4>;

} // namespace engine
