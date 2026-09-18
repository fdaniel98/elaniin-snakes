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

#include <snake/brain.hpp>
#include <snake/config_loader.hpp>
#include <snake/search.hpp>

#include <nlohmann/json.hpp>

namespace {
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
    int n = 0, suma_prof = 0, prof_min = 99;
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
        std::printf("  %-34s prof=%d nodos=%-9lld %6lld us  rivales=%d\n",
                    nombre.c_str(),
                    r.depth,
                    r.nodes,
                    us_ll,
                    r.rivals_simulated);
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
