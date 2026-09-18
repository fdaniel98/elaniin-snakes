/// @file test_voronoi.cpp
/// El reparto de territorio de v1. Lo que se comprueba no es "da un numero", sino las
/// tres propiedades de las que depende la heuristica: que ninguna casilla se cuente dos
/// veces, que el empate de distancia lo gane la mas larga y no el indice del bucle, y que
/// un pasillo que el rival sella antes NO cuente como nuestro, que es justo lo que el
/// flood fill de v0 contaba mal. ver docs/strategy.md#s-v1

#include <array>
#include <vector>

#include <engine/state.hpp>

#include <snake/eval/voronoi.hpp>

#include <catch2/catch_test_macros.hpp>

namespace {

using State = engine::State11;
using Board = State::Board;
using engine::Coord;

constexpr int cell(int x, int y) {
    return Board::index_of(x, y);
}

void put(State& s, engine::SnakeId id, std::vector<Coord> body, int length_extra = 0) {
    auto& snake = s.snake(id);
    snake.head_slot = 0;
    snake.length = static_cast<std::uint16_t>(body.size() + static_cast<std::size_t>(length_extra));
    snake.health = 100;
    snake.status = engine::Elimination::alive;
    snake.eliminated_on_turn = -1;
    for (std::size_t i = 0; i < body.size(); ++i) {
        snake.cells[i] = static_cast<std::uint16_t>(Board::index_of(body[i]));
    }
    // `length_extra` sirve para hacer una serpiente "mas larga" sin dibujarle mas cuerpo:
    // el desempate de Voronoi mira `length`, no el numero de segmentos pintados.
    for (int i = 0; i < length_extra; ++i) {
        snake.cells[body.size() + static_cast<std::size_t>(i)] =
            static_cast<std::uint16_t>(Board::index_of(body.back()));
    }
    if (static_cast<int>(id) + 1 > static_cast<int>(s.snake_count)) {
        s.snake_count = static_cast<std::uint8_t>(static_cast<int>(id) + 1);
    }
    s.refresh_occupancy();
}

} // namespace

TEST_CASE("el reparto no cuenta ninguna casilla dos veces", "[voronoi]") {
    State s{};
    put(s, 0, {{1, 5}});
    put(s, 1, {{9, 5}});
    const auto t = snake::eval::voronoi(s, s.bodies);

    const int repartidas = t.cells[0] + t.cells[1] + t.contested;
    // Las dos cabezas ocupan casilla y estan bloqueadas para el resto.
    REQUIRE(repartidas <= State::cells);
    REQUIRE(repartidas > 100);
    REQUIRE(t.cells[0] > 0);
    REQUIRE(t.cells[1] > 0);
}

TEST_CASE("dos cabezas simetricas y de igual longitud parten el tablero", "[voronoi]") {
    State s{};
    put(s, 0, {{0, 5}});
    put(s, 1, {{10, 5}});
    const auto t = snake::eval::voronoi(s, s.bodies);

    // Simetria exacta: ninguna de las dos puede tener mas que la otra.
    REQUIRE(t.cells[0] == t.cells[1]);
    // La columna central equidista: con longitudes iguales no es de nadie.
    REQUIRE(t.contested >= 11);
}

TEST_CASE("el empate de distancia lo gana la mas larga, no el indice", "[voronoi]") {
    State corta{};
    put(corta, 0, {{0, 5}});
    put(corta, 1, {{10, 5}}, 5); // la 1 es mas larga
    const auto a = snake::eval::voronoi(corta, corta.bodies);
    REQUIRE(a.cells[1] > a.cells[0]);
    REQUIRE(a.contested == 0);

    // El mismo tablero con las longitudes al reves tiene que dar el resultado espejo. Si
    // ganara el indice del bucle, la serpiente 0 ganaria en los dos casos.
    State larga{};
    put(larga, 0, {{0, 5}}, 5);
    put(larga, 1, {{10, 5}});
    const auto b = snake::eval::voronoi(larga, larga.bodies);
    REQUIRE(b.cells[0] == a.cells[1]);
    REQUIRE(b.cells[1] == a.cells[0]);
}

TEST_CASE("un pasillo que el rival sella antes no es territorio nuestro", "[voronoi]") {
    // Muro vertical en x=5 con una sola puerta en (5,0). La serpiente 1 esta pegada a la
    // puerta; la 0, lejos. El flood fill de v0 le daria a la 0 todo el tablero porque la
    // puerta esta abierta; Voronoi no, porque la 1 llega antes.
    State s{};
    put(s, 0, {{0, 10}});
    put(s, 1, {{6, 0}});
    for (int y = 1; y < 11; ++y) {
        s.bodies.set(cell(5, y));
    }
    const auto t = snake::eval::voronoi(s, s.bodies);

    const int derecha = 5 * 11; // columnas 6..10
    REQUIRE(t.cells[1] > derecha / 2);
    // Lo que importa: la izquierda no se lleva la derecha entera.
    REQUIRE(t.cells[0] < State::cells - derecha);
}

TEST_CASE("el territorio con hazard vale menos que el limpio", "[voronoi]") {
    State limpio{};
    put(limpio, 0, {{5, 5}});
    const auto a = snake::eval::voronoi(limpio, limpio.bodies);

    State con_hazard{};
    put(con_hazard, 0, {{5, 5}});
    for (int x = 0; x < 11; ++x) {
        for (int y = 0; y < 5; ++y) {
            con_hazard.hazards.set(cell(x, y));
        }
    }
    const auto b = snake::eval::voronoi(con_hazard, con_hazard.bodies, 50);

    REQUIRE(a.cells[0] == b.cells[0]);      // el mismo numero de casillas
    REQUIRE(b.weighted[0] < a.weighted[0]); // pero valen menos
}

TEST_CASE("una serpiente eliminada no reparte territorio", "[voronoi]") {
    State s{};
    put(s, 0, {{1, 5}});
    put(s, 1, {{9, 5}});
    s.snake(1).status = engine::Elimination::wall_collision;
    const auto t = snake::eval::voronoi(s, s.bodies);

    REQUIRE(t.cells[1] == 0);
    REQUIRE(t.contested == 0);
}
