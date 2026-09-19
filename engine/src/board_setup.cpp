/// @file board_setup.cpp
/// Colocacion inicial: el tablero del turno 0, antes de que nadie se mueva.
///
/// Vive aparte de `rules.cpp` y de `royale_map.cpp` porque en la fuente tambien esta
/// aparte: no es el pipeline del turno (`standard.go`) ni un hook de mapa
/// (`maps/*.go`), sino la construccion del tablero (`board.go`). En servidor no se usa
/// -el tablero llega en el request-; lo necesita la arena, que genera partidas nuevas.

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>

#include <engine/bitboard.hpp>
#include <engine/rng.hpp>
#include <engine/rules.hpp>
#include <engine/ruleset.hpp>
#include <engine/state.hpp>

namespace engine {

namespace {

/// Los cuatro vecinos en diagonal de una casilla, los que existan dentro del tablero.
template <int W, int H> int diagonales(int celda, std::array<int, 4>& salida) noexcept {
    const Coord c = Bitboard<W, H>::coord_of(celda);
    int n = 0;
    for (const int dx : {-1, 1}) {
        for (const int dy : {-1, 1}) {
            const Coord v{static_cast<std::int8_t>(c.x + dx), static_cast<std::int8_t>(c.y + dy)};
            if (Bitboard<W, H>::in_bounds(v)) {
                salida[static_cast<std::size_t>(n++)] = Bitboard<W, H>::index_of(v);
            }
        }
    }
    return n;
}

} // namespace

template <int W, int H, int MaxSnakes>
GameState<W, H, MaxSnakes>
start_board(int snake_count, const Ruleset& rules, std::uint64_t seed) noexcept {
    using Board = Bitboard<W, H>;
    GameState<W, H, MaxSnakes> s;
    s.rules = rules;
    s.turn = 0;

    const int cuantas = snake_count < 0 ? 0 : (snake_count > MaxSnakes ? MaxSnakes : snake_count);
    s.snake_count = static_cast<std::uint8_t>(cuantas);
    if (cuantas == 0) {
        return s;
    }

    // Los ocho puntos fijos: 4 esquinas y 4 cardinales a distancia 1 del borde, en el
    // orden de la fuente. ver docs/rules.md#r-11
    const int mn = 1;
    const int mdx = (W - 1) / 2;
    const int mdy = (H - 1) / 2;
    const int mxx = W - 2;
    const int mxy = H - 2;
    std::array<int, 8> puntos{
        Board::index_of(mn, mn),
        Board::index_of(mn, mdy),
        Board::index_of(mn, mxy),
        Board::index_of(mdx, mn),
        Board::index_of(mdx, mxy),
        Board::index_of(mxx, mn),
        Board::index_of(mxx, mdy),
        Board::index_of(mxx, mxy),
    };

    // La baraja es la del `Rng` del repo, no la del `math/rand` de Go: el reparto de
    // asientos es nuestro y la forma -ocho puntos fijos barajados- es la oficial.
    // ver docs/decisions/ADR-0010-rng-del-shrink.md#d-0091
    Rng rng(seed);
    rng.shuffle(puntos.data(), puntos.size());

    for (int i = 0; i < cuantas; ++i) {
        // Los tres segmentos nacen APILADOS en la misma casilla, lo que significa que la
        // cola tampoco se libera en los primeros turnos aunque nadie haya comido.
        // ver docs/rules.md#r-11 y ver docs/rules.md#r-04
        s.snakes[static_cast<std::size_t>(i)].spawn(
            puntos[static_cast<std::size_t>(i)], start_length, max_health);
    }
    s.refresh_occupancy();

    // Comida inicial: una en diagonal por cabeza, y una en el centro. La casilla tiene que
    // quedar mas lejos del centro que la cabeza en al menos un eje y no ser esquina del
    // tablero. Con 4 serpientes o menos la condicion de tablero pequeño no aplica.
    // ver docs/rules.md#r-11
    const int centro_x = (W - 1) / 2;
    const int centro_y = (H - 1) / 2;
    const std::array<int, 4> esquinas{
        Board::index_of(0, 0),
        Board::index_of(0, H - 1),
        Board::index_of(W - 1, 0),
        Board::index_of(W - 1, H - 1),
    };

    for (int i = 0; i < cuantas; ++i) {
        const int cabeza = s.snakes[static_cast<std::size_t>(i)].head();
        const Coord hc = Board::coord_of(cabeza);
        std::array<int, 4> vecinas{};
        const int n = diagonales<W, H>(cabeza, vecinas);

        std::array<int, 4> validas{};
        int v = 0;
        for (int k = 0; k < n; ++k) {
            const int celda = vecinas[static_cast<std::size_t>(k)];
            if (s.bodies.test(celda) || s.food.test(celda)) {
                continue;
            }
            const Coord cc = Board::coord_of(celda);
            const bool mas_lejos = std::abs(cc.x - centro_x) > std::abs(hc.x - centro_x) ||
                                   std::abs(cc.y - centro_y) > std::abs(hc.y - centro_y);
            if (!mas_lejos) {
                continue;
            }
            bool es_esquina = false;
            for (const int e : esquinas) {
                es_esquina = es_esquina || e == celda;
            }
            if (es_esquina) {
                continue;
            }
            validas[static_cast<std::size_t>(v++)] = celda;
        }
        if (v > 0) {
            // Cual de las validas toca es sorteo, y el sorteo es nuestro igual que la
            // baraja de asientos. Las candidatas si son las de la fuente.
            s.food.set(
                validas[static_cast<std::size_t>(rng.bounded(static_cast<std::uint64_t>(v)))]);
        }
    }

    const int centro = Board::index_of(centro_x, centro_y);
    if (!s.bodies.test(centro) && !s.food.test(centro)) {
        s.food.set(centro);
    }
    return s;
}

template GameState<7, 7, 4> start_board<7, 7, 4>(int, const Ruleset&, std::uint64_t) noexcept;
template GameState<11, 11, 4> start_board<11, 11, 4>(int, const Ruleset&, std::uint64_t) noexcept;
template GameState<19, 19, 4> start_board<19, 19, 4>(int, const Ruleset&, std::uint64_t) noexcept;

} // namespace engine
