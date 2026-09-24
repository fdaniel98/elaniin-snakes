/// @file sonda_tt.cpp
/// Que compra la tabla de transposicion EN EL DUELO: profundidad y nodos con ella y sin
/// ella, al mismo presupuesto.
///
/// Las posiciones son de duelo avanzado -dos serpientes largas, tablero medio lleno-,
/// que es donde se pierde (ver docs/experimentos-duelo.md#s-supervivencia-duelo-r). La sonda no
/// dice si se gana mas: dice si se calcula mas, que es la condicion previa.
/// ver docs/strategy.md#s-tabla-duelo
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <span>

#include <engine/rng.hpp>
#include <engine/rules.hpp>

#include <snake/brain.hpp>
#include <snake/search.hpp>

namespace {

/// Duelo REAL: se juega una partida de dos con el propio cerebro y se devuelve el estado
/// del turno pedido. Las posiciones sinteticas -cuerpos enrollados al azar- se resuelven
/// en 70 ms y mienten sobre el coste; estas son las que de verdad ve la snake.
engine::State11 duelo(engine::Rng& rng, const snake::Params& p, int turnos) {
    engine::State11 s = engine::start_board<11, 11, 4>(2, engine::Ruleset{}, rng.next() | 1U);
    const snake::Deadline lejos(snake::Deadline::Clock::now() + std::chrono::hours(1));
    snake::Params rapido = p;
    rapido.search.budget_nodes = 3000; // barato: solo hay que llegar a una posicion creible
    rapido.search.tt_version = 0;
    engine::State11 ultimo_vivo = s;
    for (int t = 0; t < turnos && s.alive_count() == 2; ++t) {
        ultimo_vivo = s;
        std::array<engine::Direction, 4> movs{};
        for (int k = 0; k < s.count(); ++k) {
            engine::State11 vista = s;
            vista.you = static_cast<engine::SnakeId>(k);
            movs[static_cast<unsigned>(k)] = snake::search(vista, lejos, rapido).best;
        }
        engine::apply(
            s,
            std::span<const engine::Direction>(movs.data(), static_cast<std::size_t>(s.count())));
        if (s.alive_count() < 2 || engine::is_terminal(s)) {
            break;
        }
        ultimo_vivo = s;
    }
    ultimo_vivo.you = 0;
    return ultimo_vivo;
}

} // namespace

int main(int argc, char** argv) {
    const int presupuesto_ms = argc > 1 ? std::atoi(argv[1]) : 150;
    const int casos = argc > 2 ? std::atoi(argv[2]) : 20;

    for (const int modo : {0, 1, 2, 3}) {
        const int tt = modo % 2;
        const int orden = modo / 2;
        engine::Rng rng(20260924);
        snake::Params p;
        p.search.version = 1;
        p.search.max_rivals = 2;
        p.search.tt_version = tt;
        p.search.order_version = orden;
        snake::warmup(p);
        long long nodos = 0, hits = 0, us_total = 0, peor_us = 0;
        int suma_prof = 0, prof_min = 99, n = 0;
        for (int i = 0; i < casos; ++i) {
            const engine::State11 s = duelo(rng, p, 60 + static_cast<int>(rng.next() % 60));
            const auto t0 = snake::Deadline::Clock::now();
            const snake::Deadline d(t0 + std::chrono::milliseconds(presupuesto_ms));
            const snake::SearchResult r = snake::search(s, d, p);
            const auto us = std::chrono::duration_cast<std::chrono::microseconds>(
                                snake::Deadline::Clock::now() - t0)
                                .count();
            if (i == 0) {
                std::printf("  (posicion 0: largos %d/%d, salud %d/%d, prof %d, nodos %lld)\n",
                            static_cast<int>(s.snakes[0].length),
                            static_cast<int>(s.snakes[1].length),
                            static_cast<int>(s.snakes[0].health),
                            static_cast<int>(s.snakes[1].health),
                            r.depth,
                            r.nodes);
            }
            nodos += r.nodes;
            hits += r.tt_hits;
            us_total += us;
            peor_us = std::max(peor_us, static_cast<long long>(us));
            suma_prof += r.depth;
            prof_min = std::min(prof_min, r.depth);
            ++n;
        }
        std::printf(
            "tt=%d orden=%d  profundidad media %.2f  minima %d  nodos/mov %lld  aciertos/mov %lld"
            "  %lld us medios  %lld peor\n",
            tt,
            orden,
            static_cast<double>(suma_prof) / n,
            prof_min,
            nodos / n,
            hits / n,
            us_total / n,
            peor_us);
    }
    return 0;
}
