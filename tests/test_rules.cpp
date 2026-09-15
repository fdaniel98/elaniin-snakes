/// @file test_rules.cpp
/// Cada caso cita el anchor de docs/rules.md que verifica. Los empates de 2, 3 y 4
/// serpientes son obligatorios: el motor oficial no asigna orden entre ellas.

#include <array>
#include <span>
#include <stdexcept>
#include <vector>

#include <engine/rules.hpp>
#include <engine/state.hpp>

#include <catch2/catch_test_macros.hpp>

using engine::Coord;
using engine::Direction;
using engine::Elimination;
using engine::Status;

namespace {

using State = engine::State11;
using Board = State::Board;

constexpr int cell(int x, int y) {
    return Board::index_of(x, y);
}

/// Coloca una serpiente con su cuerpo explicito: `body[0]` es la cabeza.
void put_snake(State& s, engine::SnakeId id, std::span<const Coord> body, int health) {
    auto& snake = s.snake(id);
    snake.head_slot = 0;
    snake.length = static_cast<std::uint16_t>(body.size());
    snake.health = static_cast<std::uint8_t>(health);
    snake.status = Elimination::alive;
    snake.eliminated_on_turn = -1;
    for (std::size_t i = 0; i < body.size(); ++i) {
        snake.cells[i] = static_cast<std::uint16_t>(Board::index_of(body[i]));
    }
    if (static_cast<int>(id) + 1 > static_cast<int>(s.snake_count)) {
        s.snake_count = static_cast<std::uint8_t>(static_cast<int>(id) + 1);
    }
    s.refresh_occupancy();
}

Status step_all(State& s, std::initializer_list<Direction> moves) {
    const std::vector<Direction> buffer(moves);
    return engine::apply(s, std::span<const Direction>(buffer));
}

} // namespace

TEST_CASE("apply: el movimiento antepone cabeza y descarta cola", "[rules][r-03]") {
    State s;
    const std::array<Coord, 3> body{Coord{5, 5}, Coord{5, 4}, Coord{5, 3}};
    put_snake(s, 0, body, 100);
    const std::array<Coord, 3> other{Coord{1, 1}, Coord{1, 0}, Coord{2, 0}};
    put_snake(s, 1, other, 100);

    REQUIRE(step_all(s, {Direction::up, Direction::up}) == Status::ok);
    REQUIRE(s.snake(0).head() == cell(5, 6));
    REQUIRE(s.snake(0).segment(1) == cell(5, 5));
    REQUIRE(s.snake(0).segment(2) == cell(5, 4));
    REQUIRE(s.snake(0).length == 3);
    REQUIRE(s.turn == 1);
}

TEST_CASE("apply: la salud baja 1 por turno y mata al llegar a 0", "[rules][r-05]") {
    State s;
    const std::array<Coord, 3> body{Coord{5, 5}, Coord{5, 4}, Coord{5, 3}};
    put_snake(s, 0, body, 1);
    const std::array<Coord, 3> other{Coord{1, 1}, Coord{1, 0}, Coord{2, 0}};
    put_snake(s, 1, other, 100);

    REQUIRE(step_all(s, {Direction::up, Direction::up}) == Status::game_over);
    REQUIRE(s.snake(0).status == Elimination::out_of_health);
    REQUIRE(s.snake(0).eliminated_on_turn == 1);
    REQUIRE(s.snake(1).health == 99);
}

TEST_CASE("apply: comer restaura la salud y duplica la cola", "[rules][r-07][r-04]") {
    State s;
    const std::array<Coord, 3> body{Coord{5, 5}, Coord{5, 4}, Coord{5, 3}};
    put_snake(s, 0, body, 50);
    const std::array<Coord, 3> other{Coord{1, 1}, Coord{1, 0}, Coord{2, 0}};
    put_snake(s, 1, other, 100);
    s.food.set(cell(5, 6));

    REQUIRE(step_all(s, {Direction::up, Direction::up}) == Status::ok);
    REQUIRE(s.snake(0).health == 100);
    REQUIRE(s.snake(0).length == 4);
    REQUIRE(s.snake(0).tail_is_stacked());
    REQUIRE_FALSE(s.food.test(cell(5, 6)));
}

TEST_CASE("apply: el hazard solo daña la cabeza y no si hay comida", "[rules][r-06]") {
    State s;
    s.rules.hazard_damage_per_turn = 14;

    SECTION("cabeza en hazard sin comida") {
        const std::array<Coord, 3> body{Coord{5, 5}, Coord{5, 4}, Coord{5, 3}};
        put_snake(s, 0, body, 100);
        const std::array<Coord, 3> other{Coord{1, 1}, Coord{1, 0}, Coord{2, 0}};
        put_snake(s, 1, other, 100);
        s.hazards.set(cell(5, 6));

        REQUIRE(step_all(s, {Direction::up, Direction::up}) == Status::ok);
        REQUIRE(s.snake(0).health == 100 - 1 - 14);
    }

    SECTION("comida en la misma casilla cancela el daño y restaura la salud") {
        const std::array<Coord, 3> body{Coord{5, 5}, Coord{5, 4}, Coord{5, 3}};
        put_snake(s, 0, body, 40);
        const std::array<Coord, 3> other{Coord{1, 1}, Coord{1, 0}, Coord{2, 0}};
        put_snake(s, 1, other, 100);
        s.hazards.set(cell(5, 6));
        s.food.set(cell(5, 6));

        REQUIRE(step_all(s, {Direction::up, Direction::up}) == Status::ok);
        REQUIRE(s.snake(0).health == 100);
    }

    SECTION("el cuerpo dentro del hazard no cuesta salud") {
        const std::array<Coord, 3> body{Coord{5, 5}, Coord{5, 4}, Coord{5, 3}};
        put_snake(s, 0, body, 100);
        const std::array<Coord, 3> other{Coord{1, 1}, Coord{1, 0}, Coord{2, 0}};
        put_snake(s, 1, other, 100);
        s.hazards.set(cell(5, 5));
        s.hazards.set(cell(5, 4));

        REQUIRE(step_all(s, {Direction::up, Direction::up}) == Status::ok);
        REQUIRE(s.snake(0).health == 99);
    }
}

TEST_CASE("apply: salir del tablero mata con wall_collision", "[rules][r-08]") {
    State s;
    const std::array<Coord, 3> body{Coord{0, 5}, Coord{1, 5}, Coord{2, 5}};
    put_snake(s, 0, body, 100);
    const std::array<Coord, 3> other{Coord{5, 1}, Coord{5, 0}, Coord{6, 0}};
    put_snake(s, 1, other, 100);

    REQUIRE(step_all(s, {Direction::left, Direction::up}) == Status::game_over);
    REQUIRE(s.snake(0).status == Elimination::wall_collision);
    REQUIRE(s.snake(1).status == Elimination::alive);
}

TEST_CASE("apply: chocar con el propio cuello es autocolision", "[rules][r-08]") {
    State s;
    const std::array<Coord, 3> body{Coord{5, 5}, Coord{5, 4}, Coord{5, 3}};
    put_snake(s, 0, body, 100);
    const std::array<Coord, 3> other{Coord{1, 1}, Coord{1, 0}, Coord{2, 0}};
    put_snake(s, 1, other, 100);

    REQUIRE(step_all(s, {Direction::down, Direction::up}) == Status::game_over);
    REQUIRE(s.snake(0).status == Elimination::self_collision);
}

TEST_CASE("apply: seguir una cola que avanza es legal; una apilada, mortal", "[rules][r-04]") {
    SECTION("cola que avanza: la casilla queda libre") {
        State s;
        const std::array<Coord, 3> mine{Coord{5, 4}, Coord{4, 4}, Coord{3, 4}};
        put_snake(s, 0, mine, 100);
        const std::array<Coord, 3> other{Coord{5, 5}, Coord{5, 6}, Coord{5, 7}};
        put_snake(s, 1, other, 100);

        REQUIRE_FALSE(engine::mask_has(engine::legal_moves(s, 0), Direction::up));
        // La cola de la serpiente 1 esta en (5,7); moverse ahi es legal porque avanza.
        const std::array<Coord, 3> chaser{Coord{5, 8}, Coord{6, 8}, Coord{7, 8}};
        put_snake(s, 2, chaser, 100);
        REQUIRE(engine::mask_has(engine::legal_moves(s, 2), Direction::down));
    }

    SECTION("cola apilada tras comer: la casilla sigue ocupada") {
        State s;
        const std::array<Coord, 4> other{Coord{5, 5}, Coord{5, 6}, Coord{5, 7}, Coord{5, 7}};
        put_snake(s, 0, other, 100);
        const std::array<Coord, 3> chaser{Coord{5, 8}, Coord{6, 8}, Coord{7, 8}};
        put_snake(s, 1, chaser, 100);
        REQUIRE(s.snake(0).tail_is_stacked());
        REQUIRE_FALSE(engine::mask_has(engine::legal_moves(s, 1), Direction::down));
    }

    SECTION("recien nacidas: los tres segmentos apilados, la cola no se libera") {
        State s;
        const std::array<Coord, 3> spawn{Coord{1, 1}, Coord{1, 1}, Coord{1, 1}};
        put_snake(s, 0, spawn, 100);
        const std::array<Coord, 3> chaser{Coord{1, 2}, Coord{2, 2}, Coord{3, 2}};
        put_snake(s, 1, chaser, 100);
        REQUIRE(s.snake(0).tail_is_stacked());
        REQUIRE_FALSE(engine::mask_has(engine::legal_moves(s, 1), Direction::down));
    }
}

TEST_CASE("apply: chocar contra el cuerpo rival mata con snake_collision", "[rules][r-08]") {
    State s;
    // 0 choca contra el cuerpo de 1; 2 intenta pasar por donde estaba el cuerpo de 1.
    const std::array<Coord, 3> a{Coord{3, 5}, Coord{2, 5}, Coord{1, 5}};
    put_snake(s, 0, a, 100);
    const std::array<Coord, 4> b{Coord{4, 6}, Coord{4, 5}, Coord{4, 4}, Coord{4, 3}};
    put_snake(s, 1, b, 100);

    REQUIRE(step_all(s, {Direction::right, Direction::up}) == Status::game_over);
    REQUIRE(s.snake(0).status == Elimination::snake_collision);
    REQUIRE(s.snake(1).status == Elimination::alive);
}

TEST_CASE("apply: una serpiente muerta de hambre deja de bloquear ese turno", "[rules][r-08]") {
    State s;
    const std::array<Coord, 3> starving{Coord{4, 5}, Coord{4, 4}, Coord{4, 3}};
    put_snake(s, 0, starving, 1);
    const std::array<Coord, 3> mover{Coord{3, 5}, Coord{2, 5}, Coord{1, 5}};
    put_snake(s, 1, mover, 100);

    REQUIRE(step_all(s, {Direction::up, Direction::right}) == Status::game_over);
    REQUIRE(s.snake(0).status == Elimination::out_of_health);
    REQUIRE(s.snake(1).status == Elimination::alive);
    REQUIRE(s.snake(1).head() == cell(4, 5));
}

TEST_CASE("apply: cabeza a cabeza, pierde la mas corta y empatan las iguales", "[rules][r-08]") {
    SECTION("longitudes distintas") {
        State s;
        const std::array<Coord, 4> big{Coord{4, 5}, Coord{3, 5}, Coord{2, 5}, Coord{1, 5}};
        put_snake(s, 0, big, 100);
        const std::array<Coord, 3> small{Coord{6, 5}, Coord{7, 5}, Coord{8, 5}};
        put_snake(s, 1, small, 100);

        REQUIRE(step_all(s, {Direction::right, Direction::left}) == Status::game_over);
        REQUIRE(s.snake(0).status == Elimination::alive);
        REQUIRE(s.snake(1).status == Elimination::head_collision);
    }

    SECTION("longitudes iguales: mueren las dos") {
        State s;
        const std::array<Coord, 3> a{Coord{4, 5}, Coord{3, 5}, Coord{2, 5}};
        put_snake(s, 0, a, 100);
        const std::array<Coord, 3> b{Coord{6, 5}, Coord{7, 5}, Coord{8, 5}};
        put_snake(s, 1, b, 100);

        REQUIRE(step_all(s, {Direction::right, Direction::left}) == Status::game_over);
        REQUIRE(s.snake(0).status == Elimination::head_collision);
        REQUIRE(s.snake(1).status == Elimination::head_collision);
        REQUIRE(s.alive_count() == 0);
    }
}

TEST_CASE("placements: empate de 2 serpientes comparte rango promediado",
          "[rules][r-12][empates]") {
    State s;
    const std::array<Coord, 3> a{Coord{4, 5}, Coord{3, 5}, Coord{2, 5}};
    put_snake(s, 0, a, 100);
    const std::array<Coord, 3> b{Coord{6, 5}, Coord{7, 5}, Coord{8, 5}};
    put_snake(s, 1, b, 100);

    REQUIRE(step_all(s, {Direction::right, Direction::left}) == Status::game_over);
    const auto p = engine::placements(s);
    REQUIRE(p.rank[0] == 1.5F);
    REQUIRE(p.rank[1] == 1.5F);
}

TEST_CASE("placements: empate de 3 serpientes en el mismo turno", "[rules][r-12][empates]") {
    State s;
    // Tres cabezas convergen en (5,5) con la misma longitud: mueren las tres.
    const std::array<Coord, 3> a{Coord{4, 5}, Coord{3, 5}, Coord{2, 5}};
    put_snake(s, 0, a, 100);
    const std::array<Coord, 3> b{Coord{6, 5}, Coord{7, 5}, Coord{8, 5}};
    put_snake(s, 1, b, 100);
    const std::array<Coord, 3> c{Coord{5, 4}, Coord{5, 3}, Coord{5, 2}};
    put_snake(s, 2, c, 100);
    const std::array<Coord, 3> survivor{Coord{0, 0}, Coord{0, 1}, Coord{0, 2}};
    put_snake(s, 3, survivor, 100);

    REQUIRE(step_all(s, {Direction::right, Direction::left, Direction::up, Direction::right}) ==
            Status::game_over);
    REQUIRE(s.snake(0).status == Elimination::head_collision);
    REQUIRE(s.snake(1).status == Elimination::head_collision);
    REQUIRE(s.snake(2).status == Elimination::head_collision);

    const auto p = engine::placements(s);
    REQUIRE(p.rank[3] == 1.0F);
    REQUIRE(p.rank[0] == 3.0F);
    REQUIRE(p.rank[1] == 3.0F);
    REQUIRE(p.rank[2] == 3.0F);
}

TEST_CASE("placements: empate de 4 serpientes da rango 2.5 a todas", "[rules][r-12][empates]") {
    State s;
    const std::array<Coord, 3> a{Coord{4, 5}, Coord{3, 5}, Coord{2, 5}};
    put_snake(s, 0, a, 100);
    const std::array<Coord, 3> b{Coord{6, 5}, Coord{7, 5}, Coord{8, 5}};
    put_snake(s, 1, b, 100);
    const std::array<Coord, 3> c{Coord{5, 4}, Coord{5, 3}, Coord{5, 2}};
    put_snake(s, 2, c, 100);
    const std::array<Coord, 3> d{Coord{5, 6}, Coord{5, 7}, Coord{5, 8}};
    put_snake(s, 3, d, 100);

    REQUIRE(step_all(s, {Direction::right, Direction::left, Direction::up, Direction::down}) ==
            Status::game_over);
    const auto p = engine::placements(s);
    for (int i = 0; i < 4; ++i) {
        REQUIRE(p.rank[static_cast<unsigned>(i)] == 2.5F);
    }
}

TEST_CASE("placements: quien muere antes queda por detras", "[rules][r-12]") {
    State s;
    const std::array<Coord, 3> a{Coord{0, 5}, Coord{1, 5}, Coord{2, 5}};
    put_snake(s, 0, a, 100);
    const std::array<Coord, 3> b{Coord{5, 5}, Coord{5, 4}, Coord{5, 3}};
    put_snake(s, 1, b, 100);
    const std::array<Coord, 3> c{Coord{9, 9}, Coord{9, 8}, Coord{9, 7}};
    put_snake(s, 2, c, 100);

    // Turno 1: la 0 se sale del tablero.
    REQUIRE(step_all(s, {Direction::left, Direction::up, Direction::up}) == Status::ok);
    // Turno 2: la 1 se sale tambien.
    s.snake(1).health = 1;
    REQUIRE(step_all(s, {Direction::up, Direction::up, Direction::left}) == Status::game_over);

    const auto p = engine::placements(s);
    REQUIRE(p.rank[2] == 1.0F);
    REQUIRE(p.rank[1] == 2.0F);
    REQUIRE(p.rank[0] == 3.0F);
}

TEST_CASE("legal_moves: no es una regla, apply acepta el movimiento mortal", "[rules][r-03]") {
    State s;
    const std::array<Coord, 3> body{Coord{0, 0}, Coord{1, 0}, Coord{2, 0}};
    put_snake(s, 0, body, 100);
    const std::array<Coord, 3> other{Coord{5, 5}, Coord{5, 4}, Coord{5, 3}};
    put_snake(s, 1, other, 100);

    const engine::MoveMask mask = engine::legal_moves(s, 0);
    REQUIRE_FALSE(engine::mask_has(mask, Direction::left));
    REQUIRE_FALSE(engine::mask_has(mask, Direction::down));
    REQUIRE_FALSE(engine::mask_has(mask, Direction::right));
    REQUIRE(engine::mask_has(mask, Direction::up));

    REQUIRE(step_all(s, {Direction::left, Direction::up}) == Status::game_over);
    REQUIRE(s.snake(0).status == Elimination::wall_collision);
}

TEST_CASE("default_move: repite el ultimo movimiento y cae a up sin cuello", "[rules][r-03]") {
    State s;
    const std::array<Coord, 3> body{Coord{5, 5}, Coord{4, 5}, Coord{3, 5}};
    put_snake(s, 0, body, 100);
    REQUIRE(engine::default_move(s, 0) == Direction::right);

    const std::array<Coord, 3> down{Coord{5, 5}, Coord{5, 6}, Coord{5, 7}};
    put_snake(s, 1, down, 100);
    REQUIRE(engine::default_move(s, 1) == Direction::down);

    const std::array<Coord, 3> stacked{Coord{2, 2}, Coord{2, 2}, Coord{2, 2}};
    put_snake(s, 2, stacked, 100);
    REQUIRE(engine::default_move(s, 2) == Direction::up);
}

TEST_CASE("is_terminal: se evalua antes del movimiento", "[rules][r-02][r-12]") {
    State s;
    const std::array<Coord, 3> body{Coord{5, 5}, Coord{5, 4}, Coord{5, 3}};
    put_snake(s, 0, body, 100);
    REQUIRE(s.alive_count() == 1);
    REQUIRE(engine::is_terminal(s));

    const std::int32_t turn_before = s.turn;
    REQUIRE(step_all(s, {Direction::up}) == Status::game_over);
    REQUIRE(s.turn == turn_before);
    REQUIRE(s.snake(0).head() == cell(5, 5));
}

TEST_CASE("refresh_occupancy: el bitboard de ocupacion casa con los cuerpos vivos",
          "[rules][inv-02]") {
    State s;
    const std::array<Coord, 3> a{Coord{5, 5}, Coord{5, 4}, Coord{5, 3}};
    put_snake(s, 0, a, 100);
    const std::array<Coord, 3> b{Coord{1, 1}, Coord{1, 0}, Coord{2, 0}};
    put_snake(s, 1, b, 100);

    REQUIRE(s.bodies.count() == 6);
    REQUIRE(s.bodies.test(cell(5, 5)));
    REQUIRE(s.bodies.test(cell(2, 0)));

    s.snake(1).status = Elimination::out_of_health;
    s.refresh_occupancy();
    REQUIRE(s.bodies.count() == 3);
}

TEST_CASE("royale_hazards sigue pendiente de la fase 1", "[.pending][rules][r-09]") {
    REQUIRE_THROWS_AS((engine::royale_hazards<11, 11>(1, 30, 25)), std::logic_error);
}
