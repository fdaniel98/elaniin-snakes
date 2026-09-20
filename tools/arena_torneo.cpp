/// @file arena_torneo.cpp
/// A/B en la arena: dos configuraciones contra el mismo campo, sobre los mismos bloques.
///
///     arena_torneo --a <config> [--b <config>] --campo <config> --bloques N
///                  [--semilla-base S] [--hilos T] [--nodos N]
///
/// `--hilos` por defecto deja DOS nucleos libres: estas corridas duran horas y la maquina
/// tiene que poder usarse mientras tanto.
///
/// Sin `--b` solo juega la rama `a`. Es lo que usa el afinado: el puesto medio de una
/// configuracion IDENTICA al campo es 2.5 exacto por construccion
/// (ver docs/experimentos.md#s-tercer-rival-r), asi que la referencia no hay que jugarla,
/// y cada evaluacion cuesta la mitad.
///
/// Emite un objeto JSON por partida a stdout. El reparto en bloques, la estadistica y el
/// veredicto NO viven aqui: los pone `training-room/arena_ab.py`, que reutiliza
/// `compara.py`. Una sola ruta estadistica en el repositorio.
///
/// Un **bloque** es una semilla con su rotacion de asientos completa: la candidata se
/// sienta en los cuatro asientos, y en los otros tres va el campo. Las dos ramas juegan
/// exactamente los mismos bloques, que es lo que hace pareada la comparacion.
/// ver docs/experimentos.md#exp-resumen
///
/// El campo son copias de una configuracion NUESTRA, no las snakes del zoo: aqui no hay
/// HTTP. Lo que eso significa y lo que no, en
/// ver docs/decisions/ADR-0031-que-mide-la-arena.md#d-0311

#include <arena/arena.hpp>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include <snake/brain.hpp>
#include <snake/config_loader.hpp>
#include <snake/params.hpp>

namespace {

struct Encargo {
    int bloque{0};
    std::uint64_t semilla{0};
    int asiento{0};
    char rama{'a'};
};

const char* nombre_causa(engine::Elimination e) noexcept {
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

const char* nombre_final(arena::Final f) noexcept {
    switch (f) {
        case arena::Final::ok:
            return "ok";
        case arena::Final::tope_turnos:
            return "tope_turnos";
        case arena::Final::invalida:
            return "invalida";
        case arena::Final::reglas_invalidas:
            return "reglas_invalidas";
        default:
            return "sin_presupuesto";
    }
}

std::string arg(int argc, char** argv, const char* clave, const std::string& por_defecto) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], clave) == 0) {
            return argv[i + 1];
        }
    }
    return por_defecto;
}

} // namespace

int main(int argc, char** argv) {
    const std::string ruta_a = arg(argc, argv, "--a", "");
    const std::string ruta_b = arg(argc, argv, "--b", "");
    const std::string ruta_campo = arg(argc, argv, "--campo", "");
    const int bloques = std::atoi(arg(argc, argv, "--bloques", "15").c_str());
    const auto semilla_base =
        static_cast<std::uint64_t>(std::atoll(arg(argc, argv, "--semilla-base", "1").c_str()));
    const int nodos = std::atoi(arg(argc, argv, "--nodos", "2000").c_str());
    // Por defecto se dejan DOS nucleos libres. Una corrida de afinado dura horas, y una
    // maquina con todos los nucleos al 100% no se puede usar para nada mientras tanto.
    // Es ademas la misma convencion que el orquestador aplica a los contenedores del zoo
    // (nucleos fisicos - 2). Con `--hilos N` se pide un numero exacto.
    const unsigned nucleos = std::thread::hardware_concurrency();
    const int por_defecto = nucleos > 3 ? static_cast<int>(nucleos) - 2 : 1;
    int hilos = std::atoi(arg(argc, argv, "--hilos", std::to_string(por_defecto).c_str()).c_str());
    if (hilos < 1) {
        hilos = 1;
    }

    if (ruta_a.empty() || ruta_campo.empty()) {
        std::fprintf(stderr,
                     "uso: arena_torneo --a <config> [--b <config>] --campo <config> "
                     "--bloques N [--semilla-base S] [--hilos T] [--nodos N]\n");
        return 2;
    }
    const bool hay_b = !ruta_b.empty();

    snake::Params pa = snake::load_params(ruta_a);
    snake::Params pb = hay_b ? snake::load_params(ruta_b) : pa;
    snake::Params pc = snake::load_params(ruta_campo);
    // El presupuesto por nodos lo fija el torneo, no el config: las dos ramas tienen que
    // pensar lo mismo o la comparacion mide el presupuesto.
    // ver docs/decisions/ADR-0030-presupuesto-por-nodos.md#d-0302
    pa.search.budget_nodes = nodos;
    pb.search.budget_nodes = nodos;
    pc.search.budget_nodes = nodos;

    std::vector<Encargo> encargos;
    encargos.reserve(static_cast<std::size_t>(bloques) * arena::max_contendientes * 2);
    for (int b = 0; b < bloques; ++b) {
        for (int asiento = 0; asiento < arena::max_contendientes; ++asiento) {
            encargos.push_back({b, semilla_base + static_cast<std::uint64_t>(b), asiento, 'a'});
            if (hay_b) {
                encargos.push_back({b, semilla_base + static_cast<std::uint64_t>(b), asiento, 'b'});
            }
        }
    }

    std::vector<std::string> salida(encargos.size());
    std::atomic<std::size_t> siguiente{0};
    // Progreso por stderr. La salida de stdout sigue saliendo entera y ordenada al final
    // -es lo que la hace identica con 1 hilo y con 16-, pero una corrida de una hora sin
    // una sola linea parece colgada, y entonces se mata y se pierde.
    std::atomic<std::size_t> hechas{0};
    const auto t_inicio = std::chrono::steady_clock::now();

    // Calentar UNA vez y antes de los hilos: `warmup` escribe un sumidero volatil y no
    // hace falta que compitan por el.
    (void)snake::warmup(pa);

    const auto trabajar = [&]() {
        while (true) {
            const std::size_t i = siguiente.fetch_add(1);
            if (i >= encargos.size()) {
                return;
            }
            const Encargo& e = encargos[i];
            std::vector<snake::Params> mesa(arena::max_contendientes, pc);
            mesa[static_cast<std::size_t>(e.asiento)] = e.rama == 'a' ? pa : pb;

            arena::ArenaConfig cfg;
            cfg.seed = e.semilla;
            cfg.rules.map_is_royale = true;
            const arena::Partida r = arena::play(cfg, mesa);

            const arena::Resultado& nuestro = r.snakes[static_cast<std::size_t>(e.asiento)];
            char buf[512];
            std::snprintf(buf,
                          sizeof(buf),
                          "{\"bloque\":%d,\"semilla\":%llu,\"asiento\":%d,\"rama\":\"%c\","
                          "\"puesto\":%.3f,\"turnos\":%d,\"turnos_vividos\":%d,"
                          "\"causa\":\"%s\",\"movimientos\":%d,\"nodos\":%lld,"
                          "\"profundidad_max\":%d,\"cortes_reloj\":%d,\"final\":\"%s\"}",
                          e.bloque,
                          static_cast<unsigned long long>(e.semilla),
                          e.asiento,
                          e.rama,
                          static_cast<double>(nuestro.puesto),
                          r.turnos,
                          nuestro.turnos_vividos,
                          nombre_causa(nuestro.causa),
                          nuestro.movimientos,
                          nuestro.nodos,
                          nuestro.profundidad_max,
                          nuestro.cortes_por_reloj,
                          nombre_final(r.final));
            salida[i] = buf;

            const std::size_t n = hechas.fetch_add(1) + 1;
            const double seg =
                std::chrono::duration<double>(std::chrono::steady_clock::now() - t_inicio).count();
            const double restan =
                n > 0 ? seg / static_cast<double>(n) * static_cast<double>(encargos.size() - n)
                      : 0.0;
            std::fprintf(stderr,
                         "\r%zu/%zu partidas  %.0f min transcurridos  ~%.0f min restantes ",
                         n,
                         encargos.size(),
                         seg / 60.0,
                         restan / 60.0);
            std::fflush(stderr);
        }
    };

    std::vector<std::thread> equipo;
    equipo.reserve(static_cast<std::size_t>(hilos));
    for (int t = 0; t < hilos; ++t) {
        equipo.emplace_back(trabajar);
    }
    for (auto& h : equipo) {
        h.join();
    }
    std::fprintf(stderr, "\n");

    // Se imprime por indice y no segun termina cada hilo: la salida es identica con 1
    // hilo y con 16, que es lo que hace comparables dos corridas.
    for (const auto& linea : salida) {
        std::printf("%s\n", linea.c_str());
    }
    return 0;
}
