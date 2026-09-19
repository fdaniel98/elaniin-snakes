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
#include <snake/search.hpp>

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

TEST_CASE("brain_v0: el arranque en frio esta acotado y se puede pagar antes de jugar",
          "[brain][deadline][inv-11][arranque]") {
    // La PRIMERA llamada a decide() en la maquina de referencia costo 9 ms sobre un
    // fixture que despues tarda menos de uno: paginas de codigo que hay que traer a
    // memoria, no algoritmo. En produccion lo pagaba el primer /move de la partida.
    // `warmup()` lo paga antes de escuchar y otra vez en /start.
    // ver docs/decisions/ADR-0021-arranque-en-frio.md
    const snake::Params params;
    const long long us = snake::warmup(params);
    INFO("warmup_us=" << us);
    REQUIRE(us >= 0);     // no lanzo
    REQUIRE(us < 350000); // cabe de sobra en el presupuesto de un movimiento
}

TEST_CASE("brain_v0: un deadline de 5 ms no se excede en ningun fixture",
          "[brain][deadline][inv-11]") {
    const snake::Params params;
    // Se calienta primero, igual que hace el servidor antes de escuchar: lo que este
    // test mide es el presupuesto del ALGORITMO, y una primera llamada en frio mide el
    // cargador del sistema. El coste en frio tiene su propio test, arriba.
    snake::warmup(params);
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

TEST_CASE("v1 y v2 tampoco se salen del deadline, que es donde de verdad costaba",
          "[brain][deadline][inv-11]") {
    // El bucle de evaluacion hacia un Voronoi y una busqueda de cuellos por candidato y
    // no miraba el reloj hasta despues de los cuatro. Con v0 eso costaba poco y no se
    // notaba; con v1/v2 encendidos es el bloque mas caro de decide(). Aqui se encienden
    // los dos y se exige el mismo presupuesto.
    snake::Params v2;
    v2.territory.version = 1;
    v2.space.worst_case_weight = 150.0;
    snake::warmup(v2);

    for (const auto& fixture : load_fixtures()) {
        INFO("fixture: " << fixture.name);
        engine::State11 state;
        REQUIRE(snake::parse_state(fixture.doc, state));

        const auto started = snake::Deadline::Clock::now();
        const snake::Deadline tight(started + std::chrono::milliseconds(5));
        const snake::Move move = snake::decide(state, tight, v2);
        const auto elapsed = snake::Deadline::Clock::now() - started;

        REQUIRE(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() <= 5);
        const engine::MoveMask legal = engine::legal_moves(state, state.you);
        if (legal != engine::move_mask_none) {
            REQUIRE(engine::mask_has(legal, move.direction));
        }
    }
}

TEST_CASE("con el deadline ya vencido v2 sigue devolviendo un movimiento legal",
          "[brain][deadline][inv-11]") {
    // El caso que el codigo nuevo tiene que cubrir: el reloj se acaba DENTRO del bucle
    // de heuristicas caras. Entonces no se puntua a medias -unos candidatos con
    // territorio y otros con cero, que descartaria al que llego tarde por tardon y no por
    // malo-, sino que se vuelve a la puntuacion de v0 para todos.
    snake::Params v2;
    v2.territory.version = 1;
    v2.space.worst_case_weight = 150.0;

    engine::State11 state;
    REQUIRE(snake::parse_state(load_fixtures().front().doc, state));

    const snake::Deadline vencido(snake::Deadline::Clock::now() - std::chrono::seconds(1));
    const snake::Move move = snake::decide(state, vencido, v2);
    REQUIRE(engine::mask_has(engine::legal_moves(state, state.you), move.direction));
    REQUIRE(move.fallback_level <= 1);
}

// ================================================================================
// [v2] Busqueda. Lo que se comprueba no es que juegue mejor -eso lo dice el A/B- sino
// que no rompe ninguna de las garantias de v0. ver snake/include/snake/search.hpp
// ================================================================================

namespace {
snake::Params con_busqueda(int rivales = 2, int profundidad = 8) {
    snake::Params p;
    p.search.version = 1;
    p.search.max_rivals = rivales;
    p.search.max_depth = profundidad;
    return p;
}
} // namespace

TEST_CASE("busqueda: nunca devuelve un movimiento ilegal en ningun fixture",
          "[brain][search][inv-10]") {
    const snake::Params p = con_busqueda();
    snake::warmup(p);
    for (const auto& fixture : load_fixtures()) {
        INFO("fixture: " << fixture.name);
        engine::State11 state;
        REQUIRE(snake::parse_state(fixture.doc, state));
        const snake::Move move = snake::decide(state, generous(), p);
        const engine::MoveMask legal = engine::legal_moves(state, state.you);
        if (legal != engine::move_mask_none) {
            REQUIRE(engine::mask_has(legal, move.direction));
        }
    }
}

TEST_CASE("busqueda: el deadline manda, a cualquier presupuesto", "[brain][search][inv-11]") {
    // El presupuesto se barre de lo absurdo a lo holgado. En los cortos no da tiempo ni a
    // una profundidad y tiene que caer a v0; en los largos busca de verdad. Ninguno puede
    // pasarse, que es lo unico que no se negocia.
    const snake::Params p = con_busqueda();
    snake::warmup(p);
    for (const int presupuesto_ms : {1, 2, 5, 20, 100}) {
        for (const auto& fixture : load_fixtures()) {
            INFO("fixture: " << fixture.name << " presupuesto=" << presupuesto_ms);
            engine::State11 state;
            REQUIRE(snake::parse_state(fixture.doc, state));

            const auto t0 = snake::Deadline::Clock::now();
            const snake::Deadline d(t0 + std::chrono::milliseconds(presupuesto_ms));
            const snake::Move move = snake::decide(state, d, p);
            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                snake::Deadline::Clock::now() - t0)
                                .count();
            REQUIRE(ms <= presupuesto_ms);
            const engine::MoveMask legal = engine::legal_moves(state, state.you);
            if (legal != engine::move_mask_none) {
                REQUIRE(engine::mask_has(legal, move.direction));
            }
        }
    }
}

TEST_CASE("busqueda: con un deadline ya vencido no busca, cae a v0", "[brain][search][inv-11]") {
    engine::State11 state;
    REQUIRE(snake::parse_state(load_fixtures().front().doc, state));
    const snake::Deadline vencido(snake::Deadline::Clock::now() - std::chrono::seconds(1));
    const snake::Move move = snake::decide(state, vencido, con_busqueda());
    REQUIRE(engine::mask_has(engine::legal_moves(state, state.you), move.direction));
    REQUIRE(move.fallback_level >= 1); // vino del fail-safe de v0, no de la busqueda
}

TEST_CASE("busqueda: en modo degradado no se busca", "[brain][search]") {
    // Simular una variante cuyas reglas el motor no reproduce da un arbol de posiciones
    // que no van a ocurrir, que es peor que no mirar. ver docs/rules-parametros.md#r-13
    for (const auto& fixture : load_fixtures()) {
        engine::State11 state;
        REQUIRE(snake::parse_state(fixture.doc, state));
        if (engine::is_supported(state.rules.variant)) {
            continue;
        }
        INFO("fixture no soportado: " << fixture.name);
        const snake::Move move = snake::decide(state, generous(), con_busqueda());
        const engine::MoveMask legal = engine::legal_moves(state, state.you);
        if (legal != engine::move_mask_none) {
            REQUIRE(engine::mask_has(legal, move.direction));
        }
    }
}

TEST_CASE("busqueda: mas profundidad nunca elige morir donde v0 sobrevive",
          "[brain][search][inv-10]") {
    // La prueba de que la busqueda no se suicida por ver demasiado: en cada fixture, si
    // v0 encuentra una direccion que no es inmediatamente mortal, la busqueda tambien
    // tiene que devolver una que no lo sea.
    for (const auto& fixture : load_fixtures()) {
        INFO("fixture: " << fixture.name);
        engine::State11 state;
        REQUIRE(snake::parse_state(fixture.doc, state));
        const engine::MoveMask legal = engine::legal_moves(state, state.you);
        if (legal == engine::move_mask_none) {
            continue;
        }
        for (const int rivales : {1, 2, 3}) {
            const snake::Move m = snake::decide(state, generous(), con_busqueda(rivales));
            INFO("rivales simulados: " << rivales);
            REQUIRE(engine::mask_has(legal, m.direction));
        }
    }
}

TEST_CASE("busqueda: profundidad mayor no cambia el veredicto sobre una muerte segura",
          "[search]") {
    // Fixture 14: arriba es mortal. Ninguna profundidad puede decidir lo contrario.
    engine::State11 state;
    bool encontrado = false;
    for (const auto& fixture : load_fixtures()) {
        if (fixture.name.rfind("14-", 0) != 0) {
            continue;
        }
        REQUIRE(snake::parse_state(fixture.doc, state));
        encontrado = true;
    }
    REQUIRE(encontrado);
    for (const int prof : {1, 2, 3, 4, 6, 8}) {
        INFO("profundidad " << prof);
        const snake::Move m = snake::decide(state, generous(), con_busqueda(2, prof));
        REQUIRE(m.direction != engine::Direction::up);
    }
}

TEST_CASE("time manager: el colchon contra el timeout del arbitro", "[deadline][presupuesto]") {
    // No habia ni un test de `from_timeout`, y con v0 daba igual: decidia en 100 us. Con
    // la busqueda el presupuesto es carga estructural -p50 de 348 ms en el torneo- y el
    // colchon es lo unico que separa un movimiento de un timeout.
    // ver docs/decisions/ADR-0023-presupuesto-de-computo.md
    const snake::Params p;

    SECTION("con el timeout de torneo queda al menos 250 ms de colchon") {
        const auto t0 = snake::Deadline::Clock::now();
        const auto d = snake::Deadline::from_timeout(500, p.time, t0);
        const auto presupuesto =
            std::chrono::duration_cast<std::chrono::milliseconds>(d.end() - t0).count();
        INFO("presupuesto de computo: " << presupuesto << " ms");
        REQUIRE(presupuesto <= 250);
        // Y que no se quede en nada: por debajo de 100 ms la busqueda pierde un nivel
        // entero (tabla del ADR-0023).
        REQUIRE(presupuesto >= 100);
    }

    SECTION("el techo manda sobre los margenes, no al reves") {
        // Con un timeout generoso el presupuesto lo fija `max_compute_ms`, no la resta.
        const auto t0 = snake::Deadline::Clock::now();
        const auto d = snake::Deadline::from_timeout(5000, p.time, t0);
        const auto presupuesto =
            std::chrono::duration_cast<std::chrono::milliseconds>(d.end() - t0).count();
        REQUIRE(presupuesto == p.time.max_compute_ms);
    }

    SECTION("un timeout absurdo no produce un deadline en el pasado") {
        const auto t0 = snake::Deadline::Clock::now();
        for (const int timeout : {0, 1, 50, 149, 150, 151}) {
            const auto d = snake::Deadline::from_timeout(timeout, p.time, t0);
            INFO("timeout=" << timeout);
            REQUIRE(d.end() > t0);
        }
    }

    SECTION("los margenes se restan de verdad") {
        const auto t0 = snake::Deadline::Clock::now();
        snake::Params sin_techo = p;
        sin_techo.time.max_compute_ms = 100000;
        const auto d = snake::Deadline::from_timeout(1000, sin_techo.time, t0);
        const auto presupuesto =
            std::chrono::duration_cast<std::chrono::milliseconds>(d.end() - t0).count();
        REQUIRE(presupuesto == 1000 - p.time.network_margin_ms - p.time.safety_margin_ms);
    }
}

TEST_CASE("busqueda: con el presupuesto de torneo no se pasa de 250 ms",
          "[brain][search][inv-11][presupuesto]") {
    // El presupuesto real, derivado del timeout real, sobre los fixtures reales. Es la
    // comprobacion de punta a punta de que lo que se despliega cabe donde tiene que caber.
    snake::Params p;
    p.search.version = 1;
    snake::warmup(p);
    for (const auto& fixture : load_fixtures()) {
        INFO("fixture: " << fixture.name);
        engine::State11 state;
        REQUIRE(snake::parse_state(fixture.doc, state));
        const auto t0 = snake::Deadline::Clock::now();
        const snake::Move move =
            snake::decide(state, snake::Deadline::from_timeout(500, p.time, t0), p);
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            snake::Deadline::Clock::now() - t0)
                            .count();
        REQUIRE(ms <= 250);
        const engine::MoveMask legal = engine::legal_moves(state, state.you);
        if (legal != engine::move_mask_none) {
            REQUIRE(engine::mask_has(legal, move.direction));
        }
    }
}

TEST_CASE("busqueda: el tope de profundidad no rompe el deadline ni la pila",
          "[brain][search][inv-11]") {
    // El tope paso de 8 a 64 porque 8 dejaba 158 de 200 ms sin usar en los finales de dos
    // (ver docs/decisions/ADR-0024-el-tope-de-profundidad.md). Lo que hay que fijar es que
    // subirlo no rompe nada: con presupuestos muy cortos se sigue cortando por reloj, y
    // con uno largo la recursion profunda no se lleva la pila por delante.
    snake::Params p;
    p.search.version = 1;
    p.search.max_depth = 64;
    snake::warmup(p);

    for (const int presupuesto_ms : {1, 5, 50, 200}) {
        for (const auto& fixture : load_fixtures()) {
            INFO("fixture: " << fixture.name << " presupuesto=" << presupuesto_ms);
            engine::State11 state;
            REQUIRE(snake::parse_state(fixture.doc, state));
            const auto t0 = snake::Deadline::Clock::now();
            const snake::Deadline d(t0 + std::chrono::milliseconds(presupuesto_ms));
            const snake::Move move = snake::decide(state, d, p);
            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                snake::Deadline::Clock::now() - t0)
                                .count();
            REQUIRE(ms <= presupuesto_ms);
            const engine::MoveMask legal = engine::legal_moves(state, state.you);
            if (legal != engine::move_mask_none) {
                REQUIRE(engine::mask_has(legal, move.direction));
            }
        }
    }
}

TEST_CASE("busqueda: un final de dos usa el presupuesto, no se planta en el tope",
          "[search][profundidad]") {
    // La regresion concreta que el tope de 8 producia: con dos serpientes la busqueda
    // llegaba a 8, se plantaba y devolvia el 79% del tiempo sin usar.
    engine::State11 s{};
    s.snake_count = 2;
    s.you = 0;
    for (int k = 0; k < 2; ++k) {
        auto& sn = s.snakes[static_cast<unsigned>(k)];
        sn.head_slot = 0;
        sn.length = 8;
        sn.health = 80;
        sn.status = engine::Elimination::alive;
        sn.eliminated_on_turn = -1;
        for (int seg = 0; seg < 8; ++seg) {
            sn.cells[static_cast<unsigned>(seg)] =
                static_cast<std::uint16_t>(engine::State11::Board::index_of(
                    {static_cast<std::int8_t>(k == 0 ? 2 : 8), static_cast<std::int8_t>(2 + seg)}));
        }
    }
    s.food.set(engine::State11::Board::index_of({5, 5}));
    s.refresh_occupancy();

    snake::Params p;
    p.search.version = 1;
    snake::warmup(p);
    // Deadline holgado A PROPOSITO. Lo que se fija aqui es que el TOPE ya no muerde, no
    // lo rapida que es la maquina: con 200 ms este test pasaba en release y fallaba en
    // debug, donde los sanitizers van veinte veces mas lentos. Un test que mide velocidad
    // disfrazado de test que mide comportamiento es un test que falla por sorpresa.
    const auto t0 = snake::Deadline::Clock::now();
    const snake::SearchResult r =
        snake::search(s, snake::Deadline(t0 + std::chrono::seconds(5)), p);
    INFO("profundidad alcanzada: " << r.depth);
    // Con el tope en 8 esto era exactamente 8, con cualquier deadline.
    REQUIRE(r.depth > 8);
    REQUIRE(r.depth <= p.search.max_depth);
}

TEST_CASE("busqueda con territorio en las hojas: mismas garantias",
          "[brain][search][hojas][inv-10][inv-11]") {
    // La evaluacion de las hojas pasa a usar Voronoi. Lo que se fija aqui no es que juegue
    // mejor -eso lo dice el A/B- sino que meter una evaluacion mas cara en el sitio mas
    // caliente no rompe la legalidad ni el deadline. ver docs/strategy.md#s-hojas
    snake::Params p;
    p.search.version = 1;
    p.territory.version = 1;
    snake::warmup(p);

    for (const int presupuesto_ms : {1, 5, 50, 200}) {
        for (const auto& fixture : load_fixtures()) {
            INFO("fixture: " << fixture.name << " presupuesto=" << presupuesto_ms);
            engine::State11 state;
            REQUIRE(snake::parse_state(fixture.doc, state));
            const auto t0 = snake::Deadline::Clock::now();
            const snake::Deadline d(t0 + std::chrono::milliseconds(presupuesto_ms));
            const snake::Move move = snake::decide(state, d, p);
            REQUIRE(std::chrono::duration_cast<std::chrono::milliseconds>(
                        snake::Deadline::Clock::now() - t0)
                        .count() <= presupuesto_ms);
            const engine::MoveMask legal = engine::legal_moves(state, state.you);
            if (legal != engine::move_mask_none) {
                REQUIRE(engine::mask_has(legal, move.direction));
            }
        }
    }
}

TEST_CASE("la guarda de 'no cabe ni mi cuerpo' no depende del territorio", "[search][hojas]") {
    // El espacio CRUDO se conserva siempre aunque el territorio sustituya al termino de
    // espacio en la puntuacion: "no cabe ni mi cuerpo" es una condicion sobre casillas
    // fisicas, no sobre quien llega antes. Si se derivara del territorio, una region
    // amplia pero disputada dejaria de contar como amplia.
    engine::State11 s{};
    s.snake_count = 1;
    s.you = 0;
    auto& yo = s.snakes[0];
    yo.head_slot = 0;
    yo.length = 10;
    yo.health = 90;
    yo.status = engine::Elimination::alive;
    yo.eliminated_on_turn = -1;
    // Encerrada en una franja de 3x3 en la esquina: 9 casillas para un cuerpo de 10.
    for (int i = 0; i < 10; ++i) {
        yo.cells[static_cast<unsigned>(i)] =
            static_cast<std::uint16_t>(engine::State11::Board::index_of(
                {static_cast<std::int8_t>(i % 3), static_cast<std::int8_t>(i / 3)}));
    }
    s.refresh_occupancy();

    snake::Params con_terr;
    con_terr.territory.version = 1;
    snake::Params sin_terr;
    // Las dos tienen que ver el mismo problema: la posicion es mala en las dos escalas.
    const double a = snake::evaluate(s, s.you, con_terr);
    const double b = snake::evaluate(s, s.you, sin_terr);
    INFO("con territorio " << a << ", sin territorio " << b);
    REQUIRE(a < 0.0);
    REQUIRE(b < 0.0);
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
    snake::warmup(params);
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
