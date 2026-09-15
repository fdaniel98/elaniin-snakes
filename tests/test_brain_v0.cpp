/// @file test_brain_v0.cpp
/// Recorre TODOS los fixtures de tests/fixtures y exige a `decide`:
///   1. devolver siempre un movimiento no inmediatamente mortal si existe alguno;
///   2. respetar `_expect_any` / `_expect_not` del fixture;
///   3. no exceder nunca el deadline, ni con uno artificial de 5 ms.
/// ver docs/invariants.md#inv-10 y docs/invariants.md#inv-11

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <new>
#include <span>
#include <string>
#include <vector>

#include <engine/rng.hpp>
#include <engine/rules.hpp>

#include <snake/brain.hpp>
#include <snake/config_loader.hpp>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

using nlohmann::json;

namespace {

struct Fixture {
    std::string name;
    json doc;
};

std::vector<Fixture> load_fixtures() {
    std::vector<Fixture> fixtures;
    for (const auto& entry : std::filesystem::directory_iterator(BSR_FIXTURES_DIR)) {
        if (entry.path().extension() != ".json") {
            continue;
        }
        std::ifstream file(entry.path());
        json doc = json::parse(file, nullptr, false);
        if (doc.is_discarded()) {
            continue;
        }
        fixtures.push_back(Fixture{entry.path().filename().string(), std::move(doc)});
    }
    std::sort(fixtures.begin(), fixtures.end(), [](const Fixture& a, const Fixture& b) {
        return a.name < b.name;
    });
    return fixtures;
}

const char* name_of(engine::Direction d) {
    switch (d) {
        case engine::Direction::up:
            return "up";
        case engine::Direction::down:
            return "down";
        case engine::Direction::left:
            return "left";
        case engine::Direction::right:
            return "right";
    }
    return "up";
}

/// Impide que el optimizador elimine la llamada cuyo coste se esta midiendo.
template <typename T> void benchmarkable(T&& value) {
    asm volatile("" : : "r,m"(value) : "memory");
}

snake::Deadline generous() {
    return snake::Deadline(snake::Deadline::Clock::now() + std::chrono::milliseconds(350));
}

} // namespace

TEST_CASE("fixtures: hay al menos 10 y cubren los casos obligatorios", "[brain][fixtures]") {
    const auto fixtures = load_fixtures();
    REQUIRE(fixtures.size() >= 10);

    int stacked_early = 0;
    int wrapped = 0;
    int constrictor = 0;
    for (const auto& fixture : fixtures) {
        const int turn = fixture.doc.value("turn", -1);
        const std::string ruleset =
            fixture.doc["game"]["ruleset"].value("name", std::string("standard"));
        if (turn <= 2) {
            ++stacked_early;
        }
        if (ruleset == "wrapped") {
            ++wrapped;
        }
        if (ruleset == "constrictor") {
            ++constrictor;
        }

        INFO("fixture sin comentario: " << fixture.name);
        REQUIRE(fixture.doc.contains("_comment"));
    }
    REQUIRE(stacked_early >= 2);
    REQUIRE(wrapped >= 1);
    REQUIRE(constrictor >= 1);
}

TEST_CASE("brain_v0: todo fixture produce un movimiento legal", "[brain][fixtures]") {
    const snake::Params params;
    for (const auto& fixture : load_fixtures()) {
        INFO("fixture: " << fixture.name);
        engine::State11 state;
        REQUIRE(snake::parse_state(fixture.doc, state));

        const engine::MoveMask legal = engine::legal_moves(state, state.you);
        const snake::Move move = snake::decide(state, generous(), params);

        if (legal != engine::move_mask_none) {
            INFO("movimiento elegido: " << name_of(move.direction));
            REQUIRE(engine::mask_has(legal, move.direction));
            REQUIRE(move.fallback_level == 0);
        }
    }
}

TEST_CASE("brain_v0: se cumplen las expectativas de cada fixture", "[brain][fixtures]") {
    const snake::Params params;
    for (const auto& fixture : load_fixtures()) {
        INFO("fixture: " << fixture.name);
        engine::State11 state;
        REQUIRE(snake::parse_state(fixture.doc, state));

        const snake::Move move = snake::decide(state, generous(), params);
        const std::string chosen = name_of(move.direction);

        if (fixture.doc.contains("_expect_any")) {
            const auto allowed = fixture.doc["_expect_any"].get<std::vector<std::string>>();
            if (!allowed.empty()) {
                INFO("elegido " << chosen);
                REQUIRE(std::find(allowed.begin(), allowed.end(), chosen) != allowed.end());
            }
        }
        if (fixture.doc.contains("_expect_not")) {
            const auto forbidden = fixture.doc["_expect_not"].get<std::vector<std::string>>();
            INFO("elegido " << chosen);
            REQUIRE(std::find(forbidden.begin(), forbidden.end(), chosen) == forbidden.end());
        }
    }
}

TEST_CASE("brain_v0: un deadline de 5 ms no se excede en ningun fixture",
          "[brain][deadline][inv-11]") {
    const snake::Params params;
    for (const auto& fixture : load_fixtures()) {
        INFO("fixture: " << fixture.name);
        engine::State11 state;
        REQUIRE(snake::parse_state(fixture.doc, state));

        const auto started = snake::Deadline::Clock::now();
        const snake::Deadline tight(started + std::chrono::milliseconds(5));
        const snake::Move move = snake::decide(state, tight, params);
        const auto elapsed = snake::Deadline::Clock::now() - started;

        REQUIRE(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() <= 5);

        const engine::MoveMask legal = engine::legal_moves(state, state.you);
        if (legal != engine::move_mask_none) {
            REQUIRE(engine::mask_has(legal, move.direction));
        }
    }
}

TEST_CASE("fail-safe: los cuatro escalones", "[brain][failsafe]") {
    const snake::Params params;
    const auto& fixtures = load_fixtures();
    REQUIRE_FALSE(fixtures.empty());

    engine::State11 open_board;
    REQUIRE(snake::parse_state(fixtures.front().doc, open_board));

    SECTION("escalon 0: decision normal") {
        const snake::Move move = snake::decide(open_board, generous(), params);
        REQUIRE(move.fallback_level == 0);
    }

    SECTION("escalon 1: deadline vencido antes de puntuar") {
        const snake::Deadline past(snake::Deadline::Clock::now() - std::chrono::seconds(1));
        const snake::Move move = snake::decide(open_board, past, params);
        REQUIRE(move.fallback_level == 1);
        REQUIRE(engine::mask_has(engine::legal_moves(open_board, open_board.you), move.direction));
    }

    SECTION("escalon 2: sin movimiento seguro, pero dentro del tablero") {
        // Cabeza en (5,5) rodeada: abajo su propio cuerpo, y arriba, izquierda y derecha
        // las cabezas de tres rivales. Ninguna de esas casillas se libera.
        engine::State11 boxed;
        const json request = json::parse(R"({
          "turn": 40,
          "game": {"timeout": 500, "ruleset": {"name": "royale", "settings": {}}, "map": "royale"},
          "board": {"width": 11, "height": 11, "food": [], "hazards": [], "snakes": [
            {"id":"you","name":"you","health":90,"latency":"1","shout":"","squad":"","length":3,
             "head":{"x":5,"y":5},
             "body":[{"x":5,"y":5},{"x":5,"y":4},{"x":5,"y":3}],
             "customizations":{"color":"#000000","head":"default","tail":"default"}},
            {"id":"a","name":"a","health":90,"latency":"1","shout":"","squad":"","length":3,
             "head":{"x":5,"y":6},
             "body":[{"x":5,"y":6},{"x":4,"y":6},{"x":3,"y":6}],
             "customizations":{"color":"#000000","head":"default","tail":"default"}},
            {"id":"b","name":"b","health":90,"latency":"1","shout":"","squad":"","length":3,
             "head":{"x":4,"y":5},
             "body":[{"x":4,"y":5},{"x":4,"y":4},{"x":4,"y":3}],
             "customizations":{"color":"#000000","head":"default","tail":"default"}},
            {"id":"c","name":"c","health":90,"latency":"1","shout":"","squad":"","length":3,
             "head":{"x":6,"y":5},
             "body":[{"x":6,"y":5},{"x":6,"y":4},{"x":6,"y":3}],
             "customizations":{"color":"#000000","head":"default","tail":"default"}}
          ]},
          "you": {"id":"you"}
        })");
        REQUIRE(snake::parse_state(request, boxed));
        REQUIRE(engine::legal_moves(boxed, boxed.you) == engine::move_mask_none);

        const snake::Move move = snake::decide(boxed, generous(), params);
        REQUIRE(move.fallback_level == 2);
    }

    SECTION("escalon 3: estado imposible de jugar") {
        engine::State11 broken;
        broken.snake_count = 1;
        broken.you = 0;
        // Serpiente de longitud cero: el cerebro no puede razonar sobre ella y debe caer
        // al movimiento determinista sin lanzar.
        broken.snakes[0].length = 0;
        broken.snakes[0].status = engine::Elimination::alive;
        const snake::Move move = snake::decide(broken, generous(), params);
        REQUIRE(move.direction == engine::Direction::up);
        REQUIRE(move.fallback_level == 3);
    }
}

TEST_CASE("brain_v0: variantes no soportadas entran en modo degradado", "[brain][r-13]") {
    const snake::Params params;
    for (const auto& fixture : load_fixtures()) {
        engine::State11 state;
        if (!snake::parse_state(fixture.doc, state)) {
            continue;
        }
        if (engine::is_supported(state.rules.variant)) {
            continue;
        }

        INFO("fixture degradado: " << fixture.name);
        const snake::Move move = snake::decide(state, generous(), params);
        const engine::MoveMask legal = engine::legal_moves(state, state.you);
        REQUIRE(engine::mask_has(legal, move.direction));
    }
}

namespace {

/// Genera un estado aleatorio pero valido: cuerpos contiguos, sin solapes, salud y
/// comida arbitrarias. Es la entrada del fuzz de la clase `robustness` del loop.
engine::State11 random_state(engine::Rng& rng) {
    using Board = engine::State11::Board;
    engine::State11 state;
    state.rules.hazard_damage_per_turn = static_cast<std::int32_t>(rng.bounded(30));
    state.turn = static_cast<std::int32_t>(rng.bounded(300));
    state.snake_count = static_cast<std::uint8_t>(1 + rng.bounded(4));
    state.you = static_cast<engine::SnakeId>(rng.bounded(state.snake_count));

    engine::Board11 taken;
    for (int i = 0; i < static_cast<int>(state.snake_count); ++i) {
        auto& snake = state.snakes[static_cast<unsigned>(i)];
        const auto length = static_cast<int>(2 + rng.bounded(8));
        engine::Coord cursor{static_cast<std::int8_t>(rng.bounded(11)),
                             static_cast<std::int8_t>(rng.bounded(11))};
        snake.head_slot = 0;
        snake.length = 0;
        snake.health = static_cast<std::uint8_t>(1 + rng.bounded(100));
        snake.status = engine::Elimination::alive;

        for (int seg = 0; seg < length; ++seg) {
            if (!Board::in_bounds(cursor) || taken.test(Board::index_of(cursor))) {
                break;
            }
            taken.set(Board::index_of(cursor));
            snake.cells[static_cast<unsigned>(seg)] =
                static_cast<std::uint16_t>(Board::index_of(cursor));
            ++snake.length;
            cursor = engine::step(cursor, static_cast<engine::Direction>(rng.bounded(4)));
        }
        if (snake.length == 0) {
            snake.cells[0] = 0;
            snake.length = 1;
        }
    }

    for (int i = 0; i < static_cast<int>(rng.bounded(5)); ++i) {
        state.food.set(static_cast<int>(rng.bounded(121)));
    }
    if (rng.bounded(2) == 0) {
        const auto side = static_cast<int>(rng.bounded(4));
        for (int k = 0; k < 11; ++k) {
            state.hazards.set(side < 2 ? Board::index_of(side * 10, k)
                                       : Board::index_of(k, (side - 2) * 10));
        }
    }
    state.refresh_occupancy();
    return state;
}

} // namespace

TEST_CASE("fuzz: 10000 estados aleatorios sin movimiento ilegal ni deadline excedido",
          "[fuzz][brain][inv-10][inv-11]") {
    const snake::Params params;
    engine::Rng rng(20260915);

    int illegal = 0;
    int deadline_violations = 0;
    constexpr int states = 10000;

    for (int i = 0; i < states; ++i) {
        const engine::State11 state = random_state(rng);
        const auto started = snake::Deadline::Clock::now();
        const snake::Deadline deadline(started + std::chrono::milliseconds(5));
        const snake::Move move = snake::decide(state, deadline, params);
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 snake::Deadline::Clock::now() - started)
                                 .count();
        if (elapsed > 5) {
            ++deadline_violations;
        }

        const engine::MoveMask legal = engine::legal_moves(state, state.you);
        if (legal != engine::move_mask_none && !engine::mask_has(legal, move.direction)) {
            ++illegal;
        }
    }

    INFO("estados=" << states << " ilegales=" << illegal
                    << " violaciones_deadline=" << deadline_violations);
    REQUIRE(illegal == 0);
    REQUIRE(deadline_violations == 0);
}

// ---------------------------------------------------------------------------------
// Allocator instrumentado: cuenta asignaciones dinamicas dentro de una region marcada.
// Es la comprobacion mecanica de INV-03, que hasta ahora era "por construccion".
// ver docs/invariants.md#inv-03
// ---------------------------------------------------------------------------------

namespace {
bool counting_allocations = false;
std::size_t allocation_count = 0;

struct AllocationGuard {
    AllocationGuard() {
        allocation_count = 0;
        counting_allocations = true;
    }

    ~AllocationGuard() { counting_allocations = false; }

    AllocationGuard(const AllocationGuard&) = delete;
    AllocationGuard& operator=(const AllocationGuard&) = delete;
    AllocationGuard(AllocationGuard&&) = delete;
    AllocationGuard& operator=(AllocationGuard&&) = delete;
};
} // namespace

void* operator new(std::size_t size) {
    if (counting_allocations) {
        ++allocation_count;
    }
    void* memory = std::malloc(size);
    if (memory == nullptr) {
        throw std::bad_alloc();
    }
    return memory;
}

void operator delete(void* memory) noexcept {
    std::free(memory);
}

void operator delete(void* memory, std::size_t) noexcept {
    std::free(memory);
}

TEST_CASE("hot path: cero asignaciones dinamicas en apply, legal_moves y decide",
          "[perf][inv-03]") {
    const snake::Params params;
    engine::State11 base;
    {
        const auto fixtures = load_fixtures();
        REQUIRE_FALSE(fixtures.empty());
        REQUIRE(snake::parse_state(fixtures.front().doc, base));
    }

    const std::array<engine::Direction, 4> moves{engine::Direction::up,
                                                 engine::Direction::down,
                                                 engine::Direction::left,
                                                 engine::Direction::right};

    std::size_t allocations_apply = 0;
    {
        engine::State11 state = base;
        const AllocationGuard guard;
        engine::apply(state, std::span<const engine::Direction>(moves));
        allocations_apply = allocation_count;
    }

    std::size_t allocations_legal = 0;
    {
        const AllocationGuard guard;
        benchmarkable(engine::legal_moves(base, base.you));
        allocations_legal = allocation_count;
    }

    std::size_t allocations_decide = 0;
    {
        const snake::Deadline deadline(snake::Deadline::Clock::now() +
                                       std::chrono::milliseconds(350));
        const AllocationGuard guard;
        benchmarkable(snake::decide(base, deadline, params));
        allocations_decide = allocation_count;
    }

    INFO("apply=" << allocations_apply << " legal_moves=" << allocations_legal
                  << " decide=" << allocations_decide);
    REQUIRE(allocations_apply == 0);
    REQUIRE(allocations_legal == 0);
    REQUIRE(allocations_decide == 0);
}
