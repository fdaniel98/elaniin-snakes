/// @file test_rules.cpp
/// Cada caso cita el anchor de docs/rules.md que verifica. Los empates de 2, 3 y 4
/// serpientes son obligatorios: el motor oficial no asigna orden entre ellas.

#include <algorithm>
#include <array>
#include <span>
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

    SECTION("con comida en la casilla, el hazard no puede matar") {
        // Con salud 10 y daño 14, si el hazard se aplicara la serpiente moriria ANTES
        // de comer. Como la comida cancela el daño, sobrevive y come. Es el unico
        // caso que distingue las dos semanticas: con salud alta, comer deja la salud
        // en 100 de todas formas y el test no prueba nada. Lo descubrio el mutante m2.
        // ver docs/rules.md#r-06
        const std::array<Coord, 3> body{Coord{5, 5}, Coord{5, 4}, Coord{5, 3}};
        put_snake(s, 0, body, 10);
        const std::array<Coord, 3> other{Coord{1, 1}, Coord{1, 0}, Coord{2, 0}};
        put_snake(s, 1, other, 100);
        s.hazards.set(cell(5, 6));
        s.food.set(cell(5, 6));

        REQUIRE(step_all(s, {Direction::up, Direction::up}) == Status::ok);
        REQUIRE(s.snake(0).status == Elimination::alive);
        REQUIRE(s.snake(0).health == 100);
        REQUIRE(s.snake(0).length == 4);
    }

    SECTION("el hazard mata cuando la salud no llega") {
        // Sin este caso, cambiar el acotado de la salud a [1,100] -es decir, que el
        // hazard nunca pueda matar- pasaba los tests. Lo descubrio la prueba de
        // mutantes de la clase robustness del loop. ver docs/rules.md#r-06
        const std::array<Coord, 3> body{Coord{5, 5}, Coord{5, 4}, Coord{5, 3}};
        put_snake(s, 0, body, 10);
        const std::array<Coord, 3> other{Coord{1, 1}, Coord{1, 0}, Coord{2, 0}};
        put_snake(s, 1, other, 100);
        s.hazards.set(cell(5, 6));

        REQUIRE(step_all(s, {Direction::up, Direction::up}) == Status::game_over);
        REQUIRE(s.snake(0).health == 0);
        REQUIRE(s.snake(0).status == Elimination::hazard);
        REQUIRE(s.snake(0).eliminated_on_turn == 1);
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

/// Rectangulo sin hazard de un bitboard, o `ok=false` si el complemento no lo es.
template <int W, int H> struct RectanguloLibre {
    bool ok{};
    int min_x{W};
    int max_x{-1};
    int min_y{H};
    int max_y{-1};

    [[nodiscard]] int bordes_movidos() const {
        return min_x + (W - 1 - max_x) + min_y + (H - 1 - max_y);
    }
};

template <int W, int H>
[[nodiscard]] RectanguloLibre<W, H> rectangulo_libre(const engine::Bitboard<W, H>& hazards) {
    RectanguloLibre<W, H> r;
    for (int x = 0; x < W; ++x) {
        for (int y = 0; y < H; ++y) {
            if (!hazards.test(engine::Bitboard<W, H>::index_of(x, y))) {
                r.min_x = std::min(r.min_x, x);
                r.max_x = std::max(r.max_x, x);
                r.min_y = std::min(r.min_y, y);
                r.max_y = std::max(r.max_y, y);
            }
        }
    }
    if (r.max_x < 0) {
        r.ok = true;
        return r;
    }
    for (int x = 0; x < W; ++x) {
        for (int y = 0; y < H; ++y) {
            const bool dentro = x >= r.min_x && x <= r.max_x && y >= r.min_y && y <= r.max_y;
            if (dentro == hazards.test(engine::Bitboard<W, H>::index_of(x, y))) {
                return r;
            }
        }
    }
    r.ok = true;
    return r;
}

TEST_CASE("royale_hazards: nada antes del primer shrink", "[rules][r-09]") {
    // `turn < shrinkEveryNTurns` no genera ningun hazard. ver docs/rules.md#r-09
    for (int turno = 0; turno < 25; ++turno) {
        REQUIRE(engine::royale_hazards<11, 11>(12345, turno, 25).count() == 0);
    }
    REQUIRE(engine::royale_hazards<11, 11>(12345, 25, 25).count() > 0);
}

TEST_CASE("royale_hazards: complemento de un rectangulo con un borde por shrink", "[rules][r-09]") {
    // Es la misma propiedad que el test diferencial comprueba sobre los hazards reales
    // del arbitro: si nuestro generador la cumple y los logs tambien, el modelo casa
    // aunque la secuencia de lados no sea la suya.
    // ver docs/decisions/ADR-0010-rng-del-shrink.md#d-0093
    for (std::uint64_t semilla = 1; semilla <= 50; ++semilla) {
        for (int cadencia : {1, 2, 5, 25}) {
            for (int turno = 0; turno <= 40; ++turno) {
                const auto hazards = engine::royale_hazards<11, 11>(semilla, turno, cadencia);
                const auto r = rectangulo_libre<11, 11>(hazards);
                INFO("semilla " << semilla << " cadencia " << cadencia << " turno " << turno);
                REQUIRE(r.ok);
                if (turno < cadencia) {
                    REQUIRE(hazards.count() == 0);
                    continue;
                }
                const int shrinks = turno / cadencia;
                if (r.max_x >= 0) {
                    REQUIRE(r.bordes_movidos() == shrinks);
                }
            }
        }
    }
}

TEST_CASE("royale_hazards: el rectangulo solo encoge", "[rules][r-09]") {
    // Monotono y anidado: lo que fue hazard lo sigue siendo. ver docs/rules.md#r-09
    for (std::uint64_t semilla = 1; semilla <= 20; ++semilla) {
        auto previos = engine::royale_hazards<11, 11>(semilla, 0, 3);
        for (int turno = 1; turno <= 60; ++turno) {
            const auto ahora = engine::royale_hazards<11, 11>(semilla, turno, 3);
            INFO("semilla " << semilla << " turno " << turno);
            // `previos sin ahora` vacio equivale a previos incluido en ahora.
            REQUIRE(previos.without(ahora).count() == 0);
            previos = ahora;
        }
    }
}

TEST_CASE("royale_hazards: determinista y sin estado global", "[rules][r-09][inv-08]") {
    const auto a = engine::royale_hazards<19, 19>(777, 31, 7);
    (void)engine::royale_hazards<19, 19>(999, 100, 3);
    const auto b = engine::royale_hazards<19, 19>(777, 31, 7);
    REQUIRE(a == b);

    // Semillas distintas dan schedules distintos: si no, el parametro sobra.
    int distintos = 0;
    for (std::uint64_t semilla = 1; semilla <= 20; ++semilla) {
        if (!(engine::royale_hazards<11, 11>(semilla, 20, 5) ==
              engine::royale_hazards<11, 11>(semilla + 100, 20, 5))) {
            ++distintos;
        }
    }
    REQUIRE(distintos >= 15);
}

TEST_CASE("royale_hazards: vectores de referencia del schedule propio", "[rules][r-09]") {
    // La secuencia de lados es nuestra y no es verificable contra el arbitro
    // (ver docs/decisions/ADR-0010-rng-del-shrink.md#d-0091), asi que sin esto nada
    // detecta que cambie. Son vectores de referencia como los del RNG: fijan el
    // comportamiento propio para que una modificacion accidental se vea.
    struct Caso {
        std::uint64_t semilla;
        int turno;
        int cadencia;
        int hazards;
        int min_x;
        int max_x;
        int min_y;
        int max_y;
    };

    const std::array<Caso, 3> casos{{
        {20260917, 12, 3, 41, 1, 10, 2, 9},
        {7, 25, 25, 11, 0, 9, 0, 10},
        {99, 40, 5, 72, 3, 9, 4, 10},
    }};

    for (const auto& caso : casos) {
        const auto hazards =
            engine::royale_hazards<11, 11>(caso.semilla, caso.turno, caso.cadencia);
        const auto r = rectangulo_libre<11, 11>(hazards);
        INFO("semilla " << caso.semilla << " turno " << caso.turno);
        REQUIRE(r.ok);
        REQUIRE(hazards.count() == caso.hazards);
        REQUIRE(r.min_x == caso.min_x);
        REQUIRE(r.max_x == caso.max_x);
        REQUIRE(r.min_y == caso.min_y);
        REQUIRE(r.max_y == caso.max_y);
    }
}

TEST_CASE("royale_hazards: al llegar justo a la cadencia ya hay hazard", "[rules][r-09]") {
    // El corte es `turn < shrinkEveryNTurns`, no `<=`: en el turno de la cadencia ya
    // hay un shrink aplicado (`maps/royale.go:54-56,64`). ver docs/rules.md#r-09
    for (int cadencia : {2, 3, 5, 25}) {
        for (std::uint64_t semilla = 1; semilla <= 10; ++semilla) {
            INFO("cadencia " << cadencia << " semilla " << semilla);
            REQUIRE(engine::royale_hazards<11, 11>(semilla, cadencia - 1, cadencia).count() == 0);
            REQUIRE(engine::royale_hazards<11, 11>(semilla, cadencia, cadencia).count() > 0);
        }
    }
}

TEST_CASE("royale_hazards: cadencia invalida devuelve el tablero limpio", "[rules][r-09]") {
    // El motor oficial devuelve error y aborta la partida (`maps/royale.go:50-52`);
    // aqui no hay a quien devolverlo, asi que se responde sin hazards y quien llama
    // valida el parametro. ver docs/decisions/ADR-0010-rng-del-shrink.md#d-0091
    REQUIRE(engine::royale_hazards<11, 11>(1, 50, 0).count() == 0);
    REQUIRE(engine::royale_hazards<11, 11>(1, 50, -3).count() == 0);
}

TEST_CASE("royale_hazards: el tablero entero puede acabar en hazard", "[rules][r-09]") {
    // Con mas shrinks que lados el rectangulo se invierte y no queda nada seguro.
    const auto hazards = engine::royale_hazards<7, 7>(3, 400, 1);
    REQUIRE(hazards.count() == 7 * 7);
}
