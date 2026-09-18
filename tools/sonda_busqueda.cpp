/// @file sonda_busqueda.cpp
/// Que profundidad alcanza la busqueda dentro del presupuesto, y cuanto tarda.
///
/// Es la pregunta que decide si la busqueda sirve: con 350 ms y `apply` a 164 ns caben
/// ~2 millones de turnos simulados por movimiento, pero eso es el techo teorico. Esto mide
/// el real sobre los fixtures. ver docs/decisions/ADR-0022-busqueda-paranoica.md
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include <engine/rng.hpp>
#include <engine/rules.hpp>

#include <snake/brain.hpp>
#include <snake/config_loader.hpp>
#include <snake/search.hpp>

#include <nlohmann/json.hpp>

namespace {

/// Posiciones de media partida con 4 serpientes vivas. Los fixtures no sirven para
/// calibrar el presupuesto: 14 de 15 tienen 2 serpientes y acaban la profundidad 8 en
/// menos de 60 ms. El torneo midio un p50 de 348 ms, o sea que en partida real se agota,
/// y lo que se agota es esto. ver docs/decisions/ADR-0023-presupuesto-de-computo.md
engine::State11 estado_de_media_partida(engine::Rng& rng) {
    using Board = engine::State11::Board;
    engine::State11 s{};
    s.snake_count = 4;
    s.you = 0;
    for (int k = 0; k < 4; ++k) {
        auto& sn = s.snakes[static_cast<unsigned>(k)];
        sn.head_slot = 0;
        sn.health = static_cast<std::uint8_t>(40 + rng.next() % 60);
        sn.status = engine::Elimination::alive;
        sn.eliminated_on_turn = -1;
        sn.length = static_cast<std::uint16_t>(6 + rng.next() % 8);
        int c = static_cast<int>(rng.next() % engine::State11::cells);
        for (int seg = 0; seg < sn.length; ++seg) {
            sn.cells[static_cast<unsigned>(seg)] = static_cast<std::uint16_t>(c);
            const int d = static_cast<int>(rng.next() % 4);
            const auto pos = Board::coord_of(c);
            const engine::Coord n{static_cast<std::int8_t>(pos.x + (d == 0) - (d == 1)),
                                  static_cast<std::int8_t>(pos.y + (d == 2) - (d == 3))};
            if (Board::in_bounds(n)) {
                c = Board::index_of(n);
            }
        }
    }
    for (int i = 0; i < 3; ++i) {
        s.food.set(static_cast<int>(rng.next() % engine::State11::cells));
    }
    // Hazards por un borde, como en royale a media partida.
    for (int x = 0; x < 2; ++x) {
        for (int y = 0; y < 11; ++y) {
            s.hazards.set(
                Board::index_of({static_cast<std::int8_t>(x), static_cast<std::int8_t>(y)}));
        }
    }
    s.rules.hazard_damage_per_turn = 14;
    s.refresh_occupancy();
    return s;
}

std::vector<std::pair<std::string, nlohmann::json>> load_fixtures() {
    std::vector<std::pair<std::string, nlohmann::json>> out;
    for (const auto& e : std::filesystem::directory_iterator(BSR_FIXTURES_DIR)) {
        if (e.path().extension() != ".json") {
            continue;
        }
        std::ifstream f(e.path());
        auto doc = nlohmann::json::parse(f, nullptr, false);
        if (!doc.is_discarded()) {
            out.emplace_back(e.path().filename().string(), std::move(doc));
        }
    }
    std::sort(
        out.begin(), out.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    return out;
}
} // namespace

int main(int argc, char** argv) {
    const int presupuesto_ms = argc > 1 ? std::atoi(argv[1]) : 350;
    const int rivales = argc > 2 ? std::atoi(argv[2]) : 2;

    snake::Params p;
    p.search.version = 1;
    p.search.max_rivals = rivales;
    snake::warmup(p);

    long long total_us = 0, total_nodos = 0, peor_us = 0;
    int n = 0, suma_prof = 0, prof_min = 99, estables = 0, suma_cambio = 0;
    for (const auto& [nombre, doc] : load_fixtures()) {
        engine::State11 s;
        if (!snake::parse_state(doc, s)) {
            continue;
        }
        const auto t0 = snake::Deadline::Clock::now();
        const snake::Deadline d(t0 + std::chrono::milliseconds(presupuesto_ms));
        const snake::SearchResult r = snake::search(s, d, p);
        const auto us = std::chrono::duration_cast<std::chrono::microseconds>(
                            snake::Deadline::Clock::now() - t0)
                            .count();
        const long long us_ll = static_cast<long long>(us);
        std::printf("  %-34s prof=%d (se fija en %d) nodos=%-9lld %6lld us\n",
                    nombre.c_str(),
                    r.depth,
                    r.last_change_depth,
                    r.nodes,
                    us_ll);
        estables += r.depth - r.last_change_depth;
        suma_cambio += r.last_change_depth;
        total_us += us_ll;
        total_nodos += r.nodes;
        peor_us = std::max(peor_us, us_ll);
        suma_prof += r.depth;
        prof_min = std::min(prof_min, r.depth);
        ++n;
    }
    if (n == 0) {
        std::printf("sin fixtures\n");
        return 1;
    }

    // --- posiciones de media partida, que es donde el presupuesto se agota de verdad ---
    {
        engine::Rng rng(20260918);
        std::vector<engine::State11> muestra;
        for (int i = 0; i < 40; ++i) {
            engine::State11 st = estado_de_media_partida(rng);
            if (engine::legal_moves(st, st.you) != engine::move_mask_none) {
                muestra.push_back(st);
            }
        }
        std::printf("\n-- %zu posiciones con 4 serpientes vivas --\n", muestra.size());
        for (const int ms : {350, 300, 250, 200, 150, 100}) {
            long long acum_prof = 0;
            long long acum_us = 0;
            long long tope_us = 0;
            int menor = 99;
            for (const auto& st : muestra) {
                const auto ini_t = snake::Deadline::Clock::now();
                const snake::Deadline dl(ini_t + std::chrono::milliseconds(ms));
                const snake::SearchResult res = snake::search(st, dl, p);
                const auto gastado =
                    static_cast<long long>(std::chrono::duration_cast<std::chrono::microseconds>(
                                               snake::Deadline::Clock::now() - ini_t)
                                               .count());
                acum_prof += res.depth;
                acum_us += gastado;
                tope_us = std::max(tope_us, gastado);
                menor = std::min(menor, res.depth);
            }
            const auto k = static_cast<double>(muestra.size());
            std::printf("  presupuesto %3d ms -> profundidad media %.2f  minima %d  "
                        "| %5lld us medios, %6lld peor\n",
                        ms,
                        static_cast<double>(acum_prof) / k,
                        menor,
                        static_cast<long long>(static_cast<double>(acum_us) / k),
                        tope_us);
        }
    }

    std::printf("\nprofundidades confirmatorias (sin cambiar la decision): %d de %d totales; "
                "el mejor movimiento se fija de media en la profundidad %.1f\n",
                estables,
                suma_prof,
                double(suma_cambio) / n);
    std::printf("\npresupuesto=%d ms rivales=%d | profundidad media=%.1f minima=%d | "
                "%lld us medios, %lld peor | %lld nodos/movimiento\n",
                presupuesto_ms,
                rivales,
                double(suma_prof) / n,
                prof_min,
                total_us / n,
                peor_us,
                total_nodos / n);
    // Lo unico que no es negociable: nunca pasarse del presupuesto.
    if (peor_us > presupuesto_ms * 1000LL) {
        std::printf("FAIL se paso del presupuesto\n");
        return 1;
    }
    return 0;
}
