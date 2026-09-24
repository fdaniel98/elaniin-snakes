/// @file search.cpp
/// Busqueda paranoica con profundizacion iterativa. ver snake/include/snake/search.hpp

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

#include <engine/rules.hpp>

#include <snake/eval/floodfill.hpp>
#include <snake/eval/voronoi.hpp>
#include <snake/search.hpp>

namespace snake {

namespace {

using State = engine::State11;
using Board = State::Board;
using engine::Direction;
using engine::SnakeId;

constexpr int k_max_snakes = 4;

/// Casillas que seguiran ocupadas el proximo turno. Misma derivacion que en brain_v0:
/// la cola cuenta como libre salvo que los dos ultimos segmentos esten apilados.
/// ver docs/rules.md#r-04
Board blocked_cells(const State& s) noexcept {
    Board b;
    for (int i = 0; i < s.count(); ++i) {
        const auto& sn = s.snakes[static_cast<unsigned>(i)];
        if (!engine::is_alive(sn.status)) {
            continue;
        }
        const int last = static_cast<int>(sn.length) - 1;
        for (int seg = 0; seg <= last; ++seg) {
            if (seg == last && !sn.tail_is_stacked()) {
                continue;
            }
            b.set(sn.segment(seg));
        }
    }
    return b;
}

int manhattan(const State& s, int a, int b) noexcept {
    const auto pa = Board::coord_of(a);
    const auto pb = Board::coord_of(b);
    (void)s;
    return std::abs(pa.x - pb.x) + std::abs(pa.y - pb.y);
}

/// Ordena por clave descendente un array de como mucho 4 elementos, in situ.
///
/// No es `std::sort` por dos motivos, y el segundo es el bueno. El primero: GCC rechaza
/// `std::sort` sobre un array de 4 con -Warray-bounds, porque el introsort de libstdc++
/// llama a `__insertion_sort(first, first + 16)` y el compilador no puede demostrar que
/// ese camino no se toma. El segundo: esto se llama en CADA nodo de la busqueda, y montar
/// introsort para ordenar cuatro cosas cuesta mas que ordenarlas.
template <typename T> void ordena_desc(T* v, int n) noexcept {
    for (int i = 1; i < n; ++i) {
        T actual = v[i];
        int j = i - 1;
        while (j >= 0 && v[j].first < actual.first) {
            v[j + 1] = v[j];
            --j;
        }
        v[j + 1] = actual;
    }
}

/// Direcciones de `id` ordenadas para que la mas prometedora se pruebe primero. Con poda
/// alfa-beta el orden es la diferencia entre cortar el 90% del arbol y no cortar nada.
int ordered_moves(const State& s,
                  SnakeId id,
                  std::array<Direction, engine::direction_count>& out,
                  int order_version = 0) noexcept {
    const engine::MoveMask legal = engine::legal_moves(s, id);
    const Board blocked = blocked_cells(s);
    const Board free_cells = Board::full().without(blocked);
    const auto& me = s.snake(id);

    std::array<std::pair<int, Direction>, engine::direction_count> con_peso{};
    int n = 0;
    for (int d = 0; d < engine::direction_count; ++d) {
        const auto dir = static_cast<Direction>(d);
        if (!engine::mask_has(legal, dir)) {
            continue;
        }
        const auto next = engine::step(Board::coord_of(me.head()), dir);
        if (!Board::in_bounds(next)) {
            continue;
        }
        const int celda = Board::index_of(next);
        if (order_version >= 1) {
            // [v16] Barato: cuantas salidas tiene el destino. Ordena peor que el flood
            // fill, pero el flood fill se paga en cada nodo y para cada serpiente.
            Board una;
            una.set(celda);
            const int salidas = (una.expand().without(una) & free_cells).count();
            con_peso[static_cast<unsigned>(n)] = {salidas, dir};
        } else {
            Board alcanzable = free_cells;
            alcanzable.set(celda);
            con_peso[static_cast<unsigned>(n)] = {eval::flood(alcanzable, celda).cells, dir};
        }
        ++n;
    }
    // Sin ninguna legal se devuelven las geometricamente posibles: `apply` acepta la
    // mortal y la busqueda tiene que poder puntuar ese ramo, no quedarse sin hijos.
    if (n == 0) {
        for (int d = 0; d < engine::direction_count; ++d) {
            const auto dir = static_cast<Direction>(d);
            if (Board::in_bounds(engine::step(Board::coord_of(me.head()), dir))) {
                out[static_cast<unsigned>(n++)] = dir;
            }
        }
        if (n == 0) {
            out[0] = Direction::up;
            n = 1;
        }
        return n;
    }
    ordena_desc(con_peso.data(), n);
    for (int i = 0; i < n; ++i) {
        out[static_cast<unsigned>(i)] = con_peso[static_cast<unsigned>(i)].second;
    }
    return n;
}

// ---------------------------------------------------------------------------------
// [v16] Tabla de transposicion. ver docs/strategy.md#s-tabla-duelo
// ---------------------------------------------------------------------------------

/// Mezclador de 64 bits (splitmix64). Propio y determinista: la clave tiene que salir
/// igual en cualquier maquina o dos corridas de la arena dejarian de ser comparables.
constexpr std::uint64_t mezcla(std::uint64_t x) noexcept {
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}

/// Clave del estado. Entran las cosas de las que depende el valor de la posicion: quien
/// ocupa que, donde esta cada cabeza, cuanto mide y cuanta salud le queda, la comida y los
/// hazards. NO entra el turno: dos turnos distintos con el mismo tablero valen lo mismo, y
/// esa es justamente la transposicion que se quiere aprovechar.
std::uint64_t clave(const State& s) noexcept {
    std::uint64_t h = mezcla(static_cast<std::uint64_t>(s.you) + 1);
    for (int i = 0; i < s.count(); ++i) {
        const auto& sn = s.snakes[static_cast<unsigned>(i)];
        const std::uint64_t vivo = engine::is_alive(sn.status) ? 1U : 0U;
        h ^= mezcla(static_cast<std::uint64_t>(sn.head()) * 131U +
                    static_cast<std::uint64_t>(sn.length) * 17U +
                    static_cast<std::uint64_t>(sn.health) * 7U + vivo +
                    static_cast<std::uint64_t>(i) * 1000003U);
        // La cola tambien: dos posiciones con el mismo cuerpo pero distinta orientacion no
        // son la misma posicion.
        h ^= mezcla(static_cast<std::uint64_t>(sn.tail()) * 31U +
                    static_cast<std::uint64_t>(i) * 7919U + 3U);
    }
    for (int w = 0; w < Board::word_count; ++w) {
        h ^= mezcla(s.bodies.word(w) + static_cast<std::uint64_t>(w) * 11U);
        h ^= mezcla(s.food.word(w) * 3U + static_cast<std::uint64_t>(w) * 13U + 1U);
        h ^= mezcla(s.hazards.word(w) * 5U + static_cast<std::uint64_t>(w) * 17U + 2U);
    }
    return h;
}

enum class Limite : std::uint8_t { exacto, inferior, superior };

struct Entrada {
    std::uint64_t key{0};
    double valor{0.0};
    std::uint32_t sello{0};
    std::int16_t depth{-1};
    Limite limite{Limite::exacto};
    Direction mejor{Direction::up};
    bool con_mejor{false};
};

/// Una sola tabla por hilo, reservada una vez y reutilizada. No se asigna memoria dentro
/// de la busqueda: el `sello` invalida las entradas viejas sin tener que borrarlas.
/// ver docs/invariants.md#inv-03
constexpr int k_tt_bits_max = 22;

std::vector<Entrada>& tabla_de(int bits) noexcept {
    static thread_local std::vector<Entrada> tabla;
    static thread_local int bits_actuales = -1;
    const int b = std::clamp(bits, 10, k_tt_bits_max);
    if (b != bits_actuales) {
        tabla.assign(static_cast<std::size_t>(1) << static_cast<unsigned>(b), Entrada{});
        bits_actuales = b;
    }
    return tabla;
}

struct Contexto {
    const Params* params{nullptr};
    Deadline deadline{Deadline::Clock::now()};
    SnakeId us{0};
    long long nodes{0};
    /// Tope de nodos, 0 = sin tope. Con tope, la busqueda es una funcion pura del estado
    /// y no de la maquina, que es lo que la arena necesita para que un A/B sea pareable.
    long long budget_nodes{0};
    bool agotado{false};
    /// Cierto si quien corto fue el RELOJ. La arena exige que sea falso: si el reloj corta
    /// con presupuesto por nodos puesto, el resultado depende de la carga de la maquina y
    /// deja de ser reproducible. Mejor un fallo ruidoso que una medicion silenciosamente
    /// sucia. ver docs/decisions/ADR-0030-presupuesto-por-nodos.md#d-0301
    bool corto_el_reloj{false};
    std::array<SnakeId, k_max_snakes> rivales{};
    int n_rivales{0};
    /// [v16] Tabla de transposicion, nula si `search.tt_version` esta a 0.
    Entrada* tt{nullptr};
    std::size_t tt_mask{0};
    std::uint32_t sello{0};
    long long tt_hits{0};
};

/// `true` cuando toca abandonar, por nodos o por reloj.
///
/// El reloj se mira en CADA nodo, y esto empezo mirandolo cada 256 -luego 32, luego 8-
/// para "no gastar tiempo mirando la hora". Medido: con la comprobacion en cada nodo la
/// profundidad media sale identica hasta la centesima (11.97 y 10.89 en la sonda, los
/// mismos numeros que con 32), y en cambio saltarse nodos costaba violaciones del deadline
/// en cuanto la hoja se encarecio con el territorio: 12 de 10 000 con 32, 2 con 8, 0 con 1.
/// `Clock::now()` son ~25 ns contra el microsegundo largo que cuesta un nodo. Era una
/// optimizacion prematura que no compraba nada y pagaba en correccion.
///
/// Los nodos se miran ANTES que el reloj a proposito: con presupuesto por nodos puesto y
/// un deadline que no se alcanza, el reloj no participa en la decision y la busqueda
/// devuelve el mismo movimiento en una maquina cargada que en una vacia. El reloj no se
/// quita -INV-11 dice que nunca se excede el deadline, y eso vale tambien aqui-, se
/// MARCA: si es el quien corta, `corto_el_reloj` queda en cierto y el llamante decide si
/// eso invalida su medicion.
bool sin_presupuesto(Contexto& ctx) noexcept {
    if (ctx.agotado) {
        return true;
    }
    if (ctx.budget_nodes > 0 && ctx.nodes >= ctx.budget_nodes) {
        ctx.agotado = true;
        return true;
    }
    if (ctx.deadline.expired()) {
        ctx.agotado = true;
        ctx.corto_el_reloj = true;
    }
    return ctx.agotado;
}

// `negamax` y `peor_respuesta` se llaman mutuamente: eso es lo que es una busqueda, y
// misc-no-recursion no distingue una recursion de diseño de un accidente. La profundidad
// esta acotada por `search.max_depth` y cada nivel copia un `GameState` trivialmente
// copiable de pocos cientos de bytes, asi que la pila esta acotada tambien.
// NOLINTNEXTLINE(misc-no-recursion)
double negamax(State s, int depth, double alpha, double beta, Contexto& ctx) noexcept;

/// Minimiza sobre las combinaciones de movimientos de los rivales simulados. Es la mitad
/// paranoica: se supone que eligen a la vez lo peor para nosotros.
// NOLINTNEXTLINE(misc-no-recursion)
double peor_respuesta(const State& s,
                      Direction nuestro,
                      int depth,
                      double alpha,
                      double beta,
                      Contexto& ctx) noexcept {
    std::array<Direction, k_max_snakes> moves{};
    for (int i = 0; i < s.count(); ++i) {
        // Los rivales que no se simulan repiten lo que el motor oficial les aplicaria si
        // no contestaran. No es que vayan a hacer eso: es la suposicion mas barata que no
        // los deja quietos, que no es un estado que el juego produzca.
        // ver docs/rules.md#r-03
        moves[static_cast<unsigned>(i)] = engine::default_move(s, static_cast<SnakeId>(i));
    }
    moves[static_cast<unsigned>(ctx.us)] = nuestro;

    // El reloj se mira ANTES de ordenar los movimientos de los rivales. Ordenar hace un
    // flood fill por direccion y por rival, y es trabajo que no estaba acotado por nada:
    // con presupuestos muy cortos -el fuzz usa 5 ms- esa preparacion sola podia pasarse.
    if (sin_presupuesto(ctx)) {
        return ctx.params->search.death_value;
    }

    // Producto cartesiano de las direcciones de los rivales simulados, iterativo para no
    // recursar sobre el numero de rivales.
    std::array<std::array<Direction, engine::direction_count>, k_max_snakes> opciones{};
    std::array<int, k_max_snakes> cuantas{};
    long long combinaciones = 1;
    for (int r = 0; r < ctx.n_rivales; ++r) {
        const SnakeId id = ctx.rivales[static_cast<unsigned>(r)];
        cuantas[static_cast<unsigned>(r)] = ordered_moves(
            s, id, opciones[static_cast<unsigned>(r)], ctx.params->search.order_version);
        combinaciones *= cuantas[static_cast<unsigned>(r)];
    }

    double peor = std::numeric_limits<double>::infinity();
    for (long long c = 0; c < combinaciones; ++c) {
        if (sin_presupuesto(ctx)) {
            break;
        }
        long long resto = c;
        for (int r = 0; r < ctx.n_rivales; ++r) {
            const int k = cuantas[static_cast<unsigned>(r)];
            moves[static_cast<unsigned>(ctx.rivales[static_cast<unsigned>(r)])] =
                opciones[static_cast<unsigned>(r)][static_cast<unsigned>(resto % k)];
            resto /= k;
        }
        State siguiente = s;
        engine::apply(
            siguiente,
            std::span<const Direction>(moves.data(), static_cast<std::size_t>(s.count())));
        ++ctx.nodes;
        const double v = negamax(siguiente, depth - 1, alpha, beta, ctx);
        peor = std::min(peor, v);
        beta = std::min(beta, peor);
        // Poda: si los rivales ya pueden llevarnos por debajo de lo que tenemos
        // asegurado en otra rama, esta rama no nos la van a dejar jugar.
        if (beta <= alpha) {
            break;
        }
    }
    return std::isinf(peor) ? ctx.params->search.death_value : peor;
}

// NOLINTNEXTLINE(misc-no-recursion)
double negamax(State s, int depth, double alpha, double beta, Contexto& ctx) noexcept {
    const auto& yo = s.snake(ctx.us);
    if (!engine::is_alive(yo.status)) {
        // Morir pronto es peor que morir tarde: sin esto la busqueda es indiferente entre
        // el turno 3 y el turno 8, y prefiere el 3 por llegar antes a una hoja amplia.
        return ctx.params->search.death_value - ctx.params->search.survival_bonus * depth;
    }
    if (engine::is_terminal(s)) {
        return ctx.params->search.win_value + ctx.params->search.survival_bonus * depth;
    }
    // Sin tiempo NO se evalua. Esto parece un detalle y era el defecto que dominaba el
    // tail: cuando el reloj se agota en el fondo del arbol, la recursion se desenrolla
    // pasando por decenas de nodos, y cada uno llamaba a `evaluate()` -que con territorio
    // hace un Voronoi- para producir un numero que nadie va a mirar, porque la iteracion
    // incompleta se descarta entera. Medido con `tools/sonda_overshoot.cpp` sobre 10 000
    // estados con presupuesto de 5 ms: el p50 clavaba el deadline (3005 us de 3000) y el
    // maximo se iba a 10 984. El valor que se devuelve aqui da igual por ese mismo motivo.
    if (sin_presupuesto(ctx)) {
        return 0.0;
    }
    if (depth <= 0) {
        return evaluate(s, ctx.us, *ctx.params);
    }

    // [v16] Sonda de la tabla. Una entrada guardada con profundidad >= la pedida vale:
    // fue el resultado de un subarbol al menos tan profundo como el que tocaba mirar.
    const std::uint64_t key = ctx.tt != nullptr ? clave(s) : 0;
    Entrada* slot = nullptr;
    bool hay_mejor = false;
    Direction mejor_tt = Direction::up;
    if (ctx.tt != nullptr) {
        slot = &ctx.tt[key & ctx.tt_mask];
        if (slot->sello == ctx.sello && slot->key == key) {
            if (slot->con_mejor) {
                hay_mejor = true;
                mejor_tt = slot->mejor;
            }
            if (slot->depth >= static_cast<std::int16_t>(depth)) {
                ++ctx.tt_hits;
                if (slot->limite == Limite::exacto) {
                    return slot->valor;
                }
                if (slot->limite == Limite::inferior && slot->valor >= beta) {
                    return slot->valor;
                }
                if (slot->limite == Limite::superior && slot->valor <= alpha) {
                    return slot->valor;
                }
            }
        }
    }

    const double alpha_inicial = alpha;
    std::array<Direction, engine::direction_count> mios{};
    int n = ordered_moves(s, ctx.us, mios, ctx.params->search.order_version);
    // El mejor movimiento que la tabla ya conoce se prueba PRIMERO: es lo que hace que la
    // poda corte arriba en vez de abajo.
    if (hay_mejor) {
        for (int i = 1; i < n; ++i) {
            if (mios[static_cast<unsigned>(i)] == mejor_tt) {
                std::swap(mios[0], mios[static_cast<unsigned>(i)]);
                break;
            }
        }
    }
    double mejor = -std::numeric_limits<double>::infinity();
    Direction mejor_dir = mios[0];
    for (int i = 0; i < n; ++i) {
        const double v = peor_respuesta(s, mios[static_cast<unsigned>(i)], depth, alpha, beta, ctx);
        if (v > mejor) {
            mejor = v;
            mejor_dir = mios[static_cast<unsigned>(i)];
        }
        alpha = std::max(alpha, mejor);
        if (alpha >= beta || sin_presupuesto(ctx)) {
            break;
        }
    }
    // Un subarbol cortado por falta de presupuesto no se guarda: su valor no es el valor
    // de la posicion, es lo que dio tiempo a mirar.
    if (slot != nullptr && !ctx.agotado) {
        const Limite lim = mejor <= alpha_inicial ? Limite::superior
                           : mejor >= beta        ? Limite::inferior
                                                  : Limite::exacto;
        if (slot->sello != ctx.sello || slot->key != key ||
            slot->depth <= static_cast<std::int16_t>(depth)) {
            slot->key = key;
            slot->valor = mejor;
            slot->sello = ctx.sello;
            slot->depth = static_cast<std::int16_t>(depth);
            slot->limite = lim;
            slot->mejor = mejor_dir;
            slot->con_mejor = true;
        }
    }
    return mejor;
}

} // namespace

double evaluate(const State& s, SnakeId us, const Params& p) noexcept {
    const auto& yo = s.snake(us);
    if (!engine::is_alive(yo.status)) {
        return p.search.death_value;
    }
    const int mi_largo = static_cast<int>(yo.length);
    const Board blocked = blocked_cells(s);
    Board libres = Board::full().without(blocked);
    if (yo.tail_is_stacked()) {
        libres.set(yo.tail());
    }
    libres.set(yo.head());

    double score = 0.0;

    // 1. Espacio. Es el termino que mas pesa y el suelo de todo: una posicion sin sitio
    //    esta perdida aunque lo demas cuadre.
    //
    //    Con `territory.version >= 1` se mide como TERRITORIO -casillas que alcanzamos
    //    antes que los rivales, por BFS simultaneo- en vez de como espacio alcanzable a
    //    secas. Es exactamente la evaluacion que se midio y se rechazo como decision de un
    //    turno (ver docs/experimentos.md#s-v1r); aqui se prueba donde el propio rechazo dijo
    //    que deberia servir: en las HOJAS de una busqueda.
    //
    //    El espacio crudo se conserva SIEMPRE para la guarda de "no cabe ni mi cuerpo":
    //    esa condicion es sobre casillas fisicas, no sobre quien llega antes.
    const int espacio = eval::flood(libres, yo.head()).cells;
    if (p.territory.version >= 1) {
        const auto t = eval::voronoi(s, blocked, p.territory.hazard_value_pct);
        // [v13] En el duelo el territorio pesa mas: gana quien corta el tablero, no quien
        // come. Solo se cuenta si hay exactamente un rival vivo; con la version a 0 el
        // factor es 1 y la cuenta ni se hace. ver docs/strategy.md#s-territorio-duelo
        double factor = 1.0;
        if (p.duel.territory_version >= 1) {
            int rivales_vivos = 0;
            for (int i = 0; i < s.count(); ++i) {
                rivales_vivos += (i != static_cast<int>(us) &&
                                  engine::is_alive(s.snakes[static_cast<unsigned>(i)].status))
                                     ? 1
                                     : 0;
            }
            factor = rivales_vivos == 1 ? p.duel.territory_scale : 1.0;
        }
        score += factor * p.territory.weight *
                 static_cast<double>(t.weighted[static_cast<std::size_t>(us)]) /
                 static_cast<double>(State::cells * 100);
        score -= p.territory.contested_weight * static_cast<double>(t.contested) /
                 static_cast<double>(State::cells);
    } else {
        score += p.space.weight * static_cast<double>(espacio) / static_cast<double>(State::cells);
    }
    if (espacio < mi_largo) {
        score -= p.space.weight;
    }

    // 1a. [v15] Supervivencia en vez de superficie, solo con un rival vivo.
    //
    //     El Voronoi premia LLEGAR antes; esto mide poder QUEDARSE. Dos preguntas que el
    //     conteo de casillas no responde:
    //       - si nuestra cola cae dentro de la region alcanzable, se puede girar detras de
    //         ella indefinidamente (el tail-chasing de los finales), y la region deja de
    //         tener fondo;
    //       - si las dos regiones ya no se tocan, el duelo son dos solitarios y gana quien
    //         aguante mas turnos: eso es una cuenta, no una heuristica.
    //     ver docs/strategy.md#s-supervivencia-duelo
    if (p.duel.survival_version >= 1) {
        int rival = -1;
        int vivos_rival = 0;
        for (int i = 0; i < s.count(); ++i) {
            if (i != static_cast<int>(us) &&
                engine::is_alive(s.snakes[static_cast<unsigned>(i)].status)) {
                ++vivos_rival;
                rival = i;
            }
        }
        if (vivos_rival == 1) {
            const auto& otro = s.snakes[static_cast<unsigned>(rival)];
            Board libres_rival = Board::full().without(blocked);
            if (otro.tail_is_stacked()) {
                libres_rival.set(otro.tail());
            }
            libres_rival.set(otro.head());
            const Board mia = eval::region(libres, yo.head());
            const Board suya = eval::region(libres_rival, otro.head());
            const bool cola_dentro = mia.test(yo.tail());
            if ((mia & suya).none()) {
                // Regiones separadas: el resultado ya no depende de lo que haga el rival.
                // Turnos que aguanta cada una: con la cola dentro se vive de la salud -y de
                // la comida que haya en la region-; sin ella, de las casillas que quedan.
                const auto aguanta = [&](const Board& r, const State::Snake& sn) {
                    const int comida = (r & s.food).count();
                    const int por_salud = static_cast<int>(sn.health) + comida * 100;
                    return r.test(sn.tail()) ? por_salud : std::min(r.count(), por_salud);
                };
                const int mios = aguanta(mia, yo);
                const int suyos = aguanta(suya, otro);
                score += p.duel.survival_weight * static_cast<double>(mios - suyos) /
                         static_cast<double>(State::cells);
            } else if (cola_dentro) {
                score += p.duel.tail_loop_weight;
            }
        }
    }

    // 1b. [v14] Trampa umbralada, solo en el duelo. El flood fill de arriba ve el hueco de
    //     AHORA; esto ve la sala con una sola puerta, que es como se muere encerrado 15
    //     turnos despues de entrar. Se paga solo donde puede decidir algo: un rival vivo y
    //     una region que ya viene justa. En tablero abierto no se calcula nada.
    //     ver docs/strategy.md#s-trampa-duelo
    if (p.duel.trap_version >= 1 && mi_largo > 0 &&
        static_cast<double>(espacio) < p.duel.trap_trigger_ratio * static_cast<double>(mi_largo)) {
        int rivales_vivos = 0;
        for (int i = 0; i < s.count(); ++i) {
            rivales_vivos += (i != static_cast<int>(us) &&
                              engine::is_alive(s.snakes[static_cast<unsigned>(i)].status))
                                 ? 1
                                 : 0;
        }
        if (rivales_vivos == 1) {
            const int peor = eval::worst_case_space(libres, yo.head(), p.duel.trap_max_cuellos);
            if (peor < mi_largo) {
                score -= p.duel.trap_weight * static_cast<double>(mi_largo - peor) /
                         static_cast<double>(mi_largo);
            }
        }
    }

    // 2. Longitud relativa y rivales vivos. Menos rivales vivos es mejor puesto, y el
    //    puesto es lo unico que puntua el torneo.
    int vivos = 0;
    int mas_largos = 0;
    int largo_rival_max = 0;
    // Distancia a la cabeza de cada rival vivo y si es mas corto que nosotros. Se guarda
    // para poder decidir DESPUES del bucle si esto es un duelo -que depende de `vivos`, y
    // `vivos` no esta completo hasta el final- sin recorrer las serpientes dos veces. Son
    // como mucho `max_snakes - 1` pares en la pila: ni una asignacion.
    std::array<std::pair<int, bool>, static_cast<std::size_t>(State::max_snakes)> rivales{};
    for (int i = 0; i < s.count(); ++i) {
        if (i == static_cast<int>(us)) {
            continue;
        }
        const auto& otro = s.snakes[static_cast<unsigned>(i)];
        if (!engine::is_alive(otro.status)) {
            continue;
        }
        largo_rival_max = std::max(largo_rival_max, static_cast<int>(otro.length));
        if (static_cast<int>(otro.length) >= mi_largo) {
            ++mas_largos;
        }
        rivales[static_cast<std::size_t>(vivos)] = {manhattan(s, yo.head(), otro.head()),
                                                    static_cast<int>(otro.length) < mi_largo};
        ++vivos;
    }

    // Final de dos: con un solo rival vivo el juego es de suma cero y la ventaja de
    // longitud deja de ser algo que acumular para ser algo que cobrar.
    // ver docs/strategy.md#s-duelo
    const bool duelo = p.duel.version >= 1 && vivos == 1;
    for (int k = 0; k < vivos; ++k) {
        const int distancia = rivales[static_cast<std::size_t>(k)].first;
        const bool mas_corto = rivales[static_cast<std::size_t>(k)].second;
        // Zona de cabeza: quedar al lado de una igual o mas larga es perder el duelo.
        if (distancia <= 1) {
            const double premio = duelo ? p.duel.prefer_shorter : p.head.prefer_shorter;
            score -= mas_corto ? -premio : p.head.avoid_equal_or_longer;
        }
        // Presion: la zona de cabeza solo puntua pegados, asi que sin un gradiente no hay
        // nada que empuje hacia el rival desde lejos. Solo siendo estrictamente mas
        // largos, que es cuando el cabezazo lo ganamos por regla.
        if (duelo && mas_corto && p.duel.pressure_weight > 0.0) {
            constexpr double kMaxManhattan =
                static_cast<double>(State::width - 1) + static_cast<double>(State::height - 1);
            const double d = std::min(static_cast<double>(distancia), kMaxManhattan);
            score += p.duel.pressure_weight * (1.0 - d / kMaxManhattan);
        }
    }
    score -= p.head.avoid_equal_or_longer * 0.25 * mas_largos;
    score -= 40.0 * vivos;

    // 3. Salud, medida en TURNOS DE VIDA y no en puntos.
    //
    // En Royale el hazard cuesta `hazardDamagePerTurn` MAS el -1 de cada turno: con 14 de
    // daño son 15 de vida por turno, o sea que dentro del hazard se muere en ~7 turnos
    // desde salud llena. Un umbral en salud absoluta -"busca comida por debajo de 50"-
    // vale en tablero limpio y miente dentro del hazard, donde 50 son tres turnos y pico.
    //
    // Medido sobre las 60 partidas de v5: 27 de 34 muertes fueron hambre o hazard con poca
    // vida, y de los 21 segundos puestos, 11 hazard y 10 hambre. Casi todo lo que perdemos
    // por poco lo perdemos por quedarnos sin vida.
    // ver docs/experimentos.md#s-supervivencia
    const auto salud = static_cast<double>(yo.health);
    const int coste_turno =
        1 + (s.hazards.test(yo.head()) ? std::max(1, s.rules.hazard_damage_per_turn) : 0);
    const double turnos_vida = salud / static_cast<double>(coste_turno);

    if (p.survival.version >= 1) {
        // Valor del margen, saturado: por encima de `safe_turns` tener mas vida no cambia
        // ninguna decision. Dentro del hazard el coste por turno hunde este numero solo,
        // sin necesidad de un termino aparte.
        const double margen = std::min(turnos_vida, static_cast<double>(p.survival.safe_turns));
        score += p.survival.weight * margen / static_cast<double>(p.survival.safe_turns);
        // Y el castigo por estar al borde, CONTINUO. El de v5 era un escalon que solo se
        // activaba con dos turnos de vida, cuando ya no da tiempo ni a salir del hazard.
        if (turnos_vida < static_cast<double>(p.survival.critical_turns)) {
            score -= p.survival.panic_weight *
                     (static_cast<double>(p.survival.critical_turns) - turnos_vida) /
                     static_cast<double>(p.survival.critical_turns);
        }
    } else {
        score += p.food.weight * salud / 100.0;
    }
    // [v5] La ventaja de longitud, y la comida como medio para conseguirla.
    //
    // v0 solo buscaba comida con hambre -"no comer por comer"-, politica razonable para
    // una snake que decide un turno y ciega para una que ve diez: el arbol ya calcula si
    // ir a por esa comida te mete en un callejon, asi que crecer deja de ser un riesgo a
    // ojo y pasa a ser algo evaluable. Medido sobre las 60 partidas de v4: moriamos siendo
    // iguales o mas cortos que TODOS los vivos en 47, y el puesto medio caia monotonamente
    // con la desventaja de longitud. ver docs/experimentos.md#s-longitud
    const int ventaja = vivos > 0 ? mi_largo - largo_rival_max : p.length.target_lead;
    // [v12] En el duelo manda otra politica: ir por delante es casi todo, porque el mas
    // largo gana cualquier cabezazo y la busqueda paranoica lo da por hecho.
    // ver docs/strategy.md#s-longitud-duelo
    const bool longitud_duelo = p.duel.length_version >= 1 && vivos == 1;
    const bool corto =
        longitud_duelo ? ventaja < 1 : p.length.version >= 1 && ventaja < p.length.target_lead;
    if (longitud_duelo) {
        const double v = std::clamp(static_cast<double>(ventaja), -6.0, 1.0);
        score += p.duel.length_weight * v;
    } else if (p.length.version >= 1) {
        // Satura en `target_lead`: lo que decide un cabezazo es ir por delante, y a partir
        // de cierta ventaja el cuerpo de mas estorba mas de lo que aporta.
        const double v = std::clamp(static_cast<double>(ventaja),
                                    -static_cast<double>(p.length.target_lead) * 2.0,
                                    static_cast<double>(p.length.target_lead));
        score += p.length.advantage_weight * v / static_cast<double>(p.length.target_lead);
    }

    // El disparador de la comida: turnos de vida si v6 esta encendida, salud cruda si no.
    const bool con_prisa = p.survival.version >= 1
                               ? turnos_vida <= static_cast<double>(p.survival.seek_below_turns)
                               : yo.health <= p.food.seek_below;
    if (con_prisa || corto) {
        const int d = eval::flood(libres, yo.head()).cells > 0 ? [&] {
            Board frente;
            frente.set(yo.head());
            Board visto = frente;
            for (int t = 1; t <= State::cells; ++t) {
                const Board sig = (frente.expand() & libres).without(visto);
                if (sig.none()) {
                    return -1;
                }
                if ((sig & s.food).any()) {
                    return t;
                }
                visto |= sig;
                frente = sig;
            }
            return -1;
        }()
                                                               : -1;
        // Con hambre manda la salud; yendo cortos manda la ventaja. El peso de cada
        // motivo es distinto y se declara por separado.
        const double peso = yo.health <= p.food.seek_below ? p.food.weight * 2.0
                            : longitud_duelo               ? p.duel.hunt_weight
                                                           : p.length.hunt_weight;
        score += d >= 0 ? peso * (1.0 - static_cast<double>(d) / static_cast<double>(State::cells))
                        : (yo.health <= p.food.seek_below ? -p.food.weight * 2.0 : 0.0);
    }

    // 4. Hazards. ver docs/rules.md#r-06
    if (s.hazards.test(yo.head())) {
        if (p.survival.version >= 1) {
            // Penalizacion BASE por estar dentro. Lo urgente ya lo dice `turnos_vida`, que
            // se calcula con el coste del hazard: aqui solo queda el "estar ahi es peor".
            score -= p.hazard.weight;
        } else {
            const int dano = std::max(1, s.rules.hazard_damage_per_turn);
            const int turnos = static_cast<int>(yo.health) / dano;
            score -= p.hazard.weight * (turnos <= 2 ? p.hazard.low_health_multiplier : 1.0);
        }
    }

    // La longitud ABSOLUTA con peso simbolico es la de v0. Con `length.version >= 1` lo
    // que puntua es la ventaja, de arriba: en un juego de cuatro, ser largo no sirve de
    // nada si el de al lado es mas largo.
    if (p.length.version == 0) {
        score += static_cast<double>(mi_largo) * 2.0;
    }
    return score;
}

SearchResult search(const State& state, Deadline deadline, const Params& params) noexcept {
    SearchResult out;
    const auto& yo = state.snake(state.you);

    // La busqueda corre contra un deadline PROPIO, unos milisegundos antes del real: lo
    // que se reserva es el coste de darse cuenta de que se acabo y volver.
    const Deadline interno(deadline.end() -
                           std::chrono::microseconds(std::max(0, params.search.reserve_us)));
    Contexto ctx;
    ctx.params = &params;
    ctx.deadline = interno;
    ctx.us = state.you;
    ctx.budget_nodes = params.search.budget_nodes;
    if (params.search.tt_version >= 1) {
        auto& tabla = tabla_de(params.search.tt_bits);
        ctx.tt = tabla.data();
        ctx.tt_mask = tabla.size() - 1;
        // Un sello por busqueda invalida lo de la jugada anterior sin borrar 2 MB: el
        // tablero cambio y un valor de hace un turno ya no describe esta posicion.
        static thread_local std::uint32_t sello_global = 0;
        ctx.sello = ++sello_global;
    }

    // Los rivales se simulan por cercania: el que esta a dos casillas decide si vivimos,
    // el que esta al otro lado del tablero no. Cada uno simulado multiplica por ~3 el
    // arbol, asi que este recorte es lo que compra profundidad.
    std::array<std::pair<int, SnakeId>, k_max_snakes> cercanos{};
    int n = 0;
    for (int i = 0; i < state.count(); ++i) {
        if (i == static_cast<int>(state.you)) {
            continue;
        }
        const auto& otro = state.snakes[static_cast<unsigned>(i)];
        if (!engine::is_alive(otro.status)) {
            continue;
        }
        cercanos[static_cast<unsigned>(n++)] = {manhattan(state, yo.head(), otro.head()),
                                                static_cast<SnakeId>(i)};
    }
    // Los rivales al reves: primero el mas CERCANO, que es el que decide si vivimos.
    for (int i = 0; i < n; ++i) {
        cercanos[static_cast<unsigned>(i)].first = -cercanos[static_cast<unsigned>(i)].first;
    }
    ordena_desc(cercanos.data(), n);
    ctx.n_rivales = std::min(n, std::max(0, params.search.max_rivals));
    for (int i = 0; i < ctx.n_rivales; ++i) {
        ctx.rivales[static_cast<unsigned>(i)] = cercanos[static_cast<unsigned>(i)].second;
    }
    out.rivals_simulated = ctx.n_rivales;

    std::array<Direction, engine::direction_count> mios{};
    const int n_mios = ordered_moves(state, state.you, mios, params.search.order_version);
    out.best = mios[0]; // el mejor por ordenacion estatica, por si no da tiempo a nada

    for (int depth = 1; depth <= params.search.max_depth; ++depth) {
        // Se pregunta por la via unica y no a `interno.expired()` directamente: asi el
        // corte del reloj queda marcado tambien cuando ocurre entre profundidades.
        if (sin_presupuesto(ctx)) {
            break;
        }
        double alpha = -std::numeric_limits<double>::infinity();
        const double beta = std::numeric_limits<double>::infinity();
        Direction mejor_dir = mios[0];
        double mejor_val = -std::numeric_limits<double>::infinity();
        bool completa = true;

        for (int i = 0; i < n_mios; ++i) {
            const double v =
                peor_respuesta(state, mios[static_cast<unsigned>(i)], depth, alpha, beta, ctx);
            if (ctx.agotado) {
                completa = false;
                break;
            }
            if (v > mejor_val) {
                mejor_val = v;
                mejor_dir = mios[static_cast<unsigned>(i)];
            }
            alpha = std::max(alpha, mejor_val);
        }

        // Una profundidad a medias no sustituye a la anterior: sus hermanos no se
        // compararon con las mismas reglas. Se devuelve la ultima COMPLETADA.
        if (!completa) {
            break;
        }
        if (depth == 1 || mejor_dir != out.best) {
            out.last_change_depth = depth;
        }
        out.best = mejor_dir;
        out.score = mejor_val;
        out.depth = depth;
        if (ctx.agotado) {
            break;
        }
    }
    out.nodes = ctx.nodes;
    out.tt_hits = ctx.tt_hits;
    out.corto_el_reloj = ctx.corto_el_reloj;
    return out;
}

} // namespace snake
