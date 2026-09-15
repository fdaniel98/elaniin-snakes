#pragma once

/// @file types.hpp
/// Tipos basicos compartidos por todo el motor. Sin dependencias fuera de la STL
/// y sin estado global.
///
/// El sistema de coordenadas esta fijado por la fuente de verdad:
/// (0,0) es la esquina inferior izquierda y `up` suma a `y`.
/// ver docs/rules.md#r-01

#include <cstdint>

namespace engine {

/// Identificador compacto de serpiente: indice en el array de serpientes del estado.
using SnakeId = std::uint8_t;

/// Direccion de movimiento. El orden numerico es estable y lo usan las mascaras.
enum class Direction : std::uint8_t {
    up = 0,
    down = 1,
    left = 2,
    right = 3,
};

inline constexpr int direction_count = 4;

/// Mascara de direcciones: bit i puesto significa "la direccion i es candidata".
/// Se usa como valor de retorno de `legal_moves`, que es una AYUDA para el cerebro,
/// no una regla del motor: `apply()` acepta cualquier direccion.
/// ver docs/rules.md#r-03
using MoveMask = std::uint8_t;

inline constexpr MoveMask move_mask_none = 0;
inline constexpr MoveMask move_mask_all = 0b1111;

[[nodiscard]] constexpr MoveMask to_mask(Direction d) noexcept {
    return static_cast<MoveMask>(1U << static_cast<unsigned>(d));
}

[[nodiscard]] constexpr bool mask_has(MoveMask m, Direction d) noexcept {
    return (m & to_mask(d)) != 0;
}

/// Coordenada de tablero. `signed` a proposito: los candidatos de movimiento se
/// calculan antes de comprobar los limites y pueden valer -1.
struct Coord {
    std::int8_t x{};
    std::int8_t y{};

    [[nodiscard]] friend constexpr bool operator==(Coord a, Coord b) noexcept {
        return a.x == b.x && a.y == b.y;
    }
};

/// Desplazamiento de una direccion. ver docs/rules.md#r-01
[[nodiscard]] constexpr Coord step(Coord c, Direction d) noexcept {
    switch (d) {
        case Direction::up:
            return Coord{c.x, static_cast<std::int8_t>(c.y + 1)};
        case Direction::down:
            return Coord{c.x, static_cast<std::int8_t>(c.y - 1)};
        case Direction::left:
            return Coord{static_cast<std::int8_t>(c.x - 1), c.y};
        case Direction::right:
            return Coord{static_cast<std::int8_t>(c.x + 1), c.y};
    }
    return c;
}

/// Direccion que lleva de `from` a `to` cuando son adyacentes ortogonales.
/// Devuelve `up` si no lo son: es el fallback del motor oficial.
/// ver docs/rules.md#r-03
[[nodiscard]] constexpr Direction direction_between(Coord from, Coord to) noexcept {
    if (to.x == from.x + 1) {
        return Direction::right;
    }
    if (to.x == from.x - 1) {
        return Direction::left;
    }
    if (to.y == from.y + 1) {
        return Direction::up;
    }
    if (to.y == from.y - 1) {
        return Direction::down;
    }
    return Direction::up;
}

/// Causa de eliminacion, con los mismos casos que el motor oficial.
/// ver docs/rules.md#r-08
enum class Elimination : std::uint8_t {
    alive = 0,
    out_of_health = 1,
    wall_collision = 2,
    self_collision = 3,
    snake_collision = 4,
    head_collision = 5,
    hazard = 6,
};

[[nodiscard]] constexpr bool is_alive(Elimination e) noexcept {
    return e == Elimination::alive;
}

/// Resultado de `apply()`.
enum class Status : std::uint8_t {
    ok = 0,        ///< El turno se aplico y la partida sigue.
    game_over = 1, ///< Quedan una o ninguna serpientes vivas. ver docs/rules.md#r-12
    error = 2,     ///< Estado invalido (p. ej. serpiente de longitud cero).
};

} // namespace engine
