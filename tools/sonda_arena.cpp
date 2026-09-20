/// @file sonda_arena.cpp
/// Throughput de la arena y calibracion de `budget_nodes`.
///
///     sonda_arena [partidas] [nodos]
///     sonda_arena --calibrar
///
/// Imprime partidas/minuto, turnos medios y nodos por movimiento. Los numeros publicables
/// salen de aqui con los flags de `deploy`. ver docs/performance.md#p-02
///
/// `--calibrar` responde la otra pregunta, la que hay que contestar en CADA maquina antes
/// de montar un A/B: cuantos nodos caben en el presupuesto real de computo
/// (`time.max_compute_ms`). Un numero de nodos no es comparable entre maquinas ni entre
/// commits que cambian el coste del nodo.
/// ver docs/decisions/ADR-0030-presupuesto-por-nodos.md#d-0302

#include <algorithm>
#include <arena/arena.hpp>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <span>
#include <vector>

#include <engine/rng.hpp>
#include <engine/rules.hpp>

#include <snake/brain.hpp>
#include <snake/deadline.hpp>
#include <snake/params.hpp>
#include <snake/search.hpp>

namespace {

/// Cuantos nodos gasta la busqueda cuando la corta el RELOJ en el presupuesto real.
/// Es el numero que hay que pasarle a `--nodos` para que la arena piense como el
/// despliegue. Se mide en la maquina donde se va a correr el A/B, no en otra.
int calibrar() {
    snake::Params p;
    p.search.budget_nodes = 0; // aqui manda el reloj a proposito: es lo que se mide
    (void)snake::warmup(p);

    arena::ArenaConfig cfg;
    cfg.rules.map_is_royale = true;

    std::vector<long long> nodos;
    for (int semilla = 1; semilla <= 12; ++semilla) {
        cfg.seed = static_cast<std::uint64_t>(semilla);
        engine::State11 s = engine::start_board<11, 11, 4>(4, cfg.rules, cfg.seed);
        // Unas cuantas jugadas para salir del spawn, donde el tablero esta vacio y la
        // busqueda no se parece a la de una partida de verdad.
        for (int t = 0; t < 25 && !engine::is_terminal(s); ++t) {
            std::array<engine::Direction, 4> mv{};
            for (int i = 0; i < s.count(); ++i) {
                engine::State11 vista = s;
                vista.you = static_cast<engine::SnakeId>(i);
                mv[static_cast<std::size_t>(i)] =
                    snake::decide(vista,
                                  snake::Deadline(snake::Deadline::Clock::now() +
                                                  std::chrono::milliseconds(20)),
                                  p)
                        .direction;
            }
            engine::apply(
                s,
                std::span<const engine::Direction>(mv.data(), static_cast<std::size_t>(s.count())));
            s.food |= engine::spawn_food(s, cfg.seed);
            s.hazards =
                engine::royale_hazards<11, 11>(cfg.seed, s.turn, cfg.rules.shrink_every_n_turns);
        }
        if (engine::is_terminal(s)) {
            continue;
        }
        const snake::Deadline d(snake::Deadline::Clock::now() +
                                std::chrono::milliseconds(p.time.max_compute_ms));
        const snake::SearchResult r = snake::search(s, d, p);
        nodos.push_back(r.nodes);
    }
    if (nodos.empty()) {
        return 0;
    }
    std::sort(nodos.begin(), nodos.end());
    const long long mediana = nodos[nodos.size() / 2];
    std::printf("presupuesto        %d ms (time.max_compute_ms)\n", p.time.max_compute_ms);
    std::printf("posiciones         %zu (turno 25 de partidas distintas, 4 vivas)\n", nodos.size());
    std::printf("nodos minimo       %lld\n", nodos.front());
    std::printf("nodos mediana      %lld\n", mediana);
    std::printf("nodos maximo       %lld\n", nodos.back());
    std::printf("\n--nodos sugerido   %lld\n", mediana);
    std::printf("Este numero vale para ESTA maquina y ESTE commit. Re-calibra tras "
                "cualquier cambio de rendimiento.\n");
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc > 1 && std::strcmp(argv[1], "--calibrar") == 0) {
        return calibrar();
    }
    const int partidas = argc > 1 ? std::atoi(argv[1]) : 20;
    const int nodos = argc > 2 ? std::atoi(argv[2]) : 2000;

    snake::Params p;
    p.search.budget_nodes = nodos;
    const std::vector<snake::Params> contendientes(4, p);

    arena::ArenaConfig cfg;
    cfg.rules.map_is_royale = true;

    const auto t0 = std::chrono::steady_clock::now();
    long long turnos = 0;
    long long movimientos = 0;
    long long nodos_totales = 0;
    int invalidas = 0;
    int topes = 0;
    for (int i = 0; i < partidas; ++i) {
        cfg.seed = static_cast<std::uint64_t>(i) + 1;
        const arena::Partida r = arena::play(cfg, contendientes);
        turnos += r.turnos;
        for (int k = 0; k < r.contendientes; ++k) {
            movimientos += r.snakes[static_cast<std::size_t>(k)].movimientos;
            nodos_totales += r.snakes[static_cast<std::size_t>(k)].nodos;
        }
        invalidas += r.final == arena::Final::invalida ? 1 : 0;
        topes += r.final == arena::Final::tope_turnos ? 1 : 0;
    }
    const double seg = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

    std::printf("partidas          %d\n", partidas);
    std::printf("budget_nodes      %d\n", nodos);
    std::printf("segundos          %.2f\n", seg);
    std::printf("partidas/min      %.1f\n", seg > 0.0 ? partidas / seg * 60.0 : 0.0);
    std::printf("turnos medios     %.1f\n", static_cast<double>(turnos) / partidas);
    std::printf("movimientos       %lld\n", movimientos);
    std::printf("nodos/movimiento  %.1f\n",
                movimientos > 0
                    ? static_cast<double>(nodos_totales) / static_cast<double>(movimientos)
                    : 0.0);
    std::printf("invalidas         %d\n", invalidas);
    std::printf("tope de turnos    %d\n", topes);
    return invalidas == 0 ? 0 : 1;
}
