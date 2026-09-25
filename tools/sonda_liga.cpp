/// @file sonda_liga.cpp
/// Que ve la busqueda en una posicion concreta, movimiento a movimiento.
///
///     sonda_liga <dir_o_json> <config> [--ms N] [--nodos N]
///
/// Para cada peticion /move imprime: el movimiento que devuelve `decide()`, la profundidad
/// completada, el valor de raiz de cada direccion y el ESPACIO CON RELOJ de cada una -las
/// casillas alcanzables contando que cada segmento se libera cuando su cola pasa-. Sirve
/// para ver si una derrota del leaderboard fue una rendicion (todas las raices a muerte) o
/// un error de la evaluacion. ver docs/experimentos-duelo.md#s-liga-0925
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <engine/rules.hpp>

#include <snake/brain.hpp>
#include <snake/config_loader.hpp>
#include <snake/eval/timed.hpp>
#include <snake/search.hpp>

#include <nlohmann/json.hpp>

namespace {

const char* nombre(int d) {
    static const std::array<const char*, 4> n{"up", "down", "left", "right"};
    return n[static_cast<std::size_t>(d)];
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "uso: sonda_liga <dir|json> <config> [--ms N] [--nodos N]\n");
        return 2;
    }
    const std::filesystem::path origen = argv[1];
    snake::Params p = snake::load_params(argv[2]);
    int ms = 150;
    for (int i = 3; i + 1 < argc; i += 2) {
        const std::string k = argv[i];
        if (k == "--ms") {
            ms = std::atoi(argv[i + 1]);
        } else if (k == "--nodos") {
            p.search.budget_nodes = std::atoi(argv[i + 1]);
        }
    }
    snake::warmup(p);
    std::vector<std::filesystem::path> ficheros;
    if (std::filesystem::is_directory(origen)) {
        for (const auto& e : std::filesystem::directory_iterator(origen)) {
            if (e.path().extension() == ".json") {
                ficheros.push_back(e.path());
            }
        }
    } else {
        ficheros.push_back(origen);
    }
    std::sort(ficheros.begin(), ficheros.end());
    for (const auto& f : ficheros) {
        std::ifstream in(f);
        const auto doc = nlohmann::json::parse(in, nullptr, false);
        engine::State11 s;
        if (doc.is_discarded() || !snake::parse_state(doc, s)) {
            std::fprintf(stderr, "SALTO %s\n", f.filename().string().c_str());
            continue;
        }
        const auto t0 = snake::Deadline::Clock::now();
        const snake::Deadline dl(t0 + std::chrono::milliseconds(ms));
        const snake::SearchResult r = snake::search(s, dl, p);
        const snake::Move m = snake::decide(
            s, snake::Deadline(snake::Deadline::Clock::now() + std::chrono::milliseconds(ms)), p);
        std::printf("%-22s decide=%-5s busqueda=%-5s prof=%2d score=%9.1f |",
                    f.stem().string().c_str(),
                    nombre(static_cast<int>(m.direction)),
                    nombre(static_cast<int>(r.best)),
                    r.depth,
                    r.score);
        const auto mask = engine::legal_moves(s, s.you);
        for (int d = 0; d < 4; ++d) {
            const auto dir = static_cast<engine::Direction>(d);
            if (!engine::mask_has(mask, dir)) {
                continue;
            }
            const int reloj = snake::eval::timed_space(s, s.you, dir);
            const double v = r.root_values[static_cast<std::size_t>(d)];
            std::printf(" %s:%s%.0f/esp%d", nombre(d), std::isnan(v) ? "?" : "", v, reloj);
        }
        std::printf("\n");
    }
    return 0;
}
