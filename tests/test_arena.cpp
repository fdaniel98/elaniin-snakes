/// @file test_arena.cpp
/// La arena tiene una sola propiedad que la justifica: que dos corridas identicas den
/// partidas identicas, en cualquier maquina y bajo cualquier carga. Todo lo demas de aqui
/// existe para que esa propiedad no se rompa en silencio.

#include <arena/arena.hpp>
#include <array>
#include <cstdint>
#include <vector>

#include <engine/rng.hpp>

#include <snake/params.hpp>

#include <catch2/catch_test_macros.hpp>

namespace {

/// Presupuesto pequeño a proposito: estos casos comprueban propiedades, no fuerza. Los
/// numeros publicables salen de `tools/sonda_arena.cpp`.
snake::Params contendiente(int nodos) {
    snake::Params p;
    p.search.budget_nodes = nodos;
    return p;
}

arena::ArenaConfig cfg_royale(std::uint64_t semilla) {
    arena::ArenaConfig cfg;
    cfg.seed = semilla;
    cfg.rules.map_is_royale = true;
    cfg.rules.shrink_every_n_turns = 25;
    return cfg;
}

} // namespace

TEST_CASE("arena: misma semilla y mismos params, misma partida", "[arena]") {
    const std::vector<snake::Params> cuatro(4, contendiente(120));
    for (std::uint64_t semilla : {1U, 7U, 99U}) {
        const arena::Partida a = arena::play(cfg_royale(semilla), cuatro);
        const arena::Partida b = arena::play(cfg_royale(semilla), cuatro);
        INFO("semilla " << semilla << " turnos " << a.turnos);
        REQUIRE(a.final == arena::Final::ok);
        REQUIRE(b.final == arena::Final::ok);
        REQUIRE(a.turnos == b.turnos);
        for (int i = 0; i < 4; ++i) {
            const auto& x = a.snakes[static_cast<std::size_t>(i)];
            const auto& y = b.snakes[static_cast<std::size_t>(i)];
            REQUIRE(x.puesto == y.puesto);
            REQUIRE(x.turnos_vividos == y.turnos_vividos);
            REQUIRE(x.causa == y.causa);
            REQUIRE(x.movimientos == y.movimientos);
            // Los nodos son la prueba de que el reloj no participo: si hubiera cortado,
            // dos corridas de la misma posicion gastarian distinto.
            REQUIRE(x.nodos == y.nodos);
            REQUIRE(x.cortes_por_reloj == 0);
        }
    }
}

TEST_CASE("arena: los puestos son los del motor, con rango compartido", "[arena][r-12]") {
    const std::vector<snake::Params> cuatro(4, contendiente(120));
    for (std::uint64_t semilla = 1; semilla <= 8; ++semilla) {
        const arena::Partida r = arena::play(cfg_royale(semilla), cuatro);
        INFO("semilla " << semilla);
        REQUIRE(r.final == arena::Final::ok);
        REQUIRE(r.contendientes == 4);
        float suma = 0.0F;
        int primeros = 0;
        for (int i = 0; i < 4; ++i) {
            const float p = r.snakes[static_cast<std::size_t>(i)].puesto;
            REQUIRE(p >= 1.0F);
            REQUIRE(p <= 4.0F);
            suma += p;
            primeros += p == 1.0F ? 1 : 0;
        }
        REQUIRE(suma == 10.0F);
        // O gana una, o el empate a la cabeza reparte rango compartido y no hay ningun 1.0
        REQUIRE(primeros <= 1);
        REQUIRE(r.turnos > 0);
    }
}

TEST_CASE("arena: sin presupuesto de nodos no se juega", "[arena]") {
    // Con `budget_nodes` en 0 y el deadline inalcanzable de la arena, la busqueda iria
    // hasta `max_depth`. Es un error de uso y se dice en vez de tardar una eternidad.
    // ver docs/decisions/ADR-0030-presupuesto-por-nodos.md#d-0301
    std::vector<snake::Params> mezcla(4, contendiente(100));
    mezcla[2].search.budget_nodes = 0;
    const arena::Partida r = arena::play(cfg_royale(3), mezcla);
    REQUIRE(r.final == arena::Final::sin_presupuesto);
    REQUIRE(r.turnos == 0);

    // v0 no busca, asi que no necesita tope: su config sin `budget_nodes` es legitima.
    std::vector<snake::Params> con_v0(4, contendiente(100));
    con_v0[1].search.version = 0;
    con_v0[1].search.budget_nodes = 0;
    con_v0[1].territory.version = 0;
    REQUIRE(arena::play(cfg_royale(3), con_v0).final == arena::Final::ok);
}

TEST_CASE("arena: el asiento decide con SUS params, no con los del vecino", "[arena]") {
    // Si la arena se equivocara de asiento, un A/B mediria la configuracion contraria a
    // la que cree. La forma barata de verlo: v0 en un asiento cambia la partida, y
    // cambiarlo de asiento produce OTRA partida distinta.
    std::vector<snake::Params> a(4, contendiente(120));
    a[0].search.version = 0;
    a[0].territory.version = 0;
    std::vector<snake::Params> b(4, contendiente(120));
    b[3].search.version = 0;
    b[3].territory.version = 0;

    const arena::Partida ra = arena::play(cfg_royale(5), a);
    const arena::Partida rb = arena::play(cfg_royale(5), b);
    REQUIRE(ra.final == arena::Final::ok);
    REQUIRE(rb.final == arena::Final::ok);
    // v0 no gasta nodos de busqueda; el asiento que los tiene a cero es el que lo lleva.
    REQUIRE(ra.snakes[0].nodos == 0);
    REQUIRE(ra.snakes[3].nodos > 0);
    REQUIRE(rb.snakes[0].nodos > 0);
    REQUIRE(rb.snakes[3].nodos == 0);
}

TEST_CASE("arena: mismo tablero inicial para ramas distintas", "[arena]") {
    // Es la mitad del pareado: A y B arrancan de la misma posicion con la misma comida.
    // La otra mitad -que el sorteo de comida de cada turno tampoco dependa de la
    // historia- la cubre el caso de `spawn_food`. ver docs/rules.md#r-10
    const engine::Ruleset reglas{};
    const auto x = engine::start_board<11, 11, 4>(4, reglas, 4242);
    const auto y = engine::start_board<11, 11, 4>(4, reglas, 4242);
    REQUIRE((x.food ^ y.food).none());
    REQUIRE((x.bodies ^ y.bodies).none());
}

TEST_CASE("arena: una partida sin hazards tambien termina", "[arena]") {
    // Sin el rectangulo que encoge, cuatro serpientes pueden dar muchas vueltas: el tope
    // de turnos existe para que un torneo no se cuelgue, y aqui se comprueba que no hace
    // falta usarlo en una partida normal.
    arena::ArenaConfig cfg;
    cfg.seed = 21;
    cfg.rules.map_is_royale = false;
    const std::vector<snake::Params> cuatro(4, contendiente(120));
    const arena::Partida r = arena::play(cfg, cuatro);
    REQUIRE(r.final == arena::Final::ok);
    REQUIRE(r.turnos < cfg.max_turns);
}

// ---------------------------------------------------------------------------------------
// Robustez: reglas absurdas, del tipo que un fuzzer o un config mal escrito producen.
// La arena es el instrumento con el que se mide TODO lo que venga; un defecto aqui no da
// una medicion mala y visible, da una creible y equivocada.
// ---------------------------------------------------------------------------------------

TEST_CASE("arena: cadencia de shrink invalida no se juega", "[arena][r-09]") {
    // `royale_hazards()` devuelve un tablero SIN hazards cuando la cadencia es menor que 1,
    // y el llamante no puede distinguir eso de un turno anterior al primer shrink. El
    // motor oficial aborta la partida (`maps/royale.go:50-52`). Era una precondicion sin
    // comprobar y anotada en STATE.md desde la fase 1; ahora hay llamante y se comprueba.
    const std::vector<snake::Params> cuatro(4, contendiente(100));
    for (const int cadencia : {0, -1, -25}) {
        arena::ArenaConfig cfg = cfg_royale(3);
        cfg.rules.shrink_every_n_turns = cadencia;
        INFO("cadencia " << cadencia);
        REQUIRE(arena::play(cfg, cuatro).final == arena::Final::reglas_invalidas);
    }
    // Sin mapa royale la cadencia no se usa para nada y no deberia estorbar.
    arena::ArenaConfig sin_royale;
    sin_royale.seed = 3;
    sin_royale.rules.map_is_royale = false;
    sin_royale.rules.shrink_every_n_turns = 0;
    REQUIRE(arena::play(sin_royale, cuatro).final == arena::Final::ok);
}

TEST_CASE("arena: numero de contendientes fuera de rango", "[arena]") {
    const std::vector<snake::Params> ninguno;
    REQUIRE(arena::play(cfg_royale(1), ninguno).contendientes == 0);

    const std::vector<snake::Params> una(1, contendiente(100));
    // Con una sola serpiente `is_terminal` corta de inmediato: queda una viva.
    const arena::Partida r = arena::play(cfg_royale(1), una);
    REQUIRE(r.contendientes == 1);
    REQUIRE(r.turnos == 0);

    // Mas contendientes que sillas: se acotan, no se escribe fuera del array.
    const std::vector<snake::Params> demasiadas(9, contendiente(100));
    REQUIRE(arena::play(cfg_royale(1), demasiadas).contendientes == 4);
}

TEST_CASE("arena: mesas mezcladas, con v0 y con pesos extremos", "[arena][fuzz]") {
    // Un A/B pone configuraciones distintas en la misma mesa. Si una mezcla rompiera algo
    // -un cerebro que decide raro, otro que no busca- lo haria en medio de una corrida de
    // miles de partidas y nadie lo veria.
    engine::Rng rng(777);
    for (int caso = 0; caso < 60; ++caso) {
        std::vector<snake::Params> mesa;
        for (int i = 0; i < 4; ++i) {
            snake::Params p = contendiente(40 + static_cast<int>(rng.bounded(200)));
            switch (rng.bounded(4)) {
                case 0: // v0: no busca
                    p.search.version = 0;
                    p.territory.version = 0;
                    break;
                case 1: // pesos absurdos por arriba
                    p.territory.weight = 1e6;
                    p.head.avoid_equal_or_longer = -500.0;
                    break;
                case 2: // todo a cero: la evaluacion no distingue nada
                    p.territory.weight = 0.0;
                    p.space.weight = 0.0;
                    p.food.weight = 0.0;
                    p.length.advantage_weight = 0.0;
                    break;
                default:
                    break;
            }
            mesa.push_back(p);
        }
        arena::ArenaConfig cfg = cfg_royale(rng.next() | 1ULL);
        const arena::Partida r = arena::play(cfg, mesa);
        INFO("caso " << caso);
        REQUIRE((r.final == arena::Final::ok || r.final == arena::Final::tope_turnos));
        float suma = 0.0F;
        for (int i = 0; i < 4; ++i) {
            suma += r.snakes[static_cast<std::size_t>(i)].puesto;
            REQUIRE(r.snakes[static_cast<std::size_t>(i)].cortes_por_reloj == 0);
        }
        REQUIRE(suma == 10.0F);
    }
}

TEST_CASE("arena: reglas absurdas no cuelgan ni revientan", "[arena][fuzz]") {
    // No se exige que la partida tenga sentido con estos parametros: se exige que el
    // motor no se salga del tablero, no se quede sin terminar y no mienta en los puestos.
    engine::Rng rng(20260920);
    const std::vector<snake::Params> cuatro(4, contendiente(60));

    for (int caso = 0; caso < 120; ++caso) {
        arena::ArenaConfig cfg;
        cfg.seed = rng.next() | 1ULL;
        cfg.rules.map_is_royale = (rng.bounded(2) == 0);
        cfg.rules.shrink_every_n_turns = 1 + static_cast<std::int32_t>(rng.bounded(60));
        cfg.rules.minimum_food = static_cast<std::int32_t>(rng.bounded(200));
        cfg.rules.food_spawn_chance = static_cast<std::int32_t>(rng.bounded(300)) - 100;
        cfg.rules.hazard_damage_per_turn = static_cast<std::int32_t>(rng.bounded(300)) - 100;
        // Incluye 0 y negativos: un `max_turns` absurdo no puede colgar el bucle ni
        // producir un puesto invalido, solo una partida que no se juega.
        cfg.max_turns = static_cast<int>(rng.bounded(400)) - 2;

        const arena::Partida r = arena::play(cfg, cuatro);
        INFO("caso " << caso << " semilla " << cfg.seed << " hazard "
                     << cfg.rules.hazard_damage_per_turn << " minimo_comida "
                     << cfg.rules.minimum_food << " chance " << cfg.rules.food_spawn_chance);
        REQUIRE(r.contendientes == 4);
        REQUIRE(r.turnos >= 0);
        REQUIRE(r.turnos <= (cfg.max_turns > 0 ? cfg.max_turns : 0));
        REQUIRE((r.final == arena::Final::ok || r.final == arena::Final::tope_turnos));

        float suma = 0.0F;
        for (int i = 0; i < 4; ++i) {
            const arena::Resultado& x = r.snakes[static_cast<std::size_t>(i)];
            REQUIRE(x.puesto >= 1.0F);
            REQUIRE(x.puesto <= 4.0F);
            REQUIRE(x.movimientos >= 0);
            REQUIRE(x.cortes_por_reloj == 0);
            suma += x.puesto;
        }
        REQUIRE(suma == 10.0F);
    }
}
