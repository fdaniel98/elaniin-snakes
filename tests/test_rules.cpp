/// @file test_rules.cpp
/// Cada caso cita el anchor de docs/rules.md que verifica. Los empates de 2, 3 y 4
/// serpientes son obligatorios: el motor oficial no asigna orden entre ellas.

#include <algorithm>
#include <array>
#include <span>
#include <vector>

#include <engine/rng.hpp>
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

TEST_CASE("fuzz: apply con direcciones arbitrarias mantiene los invariantes",
          "[fuzz][rules][inv-01][inv-02]") {
    // `apply()` acepta cualquier direccion, incluida la inmediatamente mortal
    // (ver docs/rules.md#r-03), asi que el fuzz manda direcciones al azar sin filtrar:
    // es exactamente lo que hace el arbitro cuando una snake responde cualquier cosa.
    // Lo que se exige no es que la partida tenga sentido, sino que el estado siga siendo
    // coherente: longitudes, ocupacion y segmentos dentro del tablero.
    engine::Rng rng(20260917);
    int estados = 0;
    int partidas_terminadas = 0;

    for (int partida = 0; partida < 3000; ++partida) {
        State s;
        s.snake_count = static_cast<std::uint8_t>(2 + rng.bounded(3));
        s.rules.hazard_damage_per_turn = static_cast<std::int32_t>(rng.bounded(30));

        for (int i = 0; i < static_cast<int>(s.snake_count); ++i) {
            const auto x = static_cast<std::int8_t>(1 + rng.bounded(9));
            const auto y = static_cast<std::int8_t>(1 + rng.bounded(9));
            s.snakes[static_cast<unsigned>(i)].spawn(
                cell(x, y), 3, static_cast<int>(1 + rng.bounded(100)));
        }
        for (int f = 0; f < 5; ++f) {
            s.food.set(static_cast<int>(rng.bounded(11 * 11)));
        }
        for (int h = 0; h < 12; ++h) {
            s.hazards.set(static_cast<int>(rng.bounded(11 * 11)));
        }
        s.refresh_occupancy();

        for (int paso = 0; paso < 60; ++paso) {
            std::array<Direction, 4> moves{};
            for (auto& move : moves) {
                move = static_cast<Direction>(rng.bounded(engine::direction_count));
            }
            const Status status =
                engine::apply(s, std::span<const Direction>(moves.data(), s.snake_count));
            ++estados;

            engine::Bitboard<11, 11> vistos;
            for (int i = 0; i < static_cast<int>(s.snake_count); ++i) {
                const auto& snake = s.snakes[static_cast<unsigned>(i)];
                if (!engine::is_alive(snake.status)) {
                    // Una serpiente eliminada lleva su turno; viva, no.
                    REQUIRE(snake.eliminated_on_turn >= 0);
                    continue;
                }
                REQUIRE(snake.eliminated_on_turn == -1);
                REQUIRE(snake.length >= 1);
                REQUIRE(snake.length <= State::body_capacity);
                REQUIRE(snake.health <= engine::max_health);
                for (int seg = 0; seg < static_cast<int>(snake.length); ++seg) {
                    const int celda = snake.segment(seg);
                    REQUIRE(celda >= 0);
                    REQUIRE(celda < State::cells);
                    vistos.set(celda);
                }
            }
            // INV-02: la ocupacion casa con los cuerpos vivos, sin sobrar ni faltar.
            REQUIRE(s.bodies == vistos);

            if (status == Status::game_over) {
                ++partidas_terminadas;
                break;
            }
            REQUIRE(status == Status::ok);
        }
    }

    REQUIRE(estados >= 10000);
    // Si ninguna partida termina, el fuzz no esta llegando a los finales.
    REQUIRE(partidas_terminadas >= 2000);
}

TEST_CASE("el motor no se sale del array aunque snake_count mienta", "[rules][inv-01]") {
    // `snake_count` es un uint8_t de un struct publico. El parser rechaza mas de
    // `max_snakes` (snake/src/config_loader.cpp), pero `apply` es una funcion de libreria y
    // acepta cualquier GameState: la arena, un test o un fuzzer pueden pasarle uno con el
    // contador mas alto. Antes de acotarlo dentro del motor, GCC 13 avisaba de una
    // escritura fuera del array en `order_by_length` -y tenia razon-.
    engine::State11 s{};
    s.you = 0;
    for (int i = 0; i < engine::State11::max_snakes; ++i) {
        auto& sn = s.snakes[static_cast<unsigned>(i)];
        sn.head_slot = 0;
        sn.length = static_cast<std::uint16_t>(3 + i);
        sn.health = 80;
        sn.status = engine::Elimination::alive;
        sn.eliminated_on_turn = -1;
        for (int seg = 0; seg < sn.length; ++seg) {
            sn.cells[static_cast<unsigned>(seg)] =
                static_cast<std::uint16_t>(engine::State11::Board::index_of(
                    {static_cast<std::int8_t>(i * 2), static_cast<std::int8_t>(seg)}));
        }
    }
    // La mentira: mas serpientes de las que caben.
    s.snake_count = static_cast<std::uint8_t>(engine::State11::max_snakes + 3);
    s.refresh_occupancy();

    const std::array<engine::Direction, engine::State11::max_snakes> moves{
        engine::Direction::up, engine::Direction::up, engine::Direction::up, engine::Direction::up};
    // Bajo ASan, salirse del array aborta. Lo que se exige aqui es que NO aborte.
    REQUIRE_NOTHROW(
        engine::apply(s, std::span<const engine::Direction>(moves.data(), moves.size())));
    REQUIRE(engine::placements(s).count <= engine::State11::max_snakes);
}

// ---------------------------------------------------------------------------------------
// spawn_food: el hook del mapa que `apply()` deja fuera a proposito. ver docs/rules.md#r-10
// ---------------------------------------------------------------------------------------

namespace {

/// Tablero con una serpiente de 3 en una esquina y el turno ya avanzado, que es como
/// llega el estado al hook: `apply()` incrementa el contador antes de devolver.
State tablero_para_comida(int turno, int comida_en_tablero) {
    State s;
    const std::array<Coord, 3> body{Coord{0, 0}, Coord{1, 0}, Coord{2, 0}};
    put_snake(s, 0, body, 100);
    const std::array<Coord, 3> otra{Coord{10, 10}, Coord{9, 10}, Coord{8, 10}};
    put_snake(s, 1, otra, 100);
    s.turn = turno;
    for (int i = 0; i < comida_en_tablero; ++i) {
        s.food.set(cell(5, i));
    }
    return s;
}

int turnos_con_comida(int chance, int turnos) {
    int veces = 0;
    for (int t = 1; t <= turnos; ++t) {
        State s = tablero_para_comida(t, 1);
        s.rules.minimum_food = 1;
        s.rules.food_spawn_chance = chance;
        if (engine::spawn_food(s, 987654321ULL).count() > 0) {
            ++veces;
        }
    }
    return veces;
}

} // namespace

TEST_CASE("spawn_food: la rama de minimo repone exactamente lo que falta", "[rules][r-10]") {
    // `maps/standard.go:80-82`: si hay menos comida que el minimo se colocan las que
    // faltan, sin mirar `foodSpawnChance` siquiera.
    for (int minimo : {1, 2, 5}) {
        for (int hay = 0; hay < minimo; ++hay) {
            State s = tablero_para_comida(7, hay);
            s.rules.minimum_food = minimo;
            s.rules.food_spawn_chance = 0; // aunque la probabilidad sea cero
            INFO("minimo " << minimo << " hay " << hay);
            REQUIRE(engine::spawn_food(s, 42).count() == minimo - hay);
        }
    }
}

TEST_CASE("spawn_food: la probabilidad real es (chance-1)/100, no chance/100", "[rules][r-10]") {
    // La comparacion del Go es `(100 - rand.Intn(100)) < foodSpawnChance`
    // (`maps/standard.go:83-85`). Con `Intn(100)` en [0,99] eso deja fuera un caso: con
    // chance 1 no se coloca NUNCA y con 100 se coloca 99 de cada 100. No es un redondeo
    // nuestro, es la regla; se reproduce tal cual.
    REQUIRE(turnos_con_comida(0, 2000) == 0);
    REQUIRE(turnos_con_comida(1, 2000) == 0);

    const int con_100 = turnos_con_comida(100, 2000);
    REQUIRE(con_100 > 1940);
    REQUIRE(con_100 < 2000);

    // Y el default de royale, 15, cae alrededor de 14 de cada 100.
    const int con_15 = turnos_con_comida(15, 4000);
    REQUIRE(con_15 > 4000 * 10 / 100);
    REQUIRE(con_15 < 4000 * 18 / 100);
}

TEST_CASE("spawn_food: nunca cae sobre cuerpo ni sobre comida ya puesta", "[rules][r-10]") {
    for (int t = 1; t <= 300; ++t) {
        State s = tablero_para_comida(t, 3);
        s.rules.minimum_food = 8; // fuerza la rama de minimo, 5 casillas nuevas
        const auto nueva = engine::spawn_food(s, 5150);
        REQUIRE(nueva.count() == 5);
        REQUIRE((nueva & s.bodies).none());
        REQUIRE((nueva & s.food).none());
    }
}

TEST_CASE("spawn_food: los hazards no excluyen casilla", "[rules][r-10]") {
    // `GetUnoccupiedPoints(b, false, false)` (`board.go:522`) no mira hazards: la comida
    // puede caer dentro de la zona, y por eso `food.seek_below_in_hazard` existe.
    State s = tablero_para_comida(9, 0);
    s.rules.minimum_food = 1;
    for (int c = 0; c < State::cells; ++c) {
        s.hazards.set(c);
    }
    REQUIRE(engine::spawn_food(s, 77).count() == 1);
}

TEST_CASE("spawn_food: el sorteo depende del turno, no de la historia", "[rules][r-10]") {
    // Es la propiedad que hace pareable un A/B: dos ramas que divergieron antes siguen
    // sacando el mismo sorteo en el mismo turno. Si alguien cambia la siembra por turno
    // por un `Rng` que se conserva entre llamadas, este caso lo caza.
    // ver docs/decisions/ADR-0029-schedule-por-turno.md#d-0291
    State a = tablero_para_comida(50, 1);
    a.rules.minimum_food = 1;
    a.rules.food_spawn_chance = 50;

    const auto esperado = engine::spawn_food(a, 31337);

    // Mil sorteos de otros turnos por delante no cambian el del turno 50.
    for (int t = 1; t <= 1000; ++t) {
        State otro = tablero_para_comida(t, 1);
        otro.rules.minimum_food = 1;
        otro.rules.food_spawn_chance = 50;
        (void)engine::spawn_food(otro, 31337);
    }
    State b = tablero_para_comida(50, 1);
    b.rules.minimum_food = 1;
    b.rules.food_spawn_chance = 50;
    REQUIRE((engine::spawn_food(b, 31337) ^ esperado).none());
}

TEST_CASE("spawn_food: turno 0 y tablero lleno no colocan nada", "[rules][r-10]") {
    // El turno 0 no pasa por el hook: la comida inicial la pone la colocacion
    // (ver docs/rules.md#r-11), y `apply()` todavia no ha incrementado nada.
    State cero = tablero_para_comida(0, 0);
    cero.rules.minimum_food = 3;
    REQUIRE(engine::spawn_food(cero, 1).count() == 0);

    // Sin casillas libres el Go tampoco coloca (`maps/standard.go:91-93`).
    State lleno = tablero_para_comida(4, 0);
    lleno.rules.minimum_food = 3;
    for (int c = 0; c < State::cells; ++c) {
        lleno.food.set(c);
    }
    REQUIRE(engine::spawn_food(lleno, 1).count() == 0);
}

// ---------------------------------------------------------------------------------------
// start_board: el tablero del turno 0. ver docs/rules.md#r-11
// ---------------------------------------------------------------------------------------

TEST_CASE("start_board: tres segmentos apilados, salud llena y puntos fijos", "[rules][r-11]") {
    const engine::Ruleset reglas{};
    for (std::uint64_t semilla = 1; semilla <= 200; ++semilla) {
        const State s = engine::start_board<11, 11, 4>(4, reglas, semilla);
        REQUIRE(s.count() == 4);
        REQUIRE(s.turn == 0);
        REQUIRE_FALSE(engine::is_terminal(s));

        std::vector<int> cabezas;
        for (int i = 0; i < s.count(); ++i) {
            const auto& snake = s.snake(static_cast<engine::SnakeId>(i));
            INFO("semilla " << semilla << " serpiente " << i);
            REQUIRE(snake.length == engine::start_length);
            REQUIRE(snake.health == engine::max_health);
            REQUIRE(snake.status == Elimination::alive);
            // Apilados: los tres segmentos en la misma casilla. Por eso la cola no se
            // libera en los primeros turnos. ver docs/rules.md#r-04
            REQUIRE(snake.segment(0) == snake.segment(1));
            REQUIRE(snake.segment(1) == snake.segment(2));
            REQUIRE(snake.tail_is_stacked());
            cabezas.push_back(snake.head());
        }

        // Cada cabeza en uno de los ocho puntos fijos, y ninguna repetida.
        const std::array<int, 8> puntos{cell(1, 1),
                                        cell(1, 5),
                                        cell(1, 9),
                                        cell(5, 1),
                                        cell(5, 9),
                                        cell(9, 1),
                                        cell(9, 5),
                                        cell(9, 9)};
        for (const int c : cabezas) {
            REQUIRE(std::find(puntos.begin(), puntos.end(), c) != puntos.end());
        }
        std::sort(cabezas.begin(), cabezas.end());
        REQUIRE(std::unique(cabezas.begin(), cabezas.end()) == cabezas.end());
    }
}

TEST_CASE("start_board: una comida por cabeza mas la del centro", "[rules][r-11]") {
    const engine::Ruleset reglas{};
    for (std::uint64_t semilla = 1; semilla <= 200; ++semilla) {
        const State s = engine::start_board<11, 11, 4>(4, reglas, semilla);
        INFO("semilla " << semilla);
        // En 11x11 los ocho puntos estan a distancia >= 4, asi que ninguna diagonal
        // choca con otra: 4 en diagonal + 1 en el centro.
        REQUIRE(s.food.count() == 5);
        REQUIRE(s.food.test(cell(5, 5)));
        REQUIRE((s.food & s.bodies).none());

        // Ninguna comida en una esquina del tablero. ver docs/rules.md#r-11
        REQUIRE_FALSE(s.food.test(cell(0, 0)));
        REQUIRE_FALSE(s.food.test(cell(0, 10)));
        REQUIRE_FALSE(s.food.test(cell(10, 0)));
        REQUIRE_FALSE(s.food.test(cell(10, 10)));

        // Cada comida que no sea la del centro esta en diagonal a alguna cabeza.
        for (int c = 0; c < State::cells; ++c) {
            if (!s.food.test(c) || c == cell(5, 5)) {
                continue;
            }
            const Coord fc = Board::coord_of(c);
            bool pegada = false;
            for (int i = 0; i < s.count(); ++i) {
                const Coord hc = Board::coord_of(s.snake(static_cast<engine::SnakeId>(i)).head());
                pegada = pegada || (std::abs(fc.x - hc.x) == 1 && std::abs(fc.y - hc.y) == 1);
            }
            REQUIRE(pegada);
        }
    }
}

TEST_CASE("start_board: determinista y sensible a la semilla", "[rules][r-11]") {
    const engine::Ruleset reglas{};
    const State a = engine::start_board<11, 11, 4>(4, reglas, 12345);
    const State b = engine::start_board<11, 11, 4>(4, reglas, 12345);
    for (int i = 0; i < 4; ++i) {
        REQUIRE(a.snake(static_cast<engine::SnakeId>(i)).head() ==
                b.snake(static_cast<engine::SnakeId>(i)).head());
    }
    REQUIRE((a.food ^ b.food).none());

    int distintas = 0;
    for (std::uint64_t semilla = 1; semilla <= 50; ++semilla) {
        const State s = engine::start_board<11, 11, 4>(4, reglas, semilla);
        if (s.snake(0).head() != a.snake(0).head()) {
            ++distintas;
        }
    }
    REQUIRE(distintas > 20);
}

TEST_CASE("start_board: tamaños y numeros de serpientes distintos", "[rules][r-11]") {
    const engine::Ruleset reglas{};
    for (int cuantas = 1; cuantas <= 4; ++cuantas) {
        const auto s = engine::start_board<7, 7, 4>(cuantas, reglas, 99);
        REQUIRE(s.count() == cuantas);
        REQUIRE(s.food.test(engine::Bitboard<7, 7>::index_of(3, 3)));
        // En 7x7 los puntos fijos estan a distancia 2 y sus diagonales pueden coincidir,
        // asi que la cota es un rango y no una igualdad.
        REQUIRE(s.food.count() >= 2);
        REQUIRE(s.food.count() <= cuantas + 1);
    }
    // `snake_count` por encima del maximo se acota en silencio: la arena, un fuzzer o un
    // test pueden pedirlo y el motor no puede escribir fuera del array.
    // ver docs/invariants.md#inv-01
    REQUIRE(engine::start_board<11, 11, 4>(9, reglas, 1).count() == 4);
    REQUIRE(engine::start_board<11, 11, 4>(-3, reglas, 1).count() == 0);
}

TEST_CASE("start_board: una partida entera se juega desde el turno 0", "[rules][r-11][r-10]") {
    // El bucle de la arena en miniatura: colocar, mover, reponer comida, regenerar
    // hazards. Si esto no termina o revienta, la fase 4 no tiene sobre que construirse.
    engine::Ruleset reglas{};
    reglas.map_is_royale = true;
    reglas.shrink_every_n_turns = 25;

    for (std::uint64_t semilla = 1; semilla <= 60; ++semilla) {
        State s = engine::start_board<11, 11, 4>(4, reglas, semilla);
        engine::Rng rng(semilla ^ 0xABCDEFULL);
        int turnos = 0;
        while (!engine::is_terminal(s) && turnos < 2000) {
            std::array<Direction, 4> moves{};
            for (int i = 0; i < s.count(); ++i) {
                const auto id = static_cast<engine::SnakeId>(i);
                const engine::MoveMask legales = engine::legal_moves(s, id);
                moves[static_cast<std::size_t>(i)] = engine::default_move(s, id);
                if (legales != engine::move_mask_none) {
                    // Playout uniforme sobre las legales, que es la politica declarada
                    // para los numeros de docs/performance.md.
                    std::array<Direction, 4> opciones{};
                    int n = 0;
                    for (const Direction d :
                         {Direction::up, Direction::down, Direction::left, Direction::right}) {
                        if (engine::mask_has(legales, d)) {
                            opciones[static_cast<std::size_t>(n++)] = d;
                        }
                    }
                    moves[static_cast<std::size_t>(i)] = opciones[static_cast<std::size_t>(
                        rng.bounded(static_cast<std::uint64_t>(n)))];
                }
            }
            engine::apply(
                s, std::span<const Direction>(moves.data(), static_cast<std::size_t>(s.count())));
            s.food |= engine::spawn_food(s, semilla);
            s.hazards =
                engine::royale_hazards<11, 11>(semilla, s.turn, reglas.shrink_every_n_turns);
            ++turnos;
        }
        INFO("semilla " << semilla << " turnos " << turnos);
        REQUIRE(turnos < 2000);
        REQUIRE(s.alive_count() <= 1);

        const auto puestos = engine::placements(s);
        REQUIRE(puestos.count == 4);
        float suma = 0.0F;
        for (int i = 0; i < puestos.count; ++i) {
            const float r = puestos.rank[static_cast<std::size_t>(i)];
            REQUIRE(r >= 1.0F);
            REQUIRE(r <= 4.0F);
            suma += r;
        }
        // Rango compartido promediado: la suma de los puestos es siempre 1+2+3+4.
        // ver docs/rules.md#r-12
        REQUIRE(suma == 10.0F);
    }
}
