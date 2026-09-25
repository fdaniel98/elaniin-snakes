/// @file test_timed.cpp
/// Espacio con reloj, sobre las posiciones reales que lo motivaron.
/// ver docs/experimentos-duelo.md#s-liga-0925
#include <filesystem>
#include <fstream>
#include <string>

#include <engine/rules.hpp>

#include <snake/brain.hpp>
#include <snake/config_loader.hpp>
#include <snake/eval/timed.hpp>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

namespace {

engine::State11 carga(const std::string& nombre) {
    const std::filesystem::path p =
        std::filesystem::path(BSR_POSICIONES_LIGA_DIR) / (nombre + ".json");
    std::ifstream in(p);
    const auto doc = nlohmann::json::parse(in);
    engine::State11 s;
    REQUIRE(snake::parse_state(doc, s));
    return s;
}

} // namespace

TEST_CASE("reloj: el bolsillo cerrado por el cuello no cabe, el pasillo abierto si",
          "[timed][liga]") {
    // 6bed3999 t225: `up` entra en la fila de arriba entre la pared y nuestro cuerpo.
    const auto s = carga("6bed3999-t225");
    const int largo = static_cast<int>(s.snake(s.you).length);
    CHECK(snake::eval::timed_space(s, s.you, engine::Direction::up) < largo);
    CHECK(snake::eval::timed_space(s, s.you, engine::Direction::down) >= largo);
}

TEST_CASE("reloj: fuera del tablero o contra un cuerpo que no se retira es -1", "[timed]") {
    const auto s = carga("6bed3999-t225");
    // La cabeza esta en x=10: `right` se sale; `left` es nuestro cuello.
    CHECK(snake::eval::timed_space(s, s.you, engine::Direction::right) == -1);
    CHECK(snake::eval::timed_space(s, s.you, engine::Direction::left) == -1);
}

TEST_CASE("reloj: acotado entre 1 y el tablero en las posiciones reales", "[timed][liga]") {
    // Desde la cabeza siempre se cuenta al menos la casilla propia, y nunca mas que el
    // tablero aunque los cuerpos se retiren todos.
    for (const auto& e : std::filesystem::directory_iterator(BSR_POSICIONES_LIGA_DIR)) {
        std::ifstream in(e.path());
        const auto doc = nlohmann::json::parse(in);
        engine::State11 s;
        REQUIRE(snake::parse_state(doc, s));
        const int aqui = snake::eval::timed_space_here(s, s.you);
        CHECK(aqui >= 1);
        CHECK(aqui <= engine::State11::cells);
    }
}

TEST_CASE("v19 sale de las rendiciones del leaderboard", "[timed][liga][v19]") {
    // Con la desesperacion y el reloj encendidos, las dos posiciones donde la salida
    // estaba libre y v5 eligio el bolsillo. Sin reloj de pared: presupuesto por nodos.
    auto p = snake::load_params(std::string(BSR_CONFIG_DIR) + "/v19-reloj.json");
    p.search.budget_nodes = 20000;
    const snake::Deadline sin_reloj(snake::Deadline::Clock::now() + std::chrono::hours(1));
    CHECK(snake::decide(carga("6bed3999-t225"), sin_reloj, p).direction == engine::Direction::down);
    CHECK(snake::decide(carga("a84b9a76-t155"), sin_reloj, p).direction == engine::Direction::left);
}
