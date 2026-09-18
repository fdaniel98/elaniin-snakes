/// @file search.cpp
/// Busqueda paranoica con profundizacion iterativa. ver snake/include/snake/search.hpp

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

#include <engine/rules.hpp>

#include <snake/eval/floodfill.hpp>
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
    for (int i = 0; i < static_cast<int>(s.snake_count); ++i) {
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
                  std::array<Direction, engine::direction_count>& out) noexcept {
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
        Board alcanzable = free_cells;
        const int celda = Board::index_of(next);
        alcanzable.set(celda);
        con_peso[static_cast<unsigned>(n)] = {eval::flood(alcanzable, celda).cells, dir};
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

struct Contexto {
    const Params* params{nullptr};
    Deadline deadline{Deadline::Clock::now()};
    SnakeId us{0};
    long long nodes{0};
    bool agotado{false};
    std::array<SnakeId, k_max_snakes> rivales{};
    int n_rivales{0};
};

/// Nodos entre dos lecturas del reloj. `Clock::now()` no es gratis y mirarlo en cada nodo
/// se lleva un pedazo del presupuesto; mirarlo cada 256 se paso 34 us del deadline en la
/// posicion de spawn, porque entre dos lecturas caben muchos `apply`. 32 es el punto en
/// que la sonda deja de pasarse y el coste sigue siendo ruido.
/// ver docs/decisions/ADR-0022-busqueda-paranoica.md
constexpr long long k_nodos_por_reloj = 32;

/// `true` cuando toca abandonar.
bool sin_tiempo(Contexto& ctx) noexcept {
    if (ctx.agotado) {
        return true;
    }
    if ((ctx.nodes % k_nodos_por_reloj) == 0 && ctx.deadline.expired()) {
        ctx.agotado = true;
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
    for (int i = 0; i < static_cast<int>(s.snake_count); ++i) {
        // Los rivales que no se simulan repiten lo que el motor oficial les aplicaria si
        // no contestaran. No es que vayan a hacer eso: es la suposicion mas barata que no
        // los deja quietos, que no es un estado que el juego produzca.
        // ver docs/rules.md#r-03
        moves[static_cast<unsigned>(i)] = engine::default_move(s, static_cast<SnakeId>(i));
    }
    moves[static_cast<unsigned>(ctx.us)] = nuestro;

    // Producto cartesiano de las direcciones de los rivales simulados, iterativo para no
    // recursar sobre el numero de rivales.
    std::array<std::array<Direction, engine::direction_count>, k_max_snakes> opciones{};
    std::array<int, k_max_snakes> cuantas{};
    long long combinaciones = 1;
    for (int r = 0; r < ctx.n_rivales; ++r) {
        const SnakeId id = ctx.rivales[static_cast<unsigned>(r)];
        cuantas[static_cast<unsigned>(r)] =
            ordered_moves(s, id, opciones[static_cast<unsigned>(r)]);
        combinaciones *= cuantas[static_cast<unsigned>(r)];
    }

    double peor = std::numeric_limits<double>::infinity();
    for (long long c = 0; c < combinaciones; ++c) {
        if (sin_tiempo(ctx)) {
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
        engine::apply(siguiente, std::span<const Direction>(moves.data(), s.snake_count));
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
    if (depth <= 0 || sin_tiempo(ctx)) {
        return evaluate(s, ctx.us, *ctx.params);
    }

    std::array<Direction, engine::direction_count> mios{};
    const int n = ordered_moves(s, ctx.us, mios);
    double mejor = -std::numeric_limits<double>::infinity();
    for (int i = 0; i < n; ++i) {
        const double v = peor_respuesta(s, mios[static_cast<unsigned>(i)], depth, alpha, beta, ctx);
        mejor = std::max(mejor, v);
        alpha = std::max(alpha, mejor);
        if (alpha >= beta || sin_tiempo(ctx)) {
            break;
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

    // 1. Espacio alcanzable, normalizado. Es el termino que mas pesa en v0 y aqui sigue
    //    siendo el suelo: una posicion sin sitio esta perdida aunque todo lo demas cuadre.
    const int espacio = eval::flood(libres, yo.head()).cells;
    score += p.space.weight * static_cast<double>(espacio) / static_cast<double>(State::cells);
    if (espacio < mi_largo) {
        score -= p.space.weight;
    }

    // 2. Longitud relativa y rivales vivos. Menos rivales vivos es mejor puesto, y el
    //    puesto es lo unico que puntua el torneo.
    int vivos = 0;
    int mas_largos = 0;
    for (int i = 0; i < static_cast<int>(s.snake_count); ++i) {
        if (i == static_cast<int>(us)) {
            continue;
        }
        const auto& otro = s.snakes[static_cast<unsigned>(i)];
        if (!engine::is_alive(otro.status)) {
            continue;
        }
        ++vivos;
        if (static_cast<int>(otro.length) >= mi_largo) {
            ++mas_largos;
        }
        // Zona de cabeza: quedar al lado de una igual o mas larga es perder el duelo.
        if (manhattan(s, yo.head(), otro.head()) <= 1) {
            score -= static_cast<int>(otro.length) >= mi_largo ? p.head.avoid_equal_or_longer
                                                               : -p.head.prefer_shorter;
        }
    }
    score -= p.head.avoid_equal_or_longer * 0.25 * mas_largos;
    score -= 40.0 * vivos;

    // 3. Salud y comida. La busqueda ve el hambre venir muchos turnos antes que v0, asi
    //    que aqui basta con que la salud valga algo y que valga mas cuanto mas escasa.
    const auto salud = static_cast<double>(yo.health);
    score += p.food.weight * salud / 100.0;
    if (yo.health <= p.food.seek_below) {
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
        score += d >= 0 ? p.food.weight * 2.0 *
                              (1.0 - static_cast<double>(d) / static_cast<double>(State::cells))
                        : -p.food.weight * 2.0;
    }

    // 4. Hazards. ver docs/rules.md#r-06
    if (s.hazards.test(yo.head())) {
        const int dano = std::max(1, s.rules.hazard_damage_per_turn);
        const int turnos = static_cast<int>(yo.health) / dano;
        score -= p.hazard.weight * (turnos <= 2 ? p.hazard.low_health_multiplier : 1.0);
    }

    score += static_cast<double>(mi_largo) * 2.0;
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

    // Los rivales se simulan por cercania: el que esta a dos casillas decide si vivimos,
    // el que esta al otro lado del tablero no. Cada uno simulado multiplica por ~3 el
    // arbol, asi que este recorte es lo que compra profundidad.
    std::array<std::pair<int, SnakeId>, k_max_snakes> cercanos{};
    int n = 0;
    for (int i = 0; i < static_cast<int>(state.snake_count); ++i) {
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
    const int n_mios = ordered_moves(state, state.you, mios);
    out.best = mios[0]; // el mejor por ordenacion estatica, por si no da tiempo a nada

    for (int depth = 1; depth <= params.search.max_depth; ++depth) {
        if (ctx.agotado || interno.expired()) {
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
        out.best = mejor_dir;
        out.score = mejor_val;
        out.depth = depth;
        if (ctx.agotado) {
            break;
        }
    }
    out.nodes = ctx.nodes;
    return out;
}

} // namespace snake
