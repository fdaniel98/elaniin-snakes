/// @file arena.cpp
/// El bucle de partida. Coloca, decide, aplica, repone comida y regenera hazards.
///
/// Usa el MISMO `decide()` que el servidor, no una copia recortada: si la arena jugara
/// con otro cerebro, lo que mida no seria lo que se despliega.
/// ver docs/architecture.md#a-02

#include <arena/arena.hpp>
#include <array>
#include <chrono>
#include <cstddef>
#include <span>

#include <engine/rules.hpp>
#include <engine/state.hpp>

#include <snake/brain.hpp>
#include <snake/deadline.hpp>

namespace arena {

namespace {

/// Un deadline que no se alcanza. Con presupuesto por nodos el reloj no debe participar;
/// esto lo pone lo bastante lejos como para que, si participa, sea un fallo de verdad y
/// no un margen mal elegido. Quien corta queda marcado en `Move::corto_el_reloj`.
snake::Deadline sin_reloj() noexcept {
    return snake::Deadline(snake::Deadline::Clock::now() + std::chrono::hours(24));
}

} // namespace

Partida play(const ArenaConfig& cfg, std::span<const snake::Params> contendientes) noexcept {
    Partida out;
    const int n = static_cast<int>(contendientes.size()) < max_contendientes
                      ? static_cast<int>(contendientes.size())
                      : max_contendientes;
    out.contendientes = n;
    if (n <= 0) {
        return out;
    }

    // Sin tope de nodos no se juega. Con `budget_nodes` en 0 y un deadline inalcanzable
    // la busqueda profundizaria hasta `max_depth`, que ni termina en un tiempo util ni
    // seria reproducible. Es un error de uso y se dice, no se arregla por dentro.
    for (int i = 0; i < n; ++i) {
        if (contendientes[static_cast<std::size_t>(i)].search.version >= 1 &&
            contendientes[static_cast<std::size_t>(i)].search.budget_nodes <= 0) {
            out.final = Final::sin_presupuesto;
            return out;
        }
    }

    State s = engine::start_board<11, 11, 4>(n, cfg.rules, cfg.seed);
    // Los hazards del turno 0 son los que toquen por cadencia, igual que en el arbitro:
    // no se asume que el turno 0 esta limpio, se pregunta. ver docs/rules.md#r-09
    if (cfg.rules.map_is_royale) {
        s.hazards =
            engine::royale_hazards<11, 11>(cfg.seed, s.turn, cfg.rules.shrink_every_n_turns);
    }

    std::array<int, max_contendientes> vivos_hasta{};
    while (!engine::is_terminal(s) && s.turn < cfg.max_turns) {
        std::array<engine::Direction, max_contendientes> movimientos{};
        for (int i = 0; i < n; ++i) {
            const auto id = static_cast<engine::SnakeId>(i);
            if (!engine::is_alive(s.snake(id).status)) {
                // Una eliminada no decide; `apply` la ignora, pero el array va completo
                // para no activar la extension de "sin entrada" del motor.
                // ver docs/rules.md#r-03
                movimientos[static_cast<std::size_t>(i)] = engine::default_move(s, id);
                continue;
            }
            // `decide()` lee `state.you`: cada contendiente ve el tablero desde su asiento.
            State vista = s;
            vista.you = id;
            const snake::Move m =
                snake::decide(vista, sin_reloj(), contendientes[static_cast<std::size_t>(i)]);
            movimientos[static_cast<std::size_t>(i)] = m.direction;

            Resultado& r = out.snakes[static_cast<std::size_t>(i)];
            ++r.movimientos;
            r.nodos += m.nodes;
            r.profundidad_max = m.depth > r.profundidad_max ? m.depth : r.profundidad_max;
            if (m.corto_el_reloj) {
                ++r.cortes_por_reloj;
            }
            vivos_hasta[static_cast<std::size_t>(i)] = s.turn;
        }

        engine::apply(
            s, std::span<const engine::Direction>(movimientos.data(), static_cast<std::size_t>(n)));

        // Las dos cosas que el arbitro hace entre turnos y `apply` deja fuera a proposito.
        // ver docs/rules.md#r-10 y ver docs/rules.md#r-09
        s.food |= engine::spawn_food(s, cfg.seed);
        if (cfg.rules.map_is_royale) {
            s.hazards =
                engine::royale_hazards<11, 11>(cfg.seed, s.turn, cfg.rules.shrink_every_n_turns);
        }
    }

    out.turnos = s.turn;
    const auto puestos = engine::placements(s);
    bool alguna_sucia = false;
    for (int i = 0; i < n; ++i) {
        Resultado& r = out.snakes[static_cast<std::size_t>(i)];
        const auto& snake = s.snake(static_cast<engine::SnakeId>(i));
        r.puesto = puestos.rank[static_cast<std::size_t>(i)];
        r.causa = snake.status;
        r.turnos_vividos = engine::is_alive(snake.status) ? s.turn : snake.eliminated_on_turn;
        alguna_sucia = alguna_sucia || r.cortes_por_reloj > 0;
    }

    // El orden importa: una partida cortada por el reloj es invalida aunque ademas haya
    // llegado al tope de turnos, porque de la primera no se puede decir nada.
    if (alguna_sucia) {
        out.final = Final::invalida;
    } else if (s.turn >= cfg.max_turns && !engine::is_terminal(s)) {
        out.final = Final::tope_turnos;
    } else {
        out.final = Final::ok;
    }
    return out;
}

} // namespace arena
