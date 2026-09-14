/// @file test_bitboard.cpp
/// El bitboard se instancia en 7x7, 11x11 y 19x19 porque el motor debe compilar y
/// pasar en los tres tamaños. ver docs/invariants.md#inv-05

#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>

#include <engine/bitboard.hpp>

using engine::Coord;

TEMPLATE_TEST_CASE("bitboard: indices y coordenadas son inversos", "[bitboard]",
                   engine::Board7, engine::Board11, engine::Board19) {
    using Board = TestType;
    for (int y = 0; y < Board::height; ++y) {
        for (int x = 0; x < Board::width; ++x) {
            const int index = Board::index_of(x, y);
            const Coord back = Board::coord_of(index);
            REQUIRE(static_cast<int>(back.x) == x);
            REQUIRE(static_cast<int>(back.y) == y);
        }
    }
}

TEMPLATE_TEST_CASE("bitboard: set/test/reset y conteo", "[bitboard]", engine::Board7,
                   engine::Board11, engine::Board19) {
    using Board = TestType;
    Board b;
    REQUIRE(b.none());
    REQUIRE(b.count() == 0);

    b.set(Coord{0, 0});
    b.set(Coord{static_cast<std::int8_t>(Board::width - 1),
                static_cast<std::int8_t>(Board::height - 1)});
    REQUIRE(b.count() == 2);
    REQUIRE(b.test(Coord{0, 0}));
    REQUIRE(b.any());

    b.reset(Coord{0, 0});
    REQUIRE_FALSE(b.test(Coord{0, 0}));
    REQUIRE(b.count() == 1);
}

TEMPLATE_TEST_CASE("bitboard: el complemento no cuenta bits fuera del tablero", "[bitboard]",
                   engine::Board7, engine::Board11, engine::Board19) {
    using Board = TestType;
    const Board empty;
    REQUIRE((~empty).count() == Board::cells);
    REQUIRE(Board::full().count() == Board::cells);
    REQUIRE((~Board::full()).count() == 0);
}

TEMPLATE_TEST_CASE("bitboard: los desplazamientos no envuelven por los bordes", "[bitboard]",
                   engine::Board7, engine::Board11, engine::Board19) {
    using Board = TestType;
    constexpr auto last_x = static_cast<std::int8_t>(Board::width - 1);
    constexpr auto last_y = static_cast<std::int8_t>(Board::height - 1);

    Board right_edge;
    right_edge.set(Coord{last_x, 0});
    REQUIRE(right_edge.east().count() == 0);
    REQUIRE(right_edge.west().test(Coord{static_cast<std::int8_t>(last_x - 1), 0}));

    Board left_edge;
    left_edge.set(Coord{0, 3});
    REQUIRE(left_edge.west().count() == 0);
    REQUIRE(left_edge.east().test(Coord{1, 3}));

    Board top_edge;
    top_edge.set(Coord{2, last_y});
    REQUIRE(top_edge.north().count() == 0);
    REQUIRE(top_edge.south().test(Coord{2, static_cast<std::int8_t>(last_y - 1)}));

    Board bottom_edge;
    bottom_edge.set(Coord{2, 0});
    REQUIRE(bottom_edge.south().count() == 0);
    REQUIRE(bottom_edge.north().test(Coord{2, 1}));
}

TEMPLATE_TEST_CASE("bitboard: expand da la casilla y sus vecinas ortogonales", "[bitboard]",
                   engine::Board7, engine::Board11, engine::Board19) {
    using Board = TestType;
    Board center;
    center.set(Coord{3, 3});
    const Board grown = center.expand();
    REQUIRE(grown.count() == 5);
    REQUIRE(grown.test(Coord{3, 3}));
    REQUIRE(grown.test(Coord{2, 3}));
    REQUIRE(grown.test(Coord{4, 3}));
    REQUIRE(grown.test(Coord{3, 2}));
    REQUIRE(grown.test(Coord{3, 4}));

    Board corner;
    corner.set(Coord{0, 0});
    REQUIRE(corner.expand().count() == 3);
}

TEMPLATE_TEST_CASE("bitboard: flood fill completo de un tablero vacio", "[bitboard]",
                   engine::Board7, engine::Board11, engine::Board19) {
    using Board = TestType;
    Board reachable;
    reachable.set(Coord{0, 0});
    const Board free_cells = Board::full();

    for (int i = 0; i < Board::cells; ++i) {
        const Board next = reachable.expand() & free_cells;
        if (next == reachable) break;
        reachable = next;
    }
    REQUIRE(reachable.count() == Board::cells);
}

TEMPLATE_TEST_CASE("bitboard: una pared parte el tablero en dos regiones", "[bitboard]",
                   engine::Board7, engine::Board11, engine::Board19) {
    using Board = TestType;
    constexpr int wall_x = Board::width / 2;
    const Board free_cells = Board::full().without(Board::column(wall_x));

    Board reachable;
    reachable.set(Coord{0, 0});
    for (int i = 0; i < Board::cells; ++i) {
        const Board next = reachable.expand() & free_cells;
        if (next == reachable) break;
        reachable = next;
    }
    REQUIRE(reachable.count() == wall_x * Board::height);
    REQUIRE_FALSE(reachable.test(
        Coord{static_cast<std::int8_t>(Board::width - 1), static_cast<std::int8_t>(0)}));
}

TEMPLATE_TEST_CASE("bitboard: rect recorta al tablero", "[bitboard]", engine::Board7,
                   engine::Board11, engine::Board19) {
    using Board = TestType;
    const Board full = Board::rect(-5, -5, Board::width + 5, Board::height + 5);
    REQUIRE(full.count() == Board::cells);

    const Board inner = Board::rect(1, 1, Board::width - 2, Board::height - 2);
    REQUIRE(inner.count() == (Board::width - 2) * (Board::height - 2));
    REQUIRE_FALSE(inner.test(Coord{0, 0}));
}

TEMPLATE_TEST_CASE("bitboard: pop_first recorre en orden creciente", "[bitboard]", engine::Board7,
                   engine::Board11, engine::Board19) {
    using Board = TestType;
    Board b;
    b.set(Coord{1, 0});
    b.set(Coord{0, 1});
    const int first = Board::index_of(1, 0);
    const int second = Board::index_of(0, 1);
    REQUIRE(b.pop_first() == first);
    REQUIRE(b.pop_first() == second);
    REQUIRE(b.pop_first() == -1);
}
