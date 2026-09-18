/// @file server.cpp
/// Servidor HTTP: GET / (personalizacion), GET /health (sonda), POST /start, /move, /end.
///
/// Contrato de red: escucha en 0.0.0.0:$PORT (default 8080), limita el cuerpo a
/// 256 KiB, fija timeouts de lectura y escritura, responde 404 sin parsear el cuerpo
/// en rutas desconocidas y NUNCA devuelve 5xx en /move.
/// ver docs/invariants.md#inv-12

#include <chrono>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>

#include <engine/state.hpp>

#include <snake/brain.hpp>
#include <snake/config_loader.hpp>
#include <snake/deadline.hpp>

#include <httplib.h>
#include <nlohmann/json.hpp>

namespace {

using nlohmann::json;

constexpr std::size_t max_payload_bytes = std::size_t{256} * 1024;
constexpr int default_port = 8080;
// Una peticion de Battlesnake son unos pocos KiB por loopback: si el cuerpo no ha
// llegado en 2 s, no va a llegar. Con 5 s bastaban ocho conexiones a medio abrir
// -`Content-Length` anunciado y cuerpo que nunca llega- para agotar el pool de hilos y
// dejar sin respuesta a una peticion legitima, que es peor que un 5xx: el arbitro no
// recibe nada y aplica su movimiento por defecto.
constexpr time_t socket_timeout_seconds = 2;
/// Hilos del servidor. El default de cpp-httplib es 8, y ocho conexiones lentas lo
/// agotan. No elimina el problema -un atacante decidido abre mas- pero lo aleja del
/// accidente, y el timeout corto recicla los hilos deprisa.
constexpr unsigned int thread_pool_size = 64;

const char* direction_name(engine::Direction d) {
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

const char* variant_name(engine::Variant v) {
    switch (v) {
        case engine::Variant::standard:
            return "standard";
        case engine::Variant::royale:
            return "royale";
        case engine::Variant::wrapped:
            return "wrapped";
        case engine::Variant::constrictor:
            return "constrictor";
        case engine::Variant::wrapped_constrictor:
            return "wrapped_constrictor";
        case engine::Variant::solo:
            return "solo";
        case engine::Variant::unknown:
            return "unknown";
    }
    return "unknown";
}

int port_from_env() {
    const char* raw = std::getenv("PORT");
    if (raw == nullptr) {
        return default_port;
    }
    try {
        const int value = std::stoi(raw);
        if (value > 0 && value < 65536) {
            return value;
        }
    } catch (const std::exception& error) {
        // Un PORT invalido no debe tumbar el proceso, pero tampoco callarse.
        std::cerr << "WARN=PORT_invalido valor=" << raw << " motivo=" << error.what() << "\n";
    }
    return default_port;
}

/// Linea de log por movimiento, parseable y de una sola linea.
void log_move(int turn,
              engine::Variant variant,
              const snake::Move& move,
              long long micros,
              bool degraded,
              const engine::Ruleset& rules) {
    std::cout << "move turn=" << turn << " ruleset=" << variant_name(variant)
              << " degraded=" << (degraded ? 1 : 0) << " move=" << direction_name(move.direction)
              << " fallback=" << move.fallback_level << " candidates=" << move.considered
              << " score=" << move.score << " us=" << micros;
    if (rules.fallbacks.any()) {
        std::cout << " WARN=fallback_ruleset";
        if (rules.fallbacks.timeout) {
            std::cout << ",timeout";
        }
        if (rules.fallbacks.hazard_damage) {
            std::cout << ",hazardDamagePerTurn";
        }
        if (rules.fallbacks.shrink_every_n_turns) {
            std::cout << ",shrinkEveryNTurns";
        }
        if (rules.fallbacks.variant) {
            std::cout << ",name";
        }
        if (rules.fallbacks.map_name) {
            std::cout << ",map";
        }
    }
    std::cout << '\n' << std::flush;
}

} // namespace

int main() {
    const snake::Params params = snake::load_params("snake/config/default.json");

    httplib::Server server;
    server.set_payload_max_length(max_payload_bytes);
    server.new_task_queue = [] { return new httplib::ThreadPool(thread_pool_size); };
    server.set_read_timeout(socket_timeout_seconds, 0);
    server.set_write_timeout(socket_timeout_seconds, 0);

    server.Get("/", [](const httplib::Request&, httplib::Response& res) {
        const json info{{"apiversion", "1"},
                        {"author", "battlesnake-royale"},
                        {"color", "#1b5e20"},
                        {"head", "beluga"},
                        {"tail", "bolt"},
                        {"version", "v0-baseline"}};
        res.set_content(info.dump(), "application/json");
    });

    // La sonda NO invoca el cerebro: el GET / de personalizacion lo consume el motor y
    // no sirve como health check.
    server.Get("/health", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(R"({"status":"ok"})", "application/json");
    });

    server.Post("/start", [](const httplib::Request& req, httplib::Response& res) {
        const json request = json::parse(req.body, nullptr, false);
        // `is_object()` y no solo `!is_discarded()`: `value()` sobre un array o un escalar
        // lanza `type_error.306`, y sin nadie que lo recoja cpp-httplib respondia 500 con
        // el mensaje interno en una cabecera. ver docs/invariants.md#inv-12
        if (request.is_object()) {
            const engine::Ruleset rules =
                snake::parse_ruleset(request.value("game", json::object()));
            std::cerr << "start ruleset=" << variant_name(rules.variant)
                      << " timeout=" << rules.timeout_ms
                      << " hazard=" << rules.hazard_damage_per_turn
                      << " shrink=" << rules.shrink_every_n_turns
                      << (engine::is_supported(rules.variant) ? "" : " WARN=variante_no_soportada")
                      << '\n';
        }
        res.set_content("{}", "application/json");
    });

    server.Post("/move", [&params](const httplib::Request& req, httplib::Response& res) {
        const auto started = std::chrono::steady_clock::now();
        snake::Move move{};
        engine::Variant variant = engine::Variant::unknown;
        engine::Ruleset rules{};
        int turn = 0;
        bool degraded = true;

        // El try NO tapa ningun fallo conocido: el parser aguanta los 14 payloads
        // adversos del check 8. Esta para que INV-12 deje de depender de que toda rama
        // futura sea cuidadosa; si algo lanza, se responde el ultimo escalon del
        // fail-safe en vez de un 500, que haria que el arbitro aplicase su movimiento por
        // defecto. ver docs/invariants.md#inv-12
        try {
            const json request = json::parse(req.body, nullptr, false);
            engine::State11 state;
            if (!request.is_discarded() && snake::parse_state(request, state)) {
                rules = state.rules;
                variant = rules.variant;
                turn = state.turn;
                degraded = !engine::is_supported(variant);
                const auto deadline =
                    snake::Deadline::from_timeout(rules.timeout_ms, params.time, started);
                move = degraded ? snake::decide_degraded(state, deadline, params)
                                : snake::decide(state, deadline, params);
            } else {
                // Tablero no instanciado -el cerebro es 11x11, ver el ADR de la fase 2-,
                // payload invalido o serpiente propia ausente: escalon 3, el ultimo del
                // fail-safe; nunca 5xx. ver docs/rules.md#r-03
                move = snake::Move{engine::Direction::up, 3, 0.0, 0};
                std::cerr << "WARN=payload_no_soportado\n";
            }
        } catch (const std::exception& error) {
            move = snake::Move{engine::Direction::up, 3, 0.0, 0};
            std::cerr << "WARN=excepcion_en_move motivo=" << error.what() << "\n";
        } catch (...) {
            move = snake::Move{engine::Direction::up, 3, 0.0, 0};
            std::cerr << "WARN=excepcion_en_move motivo=desconocido\n";
        }

        const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(
                                std::chrono::steady_clock::now() - started)
                                .count();
        log_move(turn, variant, move, micros, degraded, rules);

        const json reply{{"move", direction_name(move.direction)}, {"shout", ""}};
        res.set_content(reply.dump(), "application/json");
    });

    server.Post("/end", [](const httplib::Request&, httplib::Response& res) {
        res.set_content("{}", "application/json");
    });

    // Rutas desconocidas: 404 sin tocar el cuerpo.
    server.set_error_handler([](const httplib::Request&, httplib::Response& res) {
        res.set_content(R"({"error":"not found"})", "application/json");
    });

    // Red de ultimo recurso para CUALQUIER ruta: sin esto cpp-httplib responde 500 y
    // ademas filtra el mensaje de la excepcion en una cabecera. En /move el 500 es lo
    // peor que puede pasar -el arbitro aplica su propio movimiento por defecto- asi que
    // se responde el ultimo escalon del fail-safe. ver docs/invariants.md#inv-12
    server.set_exception_handler(
        [](const httplib::Request& req, httplib::Response& res, const std::exception_ptr& ep) {
            std::string motivo = "desconocido";
            try {
                std::rethrow_exception(ep);
            } catch (const std::exception& error) {
                motivo = error.what();
            } catch (...) {
                // Algo que ni siquiera hereda de std::exception. No hay nada que
                // consultarle, pero tragarselo es justo el punto: la alternativa es que
                // cpp-httplib responda 500, que es lo que esta funcion existe para evitar.
                motivo = "excepcion que no hereda de std::exception";
            }
            std::cerr << "WARN=excepcion ruta=" << req.path << " motivo=" << motivo << "\n";
            res.status = 200;
            if (req.path == "/move") {
                res.set_content(R"({"move":"up","shout":""})", "application/json");
            } else {
                res.set_content("{}", "application/json");
            }
        });

    const int port = port_from_env();
    std::cerr << "listening 0.0.0.0:" << port << '\n';
    if (!server.listen("0.0.0.0", port)) {
        std::cerr << "ERROR=no_se_pudo_escuchar port=" << port << '\n';
        return 1;
    }
    return 0;
}
