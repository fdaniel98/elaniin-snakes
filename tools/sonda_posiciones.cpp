/// @file sonda_posiciones.cpp
/// Puntua un config sobre las posiciones donde una derrota se volvio irreversible.
///
///     sonda_posiciones <dir_posiciones> <config> [--rival config] [--nodos N]
///
/// Cada posicion se juega hasta el final -nuestra snake con el config pedido, la rival con
/// el de `--rival`, por defecto el default del repo- y se cuenta cuantos turnos sobrevive
/// la nuestra y por que muere. Es determinista: presupuesto por nodos, sin reloj.
///
/// Por que sirve: en los duelos perdidos contra snork Tree el espacio libre cae de 2.5
/// veces el cuerpo a cero en los ultimos 20 turnos con la salud intacta
/// (ver docs/experimentos-duelo.md#s-derrumbe). Estas son esas posiciones. Un candidato que
/// aguante mas turnos en ellas esta arreglando exactamente lo que nos mata, y se sabe en
/// segundos en vez de en un torneo de horas.
///
/// Lo que NO es: un veredicto. La rival aqui la juega nuestro propio cerebro, no Tree, asi
/// que mide "salir de posiciones asi", no "ganar a Tree". El veredicto sigue saliendo del
/// torneo contra gauntlet-v2. ver docs/strategy.md#s-barrido
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <span>
#include <string>
#include <vector>

#include <engine/rules.hpp>

#include <snake/brain.hpp>
#include <snake/config_loader.hpp>
#include <snake/search.hpp>

#include <nlohmann/json.hpp>

namespace {

snake::Deadline sin_reloj() {
    return snake::Deadline(snake::Deadline::Clock::now() + std::chrono::hours(1));
}

struct Resultado {
    int turnos{0};
    engine::Elimination causa{engine::Elimination::alive};
    bool gano{false};
};

/// Juega desde `inicio` hasta que la partida acaba. Devuelve lo que le paso a `you`.
Resultado juega(engine::State11 inicio,
                const snake::Params& nuestro,
                const snake::Params& rival,
                std::uint64_t semilla,
                int max_turnos) {
    Resultado out;
    const auto yo = inicio.you;
    engine::State11 s = inicio;
    const int turno0 = s.turn;
    while (!engine::is_terminal(s) && s.turn - turno0 < max_turnos) {
        std::array<engine::Direction, engine::State11::max_snakes> movs{};
        for (int i = 0; i < s.count(); ++i) {
            const auto id = static_cast<engine::SnakeId>(i);
            if (!engine::is_alive(s.snake(id).status)) {
                movs[static_cast<std::size_t>(i)] = engine::default_move(s, id);
                continue;
            }
            engine::State11 vista = s;
            vista.you = id;
            movs[static_cast<std::size_t>(i)] =
                snake::decide(vista, sin_reloj(), id == yo ? nuestro : rival).direction;
        }
        engine::apply(
            s,
            std::span<const engine::Direction>(movs.data(), static_cast<std::size_t>(s.count())));
        // Lo que el arbitro hace entre turnos y `apply` deja fuera. ver docs/rules.md#r-10
        s.food |= engine::spawn_food(s, semilla);
        if (!engine::is_alive(s.snake(yo).status)) {
            break;
        }
    }
    const auto& nos = s.snake(yo);
    out.causa = nos.status;
    out.turnos = engine::is_alive(nos.status) ? s.turn - turno0 : nos.eliminated_on_turn - turno0;
    out.gano = engine::is_alive(nos.status);
    return out;
}

const char* nombre_causa(engine::Elimination e) {
    switch (e) {
        case engine::Elimination::alive:
            return "viva";
        case engine::Elimination::out_of_health:
            return "hambre";
        case engine::Elimination::wall_collision:
            return "pared";
        case engine::Elimination::self_collision:
            return "cuerpo_propio";
        case engine::Elimination::snake_collision:
            return "cuerpo_rival";
        case engine::Elimination::head_collision:
            return "cabezazo";
        case engine::Elimination::hazard:
            return "hazard";
        default:
            return "otra";
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "uso: sonda_posiciones <dir> <config> [--rival config] [--nodos N]\n");
        return 2;
    }
    const std::string dir = argv[1];
    const std::string cfg_nuestro = argv[2];
    std::string cfg_rival = "snake/config/default.json";
    long long nodos = 14821;
    for (int i = 3; i + 1 < argc; i += 2) {
        const std::string k = argv[i];
        if (k == "--rival") {
            cfg_rival = argv[i + 1];
        } else if (k == "--nodos") {
            nodos = std::atoll(argv[i + 1]);
        }
    }

    snake::Params nuestro = snake::load_params(cfg_nuestro);
    snake::Params rival = snake::load_params(cfg_rival);
    nuestro.search.budget_nodes = static_cast<std::int32_t>(nodos);
    rival.search.budget_nodes = static_cast<std::int32_t>(nodos);
    snake::warmup(nuestro);

    std::vector<std::filesystem::path> ficheros;
    for (const auto& e : std::filesystem::directory_iterator(dir)) {
        if (e.path().extension() == ".json") {
            ficheros.push_back(e.path());
        }
    }
    std::sort(ficheros.begin(), ficheros.end());
    if (ficheros.empty()) {
        std::fprintf(stderr, "sin posiciones en %s\n", dir.c_str());
        return 2;
    }

    long long suma = 0;
    int n = 0, ganadas = 0;
    std::vector<int> turnos;
    for (const auto& f : ficheros) {
        std::ifstream in(f);
        const auto doc = nlohmann::json::parse(in, nullptr, false);
        if (doc.is_discarded()) {
            std::fprintf(stderr, "SALTO  %s no es JSON\n", f.filename().string().c_str());
            continue;
        }
        engine::State11 s;
        if (!snake::parse_state(doc, s)) {
            std::fprintf(stderr,
                         "SALTO  %s no describe un tablero jugable\n",
                         f.filename().string().c_str());
            continue;
        }
        // La semilla sale del nombre para que dos corridas de la misma posicion generen la
        // misma comida, y dos posiciones distintas no compartan racha.
        const std::uint64_t semilla = std::hash<std::string>{}(f.filename().string()) | 1ULL;
        const Resultado r = juega(s, nuestro, rival, semilla, 500);
        suma += r.turnos;
        turnos.push_back(r.turnos);
        ganadas += r.gano ? 1 : 0;
        ++n;
        std::printf("  %-34s %4d turnos  %s\n",
                    f.filename().string().c_str(),
                    r.turnos,
                    nombre_causa(r.causa));
    }
    if (n == 0) {
        return 2;
    }
    std::sort(turnos.begin(), turnos.end());
    std::printf("\n%s sobre %d posiciones: media %.1f turnos, mediana %d, sobrevive en %d\n",
                cfg_nuestro.c_str(),
                n,
                static_cast<double>(suma) / n,
                turnos[static_cast<std::size_t>(n / 2)],
                ganadas);
    return 0;
}
