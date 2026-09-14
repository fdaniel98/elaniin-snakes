/// @file test_brain_v0.cpp
/// Recorre TODOS los fixtures de tests/fixtures y exige a `decide`:
///   1. devolver siempre un movimiento no inmediatamente mortal si existe alguno;
///   2. respetar `_expect_any` / `_expect_not` del fixture;
///   3. no exceder nunca el deadline, ni con uno artificial de 5 ms.
/// ver docs/invariants.md#inv-10 y docs/invariants.md#inv-11

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <engine/rules.hpp>

#include <snake/brain.hpp>
#include <snake/config_loader.hpp>

using nlohmann::json;

namespace {

struct Fixture {
    std::string name;
    json doc;
};

std::vector<Fixture> load_fixtures() {
    std::vector<Fixture> fixtures;
    for (const auto& entry : std::filesystem::directory_iterator(BSR_FIXTURES_DIR)) {
        if (entry.path().extension() != ".json") continue;
        std::ifstream file(entry.path());
        json doc = json::parse(file, nullptr, false);
        if (doc.is_discarded()) continue;
        fixtures.push_back(Fixture{entry.path().filename().string(), std::move(doc)});
    }
    std::sort(fixtures.begin(), fixtures.end(),
              [](const Fixture& a, const Fixture& b) { return a.name < b.name; });
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
        if (turn <= 2) ++stacked_early;
        if (ruleset == "wrapped") ++wrapped;
        if (ruleset == "constrictor") ++constrictor;

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
        REQUIRE(engine::mask_has(engine::legal_moves(open_board, open_board.you),
                                 move.direction));
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

TEST_CASE("brain_v0: variantes no soportadas entran en modo degradado",
          "[brain][r-13]") {
    const snake::Params params;
    for (const auto& fixture : load_fixtures()) {
        engine::State11 state;
        if (!snake::parse_state(fixture.doc, state)) continue;
        if (engine::is_supported(state.rules.variant)) continue;

        INFO("fixture degradado: " << fixture.name);
        const snake::Move move = snake::decide(state, generous(), params);
        const engine::MoveMask legal = engine::legal_moves(state, state.you);
        REQUIRE(engine::mask_has(legal, move.direction));
    }
}
