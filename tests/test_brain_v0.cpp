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

#include <catch2/catch_approx.hpp>
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

/// Deadline "de sobra" para un test de comportamiento: 40 ms.
///
/// Eran 350, que es el presupuesto entero de un movimiento. Con v0 daba igual -decidia en
/// 100 us y volvia- pero desde que el default busca, cada llamada se los gasta enteros: la
/// bateria paso de 3 a 68 segundos y el selftest, que lanza un gate por veneno, dejo de
/// caber en un rato razonable. Estos tests comprueban legalidad y fail-safe, no fuerza,
/// y con 40 ms la busqueda llega a profundidad de sobra para eso. Los tests que SI miden
/// el presupuesto se fijan el suyo explicitamente.
/// Holgura de reloj admitida al comprobar un deadline, en milisegundos.
///
/// Un test de reloj no puede distinguir "nuestro codigo se paso" de "el sistema operativo
/// nos quito la CPU". Medido con `tools/sonda_overshoot.cpp` en una maquina tranquila:
/// 0 de 3000 fuera de presupuesto a 5, 20 y 50 ms, 0 de 500 a 200 ms, y el p50 clava el
/// deadline con 5 us de margen. Cuando la maquina esta cargada aparecen paradas de 1 a 3
/// ms que no son nuestras -el mismo host dio 9 ms de arranque en frio y 2 violaciones de
/// 10 000 en la fase 2-.
///
/// 25 ms separa las dos cosas con holgura: un rebasamiento ALGORITMICO no son 25 ms, son
/// los 200 del presupuesto entero, porque significaria que la busqueda no mira el reloj.
/// Con presupuestos de 1 o 5 ms esto es la diferencia entre un test que mide el cerebro y
/// uno que mide la maquina. ver docs/decisions/ADR-0027-tolerancia-de-reloj.md
constexpr long long k_holgura_reloj_ms = 25;

snake::Deadline generous() {
    return snake::Deadline(snake::Deadline::Clock::now() + std::chrono::milliseconds(40));
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

/// Parametros de v0: sin busqueda y sin territorio. Las expectativas de los fixtures se
/// escribieron contra el baseline, y desde que el default es v4 hay que pedirlo explicito.
/// v0 sigue siendo la referencia fija (§9) y sigue teniendo que cumplirlas.
namespace {
snake::Params params_v0() {
    snake::Params p;
    p.search.version = 0;
    p.territory.version = 0;
    return p;
}
} // namespace

TEST_CASE("brain_v0: se cumplen las expectativas de cada fixture", "[brain][fixtures]") {
    // v0 EXPLICITO, no el default: desde que el default es v4 la busqueda elige distinto
    // en algun fixture, y las expectativas se escribieron contra el baseline. No se relaja
    // nada: v0 sigue teniendo que cumplirlas todas, y v4 tiene sus propios tests de
    // legalidad y deadline sobre los mismos fixtures.
    //
    // El caso concreto que lo destapo, y que vale la pena leer: en
    // `02-spawn-turno2-cola-apilada.json` el fixture prohibe `down` y dice que bajar es
    // mortal. No lo es. El rival apunta hacia ABAJO -su cuello es (5,6)-, asi que no puede
    // subir por la columna 5; bajar a (5,8) es seguro. Lo cierto del fixture es que (5,7)
    // no se libera, que es otra cosa. v0 evita `down` por su regla ciega de no acercarse a
    // una serpiente mas larga; la busqueda ve que esa serpiente no puede venir.
    // Pendiente de decision humana en STATE.md.
    const snake::Params params = params_v0();
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

        REQUIRE(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() <=
                5 + k_holgura_reloj_ms);

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

        REQUIRE(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() <=
                5 + k_holgura_reloj_ms);
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
            REQUIRE(ms <= presupuesto_ms + k_holgura_reloj_ms);
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

    SECTION("un timeout mas corto que 500 sigue dejando buscar") {
        // El caso que el margen de red de 260 rompia en silencio: 300-260-50 = -10, que
        // `from_timeout` acota a 1 ms, y con 1 ms la snake juega por ordenacion estatica
        // sin buscar nada. El torneo puede anunciar un timeout distinto de 500 y el gate
        // no lo veria: ningun fixture lleva timeout propio.
        // ver docs/decisions/ADR-0037-margenes-medidos.md#d-0376
        const auto t0 = snake::Deadline::Clock::now();
        for (const int timeout : {250, 300, 400, 500}) {
            const auto d = snake::Deadline::from_timeout(timeout, p.time, t0);
            const auto presupuesto =
                std::chrono::duration_cast<std::chrono::milliseconds>(d.end() - t0).count();
            INFO("timeout=" << timeout << " presupuesto=" << presupuesto);
            // 100 ms es el suelo por debajo del cual se pierde un nivel entero de
            // profundidad (tabla del ADR-0023), el mismo suelo que exige la seccion de
            // arriba para el timeout de torneo.
            REQUIRE(presupuesto >= 100);
            // Y sigue cabiendo: computo + transporte medido (57 ms p99) bajo el timeout.
            REQUIRE(presupuesto + 57 < timeout);
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
        // Sin holgura A PROPOSITO: 250 ya son 50 ms por encima del presupuesto de 200, o
        // sea que la holgura ya esta dentro. Este es el presupuesto de PRODUCCION y aqui
        // la comprobacion se queda estricta.
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
            REQUIRE(ms <= presupuesto_ms + k_holgura_reloj_ms);
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
    // Territorio APAGADO: este test es sobre el tope de profundidad, no sobre la
    // evaluacion. Con Voronoi en las hojas cada nodo cuesta mas y bajo sanitizers no daba
    // tiempo a pasar de 8, con lo que el test volvia a medir velocidad en vez de
    // comportamiento. Es la segunda vez que me pasa en este mismo test.
    p.territory.version = 0;
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
                        .count() <= presupuesto_ms + k_holgura_reloj_ms);
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

namespace {
/// Dos serpientes en el mismo sitio, con las longitudes que se pidan. Sirve para aislar el
/// termino de ventaja: todo lo demas es identico.
engine::State11 duelo(int mi_largo, int su_largo) {
    using Board = engine::State11::Board;
    engine::State11 s{};
    s.snake_count = 2;
    s.you = 0;
    const int largos[2] = {mi_largo, su_largo};
    for (int k = 0; k < 2; ++k) {
        auto& sn = s.snakes[static_cast<unsigned>(k)];
        sn.head_slot = 0;
        sn.length = static_cast<std::uint16_t>(largos[k]);
        sn.health = 90;
        sn.status = engine::Elimination::alive;
        sn.eliminated_on_turn = -1;
        // Serpenteando por DOS columnas: en una sola, un cuerpo de 14 se sale de un
        // tablero de 11 y escribe fuera del bitboard. Lo cazo ASan, no yo.
        const int col = k == 0 ? 0 : 8;
        for (int seg = 0; seg < largos[k]; ++seg) {
            const int x = col + seg / 11;
            const int y = (seg / 11) % 2 == 0 ? seg % 11 : 10 - (seg % 11);
            sn.cells[static_cast<unsigned>(seg)] = static_cast<std::uint16_t>(
                Board::index_of({static_cast<std::int8_t>(x), static_cast<std::int8_t>(y)}));
        }
    }
    s.refresh_occupancy();
    return s;
}

snake::Params params_v5() {
    snake::Params p;
    p.search.version = 1;
    p.territory.version = 1;
    p.length.version = 1;
    return p;
}
} // namespace

TEST_CASE("v5: ir por delante en longitud puntua mejor que ir por detras", "[search][longitud]") {
    // Lo que decide un cabezazo no es ser largo, es ser MAS largo. Medido sobre las 60
    // partidas de v4: en 47 moriamos siendo iguales o mas cortos que todos los vivos, y el
    // puesto medio caia monotonamente con la desventaja. ver docs/experimentos.md#s-longitud
    const snake::Params v5 = params_v5();
    const double detras = snake::evaluate(duelo(5, 8), 0, v5);
    const double igual = snake::evaluate(duelo(8, 8), 0, v5);
    const double delante = snake::evaluate(duelo(8, 5), 0, v5);
    INFO("detras " << detras << " igual " << igual << " delante " << delante);
    REQUIRE(detras < igual);
    REQUIRE(igual < delante);
}

TEST_CASE("v5: la ventaja satura, no crece sin fin", "[search][longitud]") {
    // Un cuerpo enorme tambien encierra. Si el termino no saturara, la snake preferiria
    // crecer siempre, que es el error contrario al que estamos arreglando.
    snake::Params v5 = params_v5();
    v5.length.target_lead = 3;
    const double justo = snake::evaluate(duelo(8, 5), 0, v5);   // ventaja exacta de 3
    const double pasado = snake::evaluate(duelo(14, 5), 0, v5); // ventaja de 9

    // El termino de ventaja no puede aportar mas por pasar de 3. Lo que quede de
    // diferencia sale de otros terminos (espacio, territorio), nunca de la ventaja.
    snake::Params sin_ventaja = v5;
    sin_ventaja.length.advantage_weight = 0.0;
    const double justo0 = snake::evaluate(duelo(8, 5), 0, sin_ventaja);
    const double pasado0 = snake::evaluate(duelo(14, 5), 0, sin_ventaja);
    INFO("aporte con ventaja 3: " << justo - justo0 << ", con ventaja 9: " << pasado - pasado0);
    REQUIRE((pasado - pasado0) == Catch::Approx(justo - justo0));
}

TEST_CASE("v5: con la salud llena pero cortos, la comida sigue atrayendo", "[search][longitud]") {
    // El cambio de politica, en una linea: v4 solo miraba la comida con hambre. Con salud
    // 90 y tres de desventaja, v4 es indiferente a donde este la comida y v5 no.
    using Board = engine::State11::Board;
    engine::State11 cerca = duelo(5, 8);
    cerca.food.set(Board::index_of({1, 0})); // pegada a nuestra cabeza, en (0,0)
    cerca.refresh_occupancy();
    engine::State11 lejos = duelo(5, 8);
    lejos.food.set(Board::index_of({10, 10})); // en la otra punta
    lejos.refresh_occupancy();

    // v4 EXPLICITO: desde que el default es v5, `Params{}` ya trae el control de longitud
    // encendido, y sin apagarlo este test compararia v5 contra v5.
    const snake::Params v4 = [] {
        snake::Params p;
        p.search.version = 1;
        p.territory.version = 1;
        p.length.version = 0;
        return p;
    }();
    REQUIRE(snake::evaluate(cerca, 0, v4) == Catch::Approx(snake::evaluate(lejos, 0, v4)));

    const snake::Params v5 = params_v5();
    REQUIRE(snake::evaluate(cerca, 0, v5) > snake::evaluate(lejos, 0, v5));
}

TEST_CASE("v5: mismas garantias de legalidad y deadline", "[brain][search][longitud][inv-11]") {
    const snake::Params v5 = params_v5();
    snake::warmup(v5);
    for (const int presupuesto_ms : {1, 5, 200}) {
        for (const auto& fixture : load_fixtures()) {
            INFO("fixture: " << fixture.name << " presupuesto=" << presupuesto_ms);
            engine::State11 state;
            REQUIRE(snake::parse_state(fixture.doc, state));
            const auto t0 = snake::Deadline::Clock::now();
            const snake::Move move = snake::decide(
                state, snake::Deadline(t0 + std::chrono::milliseconds(presupuesto_ms)), v5);
            REQUIRE(std::chrono::duration_cast<std::chrono::milliseconds>(
                        snake::Deadline::Clock::now() - t0)
                        .count() <= presupuesto_ms + k_holgura_reloj_ms);
            const engine::MoveMask legal = engine::legal_moves(state, state.you);
            if (legal != engine::move_mask_none) {
                REQUIRE(engine::mask_has(legal, move.direction));
            }
        }
    }
}

namespace {
snake::Params params_v6() {
    snake::Params p = params_v5();
    p.survival.version = 1;
    return p;
}

/// Un estado de una sola serpiente con la salud y el hazard que se pidan, para aislar el
/// termino de supervivencia.
engine::State11 con_salud(int salud, bool en_hazard, int dano = 14) {
    using Board = engine::State11::Board;
    engine::State11 s{};
    s.snake_count = 1;
    s.you = 0;
    auto& yo = s.snakes[0];
    yo.head_slot = 0;
    yo.length = 5;
    yo.health = static_cast<std::uint8_t>(salud);
    yo.status = engine::Elimination::alive;
    yo.eliminated_on_turn = -1;
    for (int seg = 0; seg < 5; ++seg) {
        yo.cells[static_cast<unsigned>(seg)] =
            static_cast<std::uint16_t>(Board::index_of({5, static_cast<std::int8_t>(5 - seg)}));
    }
    s.rules.hazard_damage_per_turn = dano;
    if (en_hazard) {
        s.hazards.set(Board::index_of({5, 5}));
    }
    s.refresh_occupancy();
    return s;
}
} // namespace

TEST_CASE("v6: la misma salud vale MENOS dentro del hazard", "[search][supervivencia]") {
    // Es el cambio de unidad en una linea. Con 60 de salud y 14 de daño, fuera del hazard
    // quedan 60 turnos y dentro 4. v5 puntuaba las dos igual salvo por una penalizacion
    // plana; v6 las distingue por lo unico que importa, cuanto queda de vida.
    const snake::Params v6 = params_v6();
    const double limpio = snake::evaluate(con_salud(60, false), 0, v6);
    const double hazard = snake::evaluate(con_salud(60, true), 0, v6);
    INFO("limpio " << limpio << ", hazard " << hazard);
    REQUIRE(hazard < limpio);

    // Y la diferencia tiene que ser MAYOR que la que daba v5, que es el punto del cambio.
    const snake::Params v5 = params_v5();
    const double brecha_v6 = limpio - hazard;
    const double brecha_v5 =
        snake::evaluate(con_salud(60, false), 0, v5) - snake::evaluate(con_salud(60, true), 0, v5);
    INFO("brecha v5 " << brecha_v5 << ", v6 " << brecha_v6);
    REQUIRE(brecha_v6 > brecha_v5);
}

TEST_CASE("v6: el castigo por estar al borde es continuo, no un escalon",
          "[search][supervivencia]") {
    // El de v5 solo se activaba con <= 2 turnos de vida, cuando ya no da tiempo ni a salir
    // del hazard. Aqui se recorre la salud a la baja DENTRO del hazard y se exige que la
    // puntuacion baje en cada paso, sin mesetas.
    const snake::Params v6 = params_v6();
    double anterior = snake::evaluate(con_salud(100, true), 0, v6);
    for (const int salud : {90, 75, 60, 45, 30, 20, 10, 5}) {
        const double actual = snake::evaluate(con_salud(salud, true), 0, v6);
        INFO("salud " << salud << ": " << actual << " (anterior " << anterior << ")");
        REQUIRE(actual < anterior);
        anterior = actual;
    }
}

TEST_CASE("v6: mas margen del necesario no cambia nada", "[search][supervivencia]") {
    // El termino satura en `safe_turns`. Sin eso, la snake perseguiria comida con 90 de
    // salud en tablero limpio, que es el error contrario al que esto arregla.
    snake::Params v6 = params_v6();
    v6.survival.safe_turns = 25;
    const double t40 = snake::evaluate(con_salud(40, false), 0, v6);
    const double t90 = snake::evaluate(con_salud(90, false), 0, v6);
    INFO("salud 40 -> " << t40 << ", salud 90 -> " << t90);
    REQUIRE(t90 == Catch::Approx(t40));
}

TEST_CASE("v6: dentro del hazard el reloj corre aunque la salud parezca alta",
          "[search][supervivencia]") {
    // El caso que motivo el cambio: 50 de salud es "comodo" en salud absoluta -el umbral
    // de v5 era 50- y son 3.3 turnos dentro de un hazard de 14. Con v6 esa posicion tiene
    // que estar YA por debajo del umbral de busqueda de comida.
    const snake::Params v6 = params_v6();
    const engine::State11 s = con_salud(50, true);
    const double turnos = 50.0 / (1.0 + 14.0);
    INFO("turnos de vida reales: " << turnos);
    REQUIRE(turnos < static_cast<double>(v6.survival.seek_below_turns));
    // Y la posicion tiene que puntuar peor que la misma salud en tablero limpio, donde son
    // 50 turnos de sobra.
    REQUIRE(snake::evaluate(s, 0, v6) < snake::evaluate(con_salud(50, false), 0, v6));
}

TEST_CASE("v6: mismas garantias de legalidad y deadline",
          "[brain][search][supervivencia][inv-11]") {
    const snake::Params v6 = params_v6();
    snake::warmup(v6);
    for (const int presupuesto_ms : {1, 5, 200}) {
        for (const auto& fixture : load_fixtures()) {
            INFO("fixture: " << fixture.name << " presupuesto=" << presupuesto_ms);
            engine::State11 state;
            REQUIRE(snake::parse_state(fixture.doc, state));
            const auto t0 = snake::Deadline::Clock::now();
            const snake::Move move = snake::decide(
                state, snake::Deadline(t0 + std::chrono::milliseconds(presupuesto_ms)), v6);
            REQUIRE(std::chrono::duration_cast<std::chrono::milliseconds>(
                        snake::Deadline::Clock::now() - t0)
                        .count() <= presupuesto_ms + k_holgura_reloj_ms);
            const engine::MoveMask legal = engine::legal_moves(state, state.you);
            if (legal != engine::move_mask_none) {
                REQUIRE(engine::mask_has(legal, move.direction));
            }
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
        if (elapsed > 5 + k_holgura_reloj_ms) {
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

// ---------------------------------------------------------------------------------------
// Presupuesto por nodos. ver docs/decisions/ADR-0030-presupuesto-por-nodos.md
// ---------------------------------------------------------------------------------------

namespace {

/// Un deadline que no se alcanza: con presupuesto por nodos el reloj no debe participar,
/// y la unica forma de comprobarlo es darle tanto margen que, si corta, es un fallo.
snake::Deadline inalcanzable() {
    return snake::Deadline(snake::Deadline::Clock::now() + std::chrono::hours(1));
}

snake::Params params_con_nodos(int nodos) {
    snake::Params p;
    p.search.budget_nodes = nodos;
    return p;
}

} // namespace

TEST_CASE("nodos: la busqueda es funcion del estado, no de la maquina", "[search][nodos]") {
    // La propiedad que hace posible el A/B de la arena: mismo estado y mismo presupuesto
    // dan el mismo movimiento, el mismo numero de nodos y la misma profundidad, sin que
    // el reloj intervenga. Se repite con la maquina en el estado en que este.
    const snake::Params p = params_con_nodos(20000);
    engine::Rng rng(20260919);

    for (int caso = 0; caso < 40; ++caso) {
        const engine::State11 s =
            engine::start_board<11, 11, 4>(4, engine::Ruleset{}, 1000 + rng.next() % 500);
        const snake::SearchResult a = snake::search(s, inalcanzable(), p);
        const snake::SearchResult b = snake::search(s, inalcanzable(), p);
        INFO("caso " << caso);
        REQUIRE_FALSE(a.corto_el_reloj);
        REQUIRE_FALSE(b.corto_el_reloj);
        REQUIRE(a.best == b.best);
        REQUIRE(a.nodes == b.nodes);
        REQUIRE(a.depth == b.depth);
        REQUIRE(a.score == b.score);
        // El tope se respeta: se mira en cada nodo, asi que el exceso es el nodo en curso.
        REQUIRE(a.nodes <= p.search.budget_nodes);
    }
}

TEST_CASE("nodos: mas presupuesto no da menos profundidad", "[search][nodos]") {
    const engine::State11 s = engine::start_board<11, 11, 4>(4, engine::Ruleset{}, 7);
    int anterior = 0;
    for (const int nodos : {500, 2000, 8000, 32000}) {
        const snake::SearchResult r = snake::search(s, inalcanzable(), params_con_nodos(nodos));
        INFO("nodos " << nodos << " profundidad " << r.depth);
        REQUIRE_FALSE(r.corto_el_reloj);
        REQUIRE(r.depth >= anterior);
        REQUIRE(r.nodes <= nodos);
        anterior = r.depth;
    }
    REQUIRE(anterior > 1);
}

TEST_CASE("nodos: si corta el reloj se marca, y con tope 0 manda el reloj", "[search][nodos]") {
    const engine::State11 s = engine::start_board<11, 11, 4>(4, engine::Ruleset{}, 11);

    // Presupuesto de nodos enorme y deadline ridiculo: corta el reloj, y se dice.
    const snake::Deadline corto(snake::Deadline::Clock::now() + std::chrono::milliseconds(1));
    const snake::SearchResult r = snake::search(s, corto, params_con_nodos(100000000));
    REQUIRE(r.corto_el_reloj);

    // Sin tope de nodos el comportamiento es el de siempre: manda el reloj.
    snake::Params sin_tope;
    sin_tope.search.budget_nodes = 0;
    const snake::Deadline normal(snake::Deadline::Clock::now() + std::chrono::milliseconds(30));
    const snake::SearchResult v = snake::search(s, normal, sin_tope);
    REQUIRE(v.depth >= 1);
    REQUIRE(v.nodes > 0);
}

TEST_CASE("nodos: el tope no rompe la legalidad ni el fail-safe", "[search][nodos]") {
    // Un presupuesto absurdamente corto no puede producir un movimiento ilegal: se
    // devuelve el mejor por ordenacion estatica, que ya es legal.
    // ver docs/invariants.md#inv-10
    engine::Rng rng(4242);
    for (int caso = 0; caso < 200; ++caso) {
        const engine::State11 s =
            engine::start_board<11, 11, 4>(4, engine::Ruleset{}, 3000 + rng.next() % 1000);
        for (const int nodos : {1, 2, 7}) {
            const snake::SearchResult r = snake::search(s, inalcanzable(), params_con_nodos(nodos));
            const engine::MoveMask legales = engine::legal_moves(s, s.you);
            INFO("caso " << caso << " nodos " << nodos);
            if (legales != engine::move_mask_none) {
                REQUIRE(engine::mask_has(legales, r.best));
            }
        }
    }
}

// ---------------------------------------------------------------------------------
// Final de dos. ver docs/strategy.md#s-duelo
// ---------------------------------------------------------------------------------

TEST_CASE("duelo: con duel.version=0 el arbol no se entera de que existe", "[search][duelo]") {
    // El aislamiento no se promete, se comprueba. Con la version a 0 los pesos del duelo
    // pueden valer cualquier disparate y tiene que salir el mismo movimiento, el mismo
    // numero de nodos y la misma puntuacion, hasta el ultimo bit.
    engine::Rng rng(20260920);
    const snake::Params base = params_con_nodos(20000);
    snake::Params absurdo = base;
    absurdo.duel.prefer_shorter = 9999.0;
    absurdo.duel.pressure_weight = 9999.0;
    REQUIRE(base.duel.version == 0);

    for (int caso = 0; caso < 15; ++caso) {
        for (const int serpientes : {2, 3, 4}) {
            const engine::State11 s = engine::start_board<11, 11, 4>(
                serpientes, engine::Ruleset{}, 500 + static_cast<std::uint64_t>(rng.next() % 500));
            const snake::SearchResult a = snake::search(s, inalcanzable(), base);
            const snake::SearchResult b = snake::search(s, inalcanzable(), absurdo);
            INFO("caso " << caso << " serpientes " << serpientes);
            REQUIRE(a.best == b.best);
            REQUIRE(a.score == b.score);
            REQUIRE(a.nodes == b.nodes);
        }
    }
}

TEST_CASE("duelo: encendido cambia la evaluacion del 1v1 y no rompe la legalidad",
          "[search][duelo]") {
    // Dos cosas, y las dos hacen falta: que el camino nuevo se ejecute de verdad -un test
    // que pasa porque el codigo no se alcanza no vale nada- y que encenderlo no produzca
    // un movimiento ilegal, que es el invariante que no se negocia.
    // ver docs/invariants.md#inv-10
    engine::Rng rng(20260921);
    const snake::Params apagado = params_con_nodos(20000);
    snake::Params encendido = apagado;
    encendido.duel.version = 1;

    int distintos = 0;
    for (int caso = 0; caso < 30; ++caso) {
        const engine::State11 s = engine::start_board<11, 11, 4>(
            2, engine::Ruleset{}, 900 + static_cast<std::uint64_t>(rng.next() % 900));
        const snake::SearchResult a = snake::search(s, inalcanzable(), apagado);
        const snake::SearchResult b = snake::search(s, inalcanzable(), encendido);
        if (a.score != b.score || a.best != b.best) {
            ++distintos;
        }
        const engine::MoveMask legal = engine::legal_moves(s, s.you);
        INFO("caso " << caso);
        if (legal != engine::move_mask_none) {
            REQUIRE(engine::mask_has(legal, b.best));
        }
    }
    INFO("posiciones de duelo en que la evaluacion cambio: " << distintos << " de 30");
    REQUIRE(distintos > 0);
}

// ---------------------------------------------------------------------------------
// Posiciones REALES del torneo. ver docs/experimentos-duelo.md#s-desesperacion
// ---------------------------------------------------------------------------------

namespace {

engine::State11 estado_real(const char* nombre) {
    std::ifstream f(std::string(BSR_FIXTURES_REALES_DIR) + "/" + nombre);
    const json doc = json::parse(f, nullptr, false);
    REQUIRE_FALSE(doc.is_discarded());
    engine::State11 s;
    REQUIRE(snake::parse_state(doc, s));
    return s;
}

snake::Params params_torneo(int despair) {
    snake::Params p = snake::load_params(std::string(BSR_CONFIG_DIR) + "/default.json");
    p.search.budget_nodes = 20000; // determinista: la misma decision en cualquier maquina
    p.search.despair_version = despair;
    return p;
}

} // namespace

TEST_CASE("desesperacion: el duelo real dee2b0c8 reproduce el bolsillo y v11 no entra",
          "[brain][desesperacion][real]") {
    // Turno 241: cabeza en (9,10), cuerpo 20, 66 casillas alcanzables. `right` lleva a un
    // bolsillo de 3 casillas y es lo que hizo la snake desplegada. El primer REQUIRE
    // prueba que el fixture captura el fallo; sin el, el segundo pasaria por nada.
    const engine::State11 s = estado_real("dee2b0c8-t241.json");
    const auto lejos = snake::Deadline(snake::Deadline::Clock::now() + std::chrono::hours(1));

    const snake::Move v5 = snake::decide(s, lejos, params_torneo(0));
    INFO("v5 elige " << static_cast<int>(v5.direction) << " con puntuacion " << v5.score);
    REQUIRE(v5.direction == engine::Direction::right);
    REQUIRE(v5.score <= 0.5 * params_torneo(0).search.death_value);

    const snake::Move v11 = snake::decide(s, lejos, params_torneo(1));
    REQUIRE(v11.direction != engine::Direction::right);
    REQUIRE(engine::mask_has(engine::legal_moves(s, s.you), v11.direction));
}

TEST_CASE("desesperacion: si la busqueda no se rinde, v11 decide igual que v5",
          "[brain][desesperacion]") {
    // El cambio solo actua con la raiz en puntuacion de muerte. En cualquier otra posicion
    // tiene que ser invisible: mismo movimiento hasta el bit, sobre partidas de verdad.
    engine::Rng rng(20260922);
    const auto lejos = snake::Deadline(snake::Deadline::Clock::now() + std::chrono::hours(1));
    int comparadas = 0;
    for (int caso = 0; caso < 20; ++caso) {
        const engine::State11 s =
            engine::start_board<11, 11, 4>(2 + static_cast<int>(rng.next() % 3),
                                           engine::Ruleset{},
                                           100 + static_cast<std::uint64_t>(rng.next() % 1000));
        const snake::Move a = snake::decide(s, lejos, params_torneo(0));
        if (a.score <= 0.5 * params_torneo(0).search.death_value) {
            continue;
        }
        const snake::Move b = snake::decide(s, lejos, params_torneo(1));
        REQUIRE(a.direction == b.direction);
        REQUIRE(a.score == b.score);
        ++comparadas;
    }
    REQUIRE(comparadas > 0);
}

TEST_CASE("longitud en duelo: con length_version=0 el arbol no se entera", "[search][duelo]") {
    // Mismo contrato que el modo duelo: apagado, sus pesos pueden valer cualquier cosa.
    // ver docs/strategy.md#s-longitud-duelo
    engine::Rng rng(20260923);
    const snake::Params base = params_con_nodos(20000);
    snake::Params absurdo = base;
    absurdo.duel.length_weight = 9999.0;
    absurdo.duel.hunt_weight = 9999.0;
    REQUIRE(base.duel.length_version == 0);
    for (int caso = 0; caso < 15; ++caso) {
        for (const int serpientes : {2, 4}) {
            const engine::State11 s = engine::start_board<11, 11, 4>(
                serpientes, engine::Ruleset{}, 300 + static_cast<std::uint64_t>(rng.next() % 500));
            const snake::SearchResult a = snake::search(s, inalcanzable(), base);
            const snake::SearchResult b = snake::search(s, inalcanzable(), absurdo);
            INFO("caso " << caso << " serpientes " << serpientes);
            REQUIRE(a.best == b.best);
            REQUIRE(a.score == b.score);
            REQUIRE(a.nodes == b.nodes);
        }
    }
}

TEST_CASE("longitud en duelo: encendida cambia la evaluacion del 1v1 y no rompe nada",
          "[search][duelo]") {
    engine::Rng rng(20260924);
    const snake::Params apagado = params_con_nodos(20000);
    snake::Params encendido = apagado;
    encendido.duel.length_version = 1;
    int distintos = 0;
    for (int caso = 0; caso < 30; ++caso) {
        const engine::State11 s = engine::start_board<11, 11, 4>(
            2, engine::Ruleset{}, 700 + static_cast<std::uint64_t>(rng.next() % 900));
        const snake::SearchResult a = snake::search(s, inalcanzable(), apagado);
        const snake::SearchResult b = snake::search(s, inalcanzable(), encendido);
        distintos += (a.score != b.score || a.best != b.best) ? 1 : 0;
        const engine::MoveMask legal = engine::legal_moves(s, s.you);
        if (legal != engine::move_mask_none) {
            REQUIRE(engine::mask_has(legal, b.best));
        }
    }
    INFO("posiciones de duelo en que cambio: " << distintos << " de 30");
    REQUIRE(distintos > 0);
}

TEST_CASE("territorio en duelo: con territory_version=0 el arbol no se entera", "[search][duelo]") {
    engine::Rng rng(20260925);
    const snake::Params base = params_con_nodos(20000);
    snake::Params absurdo = base;
    absurdo.duel.territory_scale = 9999.0;
    REQUIRE(base.duel.territory_version == 0);
    for (int caso = 0; caso < 15; ++caso) {
        for (const int serpientes : {2, 4}) {
            const engine::State11 s = engine::start_board<11, 11, 4>(
                serpientes, engine::Ruleset{}, 400 + static_cast<std::uint64_t>(rng.next() % 500));
            const snake::SearchResult a = snake::search(s, inalcanzable(), base);
            const snake::SearchResult b = snake::search(s, inalcanzable(), absurdo);
            INFO("caso " << caso << " serpientes " << serpientes);
            REQUIRE(a.best == b.best);
            REQUIRE(a.score == b.score);
            REQUIRE(a.nodes == b.nodes);
        }
    }
}

TEST_CASE("territorio en duelo: encendido cambia el 1v1 y no rompe la legalidad",
          "[search][duelo]") {
    engine::Rng rng(20260926);
    const snake::Params apagado = params_con_nodos(20000);
    snake::Params encendido = apagado;
    encendido.duel.territory_version = 1;
    int distintos = 0;
    for (int caso = 0; caso < 30; ++caso) {
        const engine::State11 s = engine::start_board<11, 11, 4>(
            2, engine::Ruleset{}, 800 + static_cast<std::uint64_t>(rng.next() % 900));
        const snake::SearchResult a = snake::search(s, inalcanzable(), apagado);
        const snake::SearchResult b = snake::search(s, inalcanzable(), encendido);
        distintos += (a.score != b.score || a.best != b.best) ? 1 : 0;
        const engine::MoveMask legal = engine::legal_moves(s, s.you);
        if (legal != engine::move_mask_none) {
            REQUIRE(engine::mask_has(legal, b.best));
        }
    }
    REQUIRE(distintos > 0);
}

TEST_CASE("trampa en duelo: con trap_version=0 el arbol no se entera", "[search][duelo]") {
    engine::Rng rng(20260927);
    const snake::Params base = params_con_nodos(20000);
    snake::Params absurdo = base;
    absurdo.duel.trap_weight = 9999.0;
    absurdo.duel.trap_trigger_ratio = 99.0;
    REQUIRE(base.duel.trap_version == 0);
    for (int caso = 0; caso < 15; ++caso) {
        for (const int serpientes : {2, 4}) {
            const engine::State11 s = engine::start_board<11, 11, 4>(
                serpientes, engine::Ruleset{}, 300 + static_cast<std::uint64_t>(rng.next() % 500));
            const snake::SearchResult a = snake::search(s, inalcanzable(), base);
            const snake::SearchResult b = snake::search(s, inalcanzable(), absurdo);
            INFO("caso " << caso << " serpientes " << serpientes);
            REQUIRE(a.best == b.best);
            REQUIRE(a.score == b.score);
            REQUIRE(a.nodes == b.nodes);
        }
    }
}

TEST_CASE("trampa en duelo: en tablero abierto no se enciende ni se paga", "[search][duelo]") {
    // El umbral es la mitad de la idea: en royale los cuellos se rechazaron por dispararse
    // en el 92.9% de los estados (ver docs/experimentos.md#s-cuellos-r). Con el tablero
    // recien abierto la cuenta ni se hace, y eso tiene que verse en los nodos.
    engine::Rng rng(20260928);
    const snake::Params apagado = params_con_nodos(20000);
    snake::Params encendido = apagado;
    encendido.duel.trap_version = 1;
    encendido.duel.trap_weight = 9999.0;
    for (int caso = 0; caso < 10; ++caso) {
        const engine::State11 s = engine::start_board<11, 11, 4>(
            2, engine::Ruleset{}, 1200 + static_cast<std::uint64_t>(rng.next() % 900));
        const snake::SearchResult a = snake::search(s, inalcanzable(), apagado);
        const snake::SearchResult b = snake::search(s, inalcanzable(), encendido);
        INFO("caso " << caso);
        REQUIRE(a.best == b.best);
        REQUIRE(a.score == b.score);
    }
}

TEST_CASE("trampa en duelo: en el duelo real dee2b0c8 cambia la puntuacion del bolsillo",
          "[search][duelo][real]") {
    // Turno 241 del duelo que perdimos en el torneo: 2 serpientes, cuerpo 20, y `right`
    // lleva a un bolsillo. Es la clase de posicion para la que se escribio el termino.
    int cambios = 0;
    for (const char* nombre :
         {"dee2b0c8-t238.json", "dee2b0c8-t239.json", "dee2b0c8-t240.json", "dee2b0c8-t241.json"}) {
        const engine::State11 s = estado_real(nombre);
        snake::Params apagado = params_torneo(0);
        apagado.search.budget_nodes = 20000;
        snake::Params encendido = apagado;
        encendido.duel.trap_version = 1;
        encendido.duel.trap_weight = 400.0;
        encendido.duel.trap_trigger_ratio = 99.0; // fuerza la cuenta en esta posicion
        const snake::SearchResult a = snake::search(s, inalcanzable(), apagado);
        const snake::SearchResult b = snake::search(s, inalcanzable(), encendido);
        INFO(nombre << " apagado score " << a.score << " mov " << static_cast<int>(a.best)
                    << " | encendido score " << b.score << " mov " << static_cast<int>(b.best));
        cambios += (a.score != b.score || a.best != b.best) ? 1 : 0;
        const engine::MoveMask legal = engine::legal_moves(s, s.you);
        REQUIRE(engine::mask_has(legal, b.best));
    }
    REQUIRE(cambios > 0);
}

TEST_CASE("trampa en duelo: con cuatro serpientes vivas no se aplica", "[search][duelo]") {
    engine::Rng rng(20260929);
    const snake::Params apagado = params_con_nodos(20000);
    snake::Params encendido = apagado;
    encendido.duel.trap_version = 1;
    encendido.duel.trap_weight = 9999.0;
    encendido.duel.trap_trigger_ratio = 99.0;
    for (int caso = 0; caso < 15; ++caso) {
        const engine::State11 s = engine::start_board<11, 11, 4>(
            4, engine::Ruleset{}, 2000 + static_cast<std::uint64_t>(rng.next() % 500));
        const snake::SearchResult a = snake::search(s, inalcanzable(), apagado);
        const snake::SearchResult b = snake::search(s, inalcanzable(), encendido);
        INFO("caso " << caso);
        REQUIRE(a.best == b.best);
        REQUIRE(a.score == b.score);
    }
}

TEST_CASE("supervivencia en duelo: con survival_version=0 el arbol no se entera",
          "[search][duelo]") {
    engine::Rng rng(20260930);
    const snake::Params base = params_con_nodos(20000);
    snake::Params absurdo = base;
    absurdo.duel.survival_weight = 9999.0;
    absurdo.duel.tail_loop_weight = 9999.0;
    REQUIRE(base.duel.survival_version == 0);
    for (int caso = 0; caso < 15; ++caso) {
        for (const int serpientes : {2, 4}) {
            const engine::State11 s = engine::start_board<11, 11, 4>(
                serpientes, engine::Ruleset{}, 600 + static_cast<std::uint64_t>(rng.next() % 500));
            const snake::SearchResult a = snake::search(s, inalcanzable(), base);
            const snake::SearchResult b = snake::search(s, inalcanzable(), absurdo);
            INFO("caso " << caso << " serpientes " << serpientes);
            REQUIRE(a.best == b.best);
            REQUIRE(a.score == b.score);
            REQUIRE(a.nodes == b.nodes);
        }
    }
}

TEST_CASE("supervivencia en duelo: encendida cambia el 1v1 y no rompe la legalidad",
          "[search][duelo]") {
    engine::Rng rng(20260931);
    const snake::Params apagado = params_con_nodos(20000);
    snake::Params encendido = apagado;
    encendido.duel.survival_version = 1;
    int distintos = 0;
    for (int caso = 0; caso < 30; ++caso) {
        const engine::State11 s = engine::start_board<11, 11, 4>(
            2, engine::Ruleset{}, 1500 + static_cast<std::uint64_t>(rng.next() % 900));
        const snake::SearchResult a = snake::search(s, inalcanzable(), apagado);
        const snake::SearchResult b = snake::search(s, inalcanzable(), encendido);
        distintos += (a.score != b.score || a.best != b.best) ? 1 : 0;
        const engine::MoveMask legal = engine::legal_moves(s, s.you);
        if (legal != engine::move_mask_none) {
            REQUIRE(engine::mask_has(legal, b.best));
        }
    }
    INFO("posiciones de duelo en que cambio: " << distintos << " de 30");
    REQUIRE(distintos > 0);
}

TEST_CASE("supervivencia en duelo: con cuatro vivas no se aplica", "[search][duelo]") {
    engine::Rng rng(20260932);
    const snake::Params apagado = params_con_nodos(20000);
    snake::Params encendido = apagado;
    encendido.duel.survival_version = 1;
    encendido.duel.survival_weight = 9999.0;
    encendido.duel.tail_loop_weight = 9999.0;
    for (int caso = 0; caso < 15; ++caso) {
        const engine::State11 s = engine::start_board<11, 11, 4>(
            4, engine::Ruleset{}, 2500 + static_cast<std::uint64_t>(rng.next() % 500));
        const snake::SearchResult a = snake::search(s, inalcanzable(), apagado);
        const snake::SearchResult b = snake::search(s, inalcanzable(), encendido);
        INFO("caso " << caso);
        REQUIRE(a.best == b.best);
        REQUIRE(a.score == b.score);
    }
}

TEST_CASE("tabla de transposicion: no rompe la legalidad y es determinista", "[search][tabla]") {
    engine::Rng rng(20260933);
    snake::Params con_tabla = params_con_nodos(20000);
    con_tabla.search.tt_version = 1;
    for (int caso = 0; caso < 20; ++caso) {
        for (const int serpientes : {2, 4}) {
            const engine::State11 s = engine::start_board<11, 11, 4>(
                serpientes, engine::Ruleset{}, 3000 + static_cast<std::uint64_t>(rng.next() % 900));
            const snake::SearchResult a = snake::search(s, inalcanzable(), con_tabla);
            const snake::SearchResult b = snake::search(s, inalcanzable(), con_tabla);
            INFO("caso " << caso << " serpientes " << serpientes);
            // Determinista: la tabla se sella por busqueda, asi que dos llamadas seguidas
            // sobre el mismo estado dan lo mismo aunque la tabla venga caliente.
            REQUIRE(a.best == b.best);
            REQUIRE(a.score == b.score);
            const engine::MoveMask legal = engine::legal_moves(s, s.you);
            if (legal != engine::move_mask_none) {
                REQUIRE(engine::mask_has(legal, a.best));
            }
        }
    }
}

TEST_CASE("tabla de transposicion: apagada por defecto y sin efecto", "[search][tabla]") {
    const snake::Params base = params_con_nodos(20000);
    REQUIRE(base.search.tt_version == 0);
    REQUIRE(base.search.order_version == 0);
    engine::Rng rng(20260934);
    for (int caso = 0; caso < 10; ++caso) {
        const engine::State11 s = engine::start_board<11, 11, 4>(
            2, engine::Ruleset{}, 4000 + static_cast<std::uint64_t>(rng.next() % 500));
        const snake::SearchResult a = snake::search(s, inalcanzable(), base);
        INFO("caso " << caso);
        REQUIRE(a.tt_hits == 0);
    }
}

TEST_CASE("shrink: con shrink_version=0 el arbol no se entera", "[search][shrink]") {
    engine::Rng rng(20260935);
    snake::Params base = params_con_nodos(20000);
    base.search.max_rivals = 2;
    snake::Params absurdo = base;
    absurdo.hazard.shrink_weight = 9999.0;
    absurdo.hazard.shrink_lookahead = 99;
    REQUIRE(base.hazard.shrink_version == 0);
    for (int caso = 0; caso < 12; ++caso) {
        engine::Ruleset reglas;
        reglas.map_is_royale = true;
        engine::State11 s = engine::start_board<11, 11, 4>(
            4, reglas, 5000 + static_cast<std::uint64_t>(rng.next() % 500));
        s.turn = 120; // justo antes de un shrink
        const snake::SearchResult a = snake::search(s, inalcanzable(), base);
        const snake::SearchResult b = snake::search(s, inalcanzable(), absurdo);
        INFO("caso " << caso);
        REQUIRE(a.best == b.best);
        REQUIRE(a.score == b.score);
    }
}

TEST_CASE("shrink: fuera de royale no se aplica nunca", "[search][shrink]") {
    engine::Rng rng(20260936);
    snake::Params apagado = params_con_nodos(20000);
    snake::Params encendido = apagado;
    encendido.hazard.shrink_version = 1;
    encendido.hazard.shrink_weight = 9999.0;
    encendido.hazard.shrink_lookahead = 99;
    for (int caso = 0; caso < 12; ++caso) {
        engine::State11 s = engine::start_board<11, 11, 4>(
            4, engine::Ruleset{}, 6000 + static_cast<std::uint64_t>(rng.next() % 500));
        s.turn = 120;
        REQUIRE_FALSE(s.rules.map_is_royale);
        const snake::SearchResult a = snake::search(s, inalcanzable(), apagado);
        const snake::SearchResult b = snake::search(s, inalcanzable(), encendido);
        INFO("caso " << caso);
        REQUIRE(a.best == b.best);
        REQUIRE(a.score == b.score);
    }
}

TEST_CASE("shrink: en royale y con el shrink cerca, cambia la decision", "[search][shrink]") {
    engine::Rng rng(20260937);
    snake::Params apagado = params_con_nodos(20000);
    apagado.search.max_rivals = 2;
    snake::Params encendido = apagado;
    encendido.hazard.shrink_version = 1;
    int distintos = 0;
    for (int caso = 0; caso < 30; ++caso) {
        engine::Ruleset reglas;
        reglas.map_is_royale = true;
        engine::State11 s = engine::start_board<11, 11, 4>(
            4, reglas, 7000 + static_cast<std::uint64_t>(rng.next() % 900));
        // Hazard ya comido por dos lados, y el proximo shrink a dos turnos.
        for (int y = 0; y < 11; ++y) {
            s.hazards.set(engine::State11::Board::index_of({0, static_cast<std::int8_t>(y)}));
            s.hazards.set(engine::State11::Board::index_of({1, static_cast<std::int8_t>(y)}));
        }
        s.turn = 23;
        s.rules = reglas;
        const snake::SearchResult a = snake::search(s, inalcanzable(), apagado);
        const snake::SearchResult b = snake::search(s, inalcanzable(), encendido);
        distintos += (a.score != b.score || a.best != b.best) ? 1 : 0;
        const engine::MoveMask legal = engine::legal_moves(s, s.you);
        if (legal != engine::move_mask_none) {
            REQUIRE(engine::mask_has(legal, b.best));
        }
    }
    INFO("posiciones en que cambio: " << distintos << " de 30");
    REQUIRE(distintos > 0);
}

TEST_CASE("umbral de supervivencia: con ratio 0 es exactamente v15", "[search][duelo]") {
    engine::Rng rng(20260938);
    snake::Params v15 = params_con_nodos(20000);
    v15.duel.survival_version = 1;
    snake::Params v18 = v15;
    v18.duel.survival_below_ratio = 0.0; // 0 = sin umbral
    for (int caso = 0; caso < 12; ++caso) {
        const engine::State11 s = engine::start_board<11, 11, 4>(
            2, engine::Ruleset{}, 8000 + static_cast<std::uint64_t>(rng.next() % 500));
        const snake::SearchResult a = snake::search(s, inalcanzable(), v15);
        const snake::SearchResult b = snake::search(s, inalcanzable(), v18);
        INFO("caso " << caso);
        REQUIRE(a.best == b.best);
        REQUIRE(a.score == b.score);
        REQUIRE(a.nodes == b.nodes);
    }
}

TEST_CASE("umbral de supervivencia: en tablero abierto apaga el termino", "[search][duelo]") {
    // El tablero recien abierto tiene espacio de sobra, asi que con umbral el arbol tiene
    // que decidir como v5 -termino apagado- y no como v15.
    engine::Rng rng(20260939);
    snake::Params v5 = params_con_nodos(20000);
    snake::Params v18 = v5;
    v18.duel.survival_version = 1;
    v18.duel.survival_below_ratio = 1.6;
    v18.duel.survival_weight = 9999.0;
    v18.duel.tail_loop_weight = 9999.0;
    for (int caso = 0; caso < 12; ++caso) {
        const engine::State11 s = engine::start_board<11, 11, 4>(
            2, engine::Ruleset{}, 9000 + static_cast<std::uint64_t>(rng.next() % 500));
        const snake::SearchResult a = snake::search(s, inalcanzable(), v5);
        const snake::SearchResult b = snake::search(s, inalcanzable(), v18);
        INFO("caso " << caso);
        REQUIRE(a.best == b.best);
        REQUIRE(a.score == b.score);
    }
}
