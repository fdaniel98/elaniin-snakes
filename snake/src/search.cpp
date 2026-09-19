/// @file search.cpp
/// Busqueda paranoica con profundizacion iterativa. ver snake/include/snake/search.hpp

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

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

/// `true` cuando toca abandonar.
///
/// El reloj se mira en CADA nodo, y esto empezo mirandolo cada 256 -luego 32, luego 8-
/// para "no gastar tiempo mirando la hora". Medido: con la comprobacion en cada nodo la
/// profundidad media sale identica hasta la centesima (11.97 y 10.89 en la sonda, los
/// mismos numeros que con 32), y en cambio saltarse nodos costaba violaciones del deadline
/// en cuanto la hoja se encarecio con el territorio: 12 de 10 000 con 32, 2 con 8, 0 con 1.
/// `Clock::now()` son ~25 ns contra el microsegundo largo que cuesta un nodo. Era una
/// optimizacion prematura que no compraba nada y pagaba en correccion.
bool sin_tiempo(Contexto& ctx) noexcept {
    if (ctx.agotado) {
        return true;
    }
    if (ctx.deadline.expired()) {
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

    // El reloj se mira ANTES de ordenar los movimientos de los rivales. Ordenar hace un
    // flood fill por direccion y por rival, y es trabajo que no estaba acotado por nada:
    // con presupuestos muy cortos -el fuzz usa 5 ms- esa preparacion sola podia pasarse.
    if (sin_tiempo(ctx)) {
        return ctx.params->search.death_value;
    }

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
    // Sin tiempo NO se evalua. Esto parece un detalle y era el defecto que dominaba el
    // tail: cuando el reloj se agota en el fondo del arbol, la recursion se desenrolla
    // pasando por decenas de nodos, y cada uno llamaba a `evaluate()` -que con territorio
    // hace un Voronoi- para producir un numero que nadie va a mirar, porque la iteracion
    // incompleta se descarta entera. Medido con `tools/sonda_overshoot.cpp` sobre 10 000
    // estados con presupuesto de 5 ms: el p50 clavaba el deadline (3005 us de 3000) y el
    // maximo se iba a 10 984. El valor que se devuelve aqui da igual por ese mismo motivo.
    if (sin_tiempo(ctx)) {
        return 0.0;
    }
    if (depth <= 0) {
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
        score += p.territory.weight *
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

    // 2. Longitud relativa y rivales vivos. Menos rivales vivos es mejor puesto, y el
    //    puesto es lo unico que puntua el torneo.
    int vivos = 0;
    int mas_largos = 0;
    int largo_rival_max = 0;
    for (int i = 0; i < static_cast<int>(s.snake_count); ++i) {
        if (i == static_cast<int>(us)) {
            continue;
        }
        const auto& otro = s.snakes[static_cast<unsigned>(i)];
        if (!engine::is_alive(otro.status)) {
            continue;
        }
        ++vivos;
        largo_rival_max = std::max(largo_rival_max, static_cast<int>(otro.length));
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
    // [v5] La ventaja de longitud, y la comida como medio para conseguirla.
    //
    // v0 solo buscaba comida con hambre -"no comer por comer"-, politica razonable para
    // una snake que decide un turno y ciega para una que ve diez: el arbol ya calcula si
    // ir a por esa comida te mete en un callejon, asi que crecer deja de ser un riesgo a
    // ojo y pasa a ser algo evaluable. Medido sobre las 60 partidas de v4: moriamos siendo
    // iguales o mas cortos que TODOS los vivos en 47, y el puesto medio caia monotonamente
    // con la desventaja de longitud. ver docs/experimentos.md#s-longitud
    const int ventaja = vivos > 0 ? mi_largo - largo_rival_max : p.length.target_lead;
    const bool corto = p.length.version >= 1 && ventaja < p.length.target_lead;
    if (p.length.version >= 1) {
        // Satura en `target_lead`: lo que decide un cabezazo es ir por delante, y a partir
        // de cierta ventaja el cuerpo de mas estorba mas de lo que aporta.
        const double v = std::clamp(static_cast<double>(ventaja),
                                    -static_cast<double>(p.length.target_lead) * 2.0,
                                    static_cast<double>(p.length.target_lead));
        score += p.length.advantage_weight * v / static_cast<double>(p.length.target_lead);
    }

    if (yo.health <= p.food.seek_below || corto) {
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
        const double peso =
            yo.health <= p.food.seek_below ? p.food.weight * 2.0 : p.length.hunt_weight;
        score += d >= 0 ? peso * (1.0 - static_cast<double>(d) / static_cast<double>(State::cells))
                        : (yo.health <= p.food.seek_below ? -p.food.weight * 2.0 : 0.0);
    }

    // 4. Hazards. ver docs/rules.md#r-06
    if (s.hazards.test(yo.head())) {
        const int dano = std::max(1, s.rules.hazard_damage_per_turn);
        const int turnos = static_cast<int>(yo.health) / dano;
        score -= p.hazard.weight * (turnos <= 2 ? p.hazard.low_health_multiplier : 1.0);
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
    return out;
}

} // namespace snake
