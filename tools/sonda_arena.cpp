/// @file sonda_arena.cpp
/// Throughput de la arena y calibracion de `budget_nodes`.
///
///     sonda_arena [partidas] [nodos]
///
/// Imprime partidas/minuto, turnos medios y nodos por movimiento. Los numeros publicables
/// salen de aqui con los flags de `deploy`. ver docs/performance.md#p-02

#include <arena/arena.hpp>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include <snake/params.hpp>

int main(int argc, char** argv) {
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
