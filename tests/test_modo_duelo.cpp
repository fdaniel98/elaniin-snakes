/// @file test_modo_duelo.cpp
/// El modo duelo es ADITIVO: fuera del 1v1 la snake tiene que ser exactamente v5.
/// ver docs/strategy.md#s-modo-duelo
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <engine/rules.hpp>

#include <snake/brain.hpp>
#include <snake/config_loader.hpp>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

namespace {

std::string config(const char* nombre) {
    return std::string(BSR_CONFIG_DIR) + "/" + nombre + ".json";
}

snake::Deadline sin_reloj() {
    return snake::Deadline(snake::Deadline::Clock::now() + std::chrono::hours(1));
}

/// Todas las posiciones reales y sinteticas del repositorio.
std::vector<engine::State11> posiciones() {
    std::vector<engine::State11> out;
    for (const char* dir : {BSR_FIXTURES_DIR,
                            BSR_FIXTURES_REALES_DIR,
                            BSR_POSICIONES_LIGA_DIR,
                            BSR_POSICIONES_CRITICAS_DIR}) {
        if (!std::filesystem::is_directory(dir)) {
            continue;
        }
        for (const auto& e : std::filesystem::directory_iterator(dir)) {
            if (e.path().extension() != ".json") {
                continue;
            }
            std::ifstream in(e.path());
            const auto doc = nlohmann::json::parse(in, nullptr, false);
            if (doc.is_discarded()) {
                continue;
            }
            // Los fixtures llevan la peticion dentro de "request" o son la peticion.
            const auto& req = doc.contains("request") ? doc["request"] : doc;
            engine::State11 s;
            if (snake::parse_state(req, s)) {
                out.push_back(s);
            }
        }
    }
    return out;
}

} // namespace

TEST_CASE("modo duelo: sin parche no hay modo duelo", "[duelo]") {
    const auto p = snake::load_params(config("default"));
    CHECK(p.duelo == nullptr);
}

TEST_CASE("modo duelo: el parche cambia solo lo que dice y hereda el resto", "[duelo]") {
    const auto base = snake::load_params(config("default"));
    const auto p = snake::load_params(config("v20-duelo"));
    REQUIRE(p.duelo != nullptr);
    CHECK(p.duelo->duelo == nullptr);
    CHECK(p.duelo->time.max_compute_ms == 250);
    CHECK(p.duelo->length.target_lead == 1);
    CHECK(p.duelo->space.timed_version == 1);
    // Fuera del parche, lo de siempre, arriba y abajo.
    CHECK(p.time.max_compute_ms == base.time.max_compute_ms);
    CHECK(p.length.target_lead == base.length.target_lead);
    CHECK(p.duelo->territory.weight == base.territory.weight);
    CHECK(p.duelo->search.max_depth == base.search.max_depth);
    CHECK(p.duelo->time.network_margin_ms == base.time.network_margin_ms);
}

TEST_CASE("modo duelo: fuera del 1v1 decide exactamente lo mismo que v5", "[duelo][aditivo]") {
    auto base = snake::load_params(config("default"));
    auto v20 = snake::load_params(config("v20-duelo"));
    base.search.budget_nodes = 3000;
    v20.search.budget_nodes = 3000;
    int comparadas = 0;
    int duelos = 0;
    for (const auto& s : posiciones()) {
        if (snake::es_duelo(s)) {
            ++duelos;
            CHECK(&snake::params_para(s, v20) == v20.duelo.get());
            continue;
        }
        CHECK(&snake::params_para(s, v20) == &v20);
        const auto a = snake::decide(s, sin_reloj(), base);
        const auto b = snake::decide(s, sin_reloj(), v20);
        CHECK(a.direction == b.direction);
        CHECK(a.nodes == b.nodes);
        ++comparadas;
    }
    // Que el test no pase por estar vacio: hay posiciones de tres y cuatro vivas.
    CHECK(comparadas >= 2);
    CHECK(duelos >= 10);
}

TEST_CASE("modo duelo: un parche vacio no cambia nada ni en el duelo", "[duelo][aditivo]") {
    auto base = snake::load_params(config("default"));
    std::ifstream in(config("default"));
    auto doc = nlohmann::json::parse(in);
    doc["duelo"] = nlohmann::json::object();
    auto vacio = snake::parse_params(doc);
    REQUIRE(vacio.duelo != nullptr);
    base.search.budget_nodes = 3000;
    vacio.search.budget_nodes = 3000;
    int n = 0;
    for (const auto& s : posiciones()) {
        if (!snake::es_duelo(s)) {
            continue;
        }
        CHECK(snake::decide(s, sin_reloj(), base).direction ==
              snake::decide(s, sin_reloj(), vacio).direction);
        ++n;
    }
    CHECK(n >= 10);
}

TEST_CASE("modo duelo: en la arena los nodos del duelo escalan con el computo", "[duelo]") {
    auto v20a = snake::load_params(config("v20a-duelo-tiempo"));
    v20a.search.budget_nodes = 1000;
    for (const auto& s : posiciones()) {
        if (!snake::es_duelo(s)) {
            continue;
        }
        const auto m = snake::decide(s, sin_reloj(), v20a);
        // 250 ms de 150: 5/3 de los nodos, redondeado.
        CHECK(m.nodes <= 1667 + 64);
    }
}

TEST_CASE("modo duelo: las claves desconocidas del parche se avisan con prefijo", "[duelo]") {
    const auto ruta = std::filesystem::temp_directory_path() / "bsr-duelo-claves.json";
    {
        std::ofstream out(ruta);
        out << R"({"space": {"weight": 1}, "duelo": {"space": {"no_existe": 1}, "nada": {}}})";
    }
    const auto fuera = snake::unknown_keys(ruta.string());
    CHECK(std::find(fuera.begin(), fuera.end(), "duelo.space.no_existe") != fuera.end());
    CHECK(std::find(fuera.begin(), fuera.end(), "duelo.nada") != fuera.end());
    CHECK(std::find(fuera.begin(), fuera.end(), "duelo") == fuera.end());
    std::filesystem::remove(ruta);
}
