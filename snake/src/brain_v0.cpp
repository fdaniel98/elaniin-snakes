/// @file brain_v0.cpp
/// Baseline COMPLETO y jugable. No es un stub: es la referencia fija contra la que se
/// mide toda version posterior, y no se borra nunca. ver docs/strategy.md#s-v0
///
/// Cada punto de la especificacion del baseline tiene su fixture en tests/fixtures.

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <exception>

#include <engine/rules.hpp>

#include <snake/brain.hpp>
#include <snake/eval/floodfill.hpp>
#include <snake/eval/voronoi.hpp>
#include <snake/search.hpp>

namespace snake {

namespace {

using State = engine::State11;
using Board = State::Board;
using engine::Coord;
using engine::Direction;

constexpr int direction_count = engine::direction_count;

/// Casillas que seguiran ocupadas el proximo turno.
///
/// La cola de una serpiente cuenta como libre si y solo si, tras su movimiento,
/// ningun segmento suyo la ocupa; eso se deriva de los segmentos duplicados del ring
/// buffer, nunca de un flag "comio el turno anterior": en el spawn los tres segmentos
/// estan apilados y en constrictor la cola no avanza jamas.
/// ver docs/rules.md#r-04
///
/// La condicion `!tail_is_stacked()` es REDUNDANTE y se deja por ser explicita: cuando
/// la cola esta apilada, el segmento `length - 2` ocupa esa misma casilla y ya la marca.
/// Lo descubrio la prueba de mutantes: quitarla no cambia ningun resultado, asi que no
/// es un mutante vivo sino un mutante equivalente. Quien la borre no rompe nada hoy,
/// pero deja el codigo dependiendo de que el ring buffer duplique el ultimo segmento.
Board blocked_cells(const State& state) noexcept {
    Board blocked;
    for (int i = 0; i < state.count(); ++i) {
        const auto& other = state.snakes[static_cast<unsigned>(i)];
        if (!engine::is_alive(other.status)) {
            continue;
        }
        const int last = static_cast<int>(other.length) - 1;
        for (int seg = 0; seg <= last; ++seg) {
            if (seg == last && !other.tail_is_stacked()) {
                continue;
            }
            blocked.set(other.segment(seg));
        }
    }
    return blocked;
}

/// Distancia en turnos a la comida mas cercana alcanzable, o -1.
int nearest_food_distance(const Board& free_cells,
                          int start,
                          const Board& food,
                          int max_turns) noexcept {
    if (start < 0) {
        return -1;
    }
    if (food.test(start)) {
        return 0;
    }

    Board frontier;
    frontier.set(start);
    Board seen = frontier;
    for (int turn = 1; turn <= max_turns; ++turn) {
        const Board next = (frontier.expand() & free_cells).without(seen);
        if (next.none()) {
            return -1;
        }
        if ((next & food).any()) {
            return turn;
        }
        seen |= next;
        frontier = next;
    }
    return -1;
}

/// Casillas a las que puede llegar la cabeza de `other` el proximo turno.
/// ver docs/rules.md#r-08
Board head_zone(const State& state, int snake_index) noexcept {
    Board head;
    head.set(state.snakes[static_cast<unsigned>(snake_index)].head());
    return head.expand().without(head);
}

struct Candidate {
    Direction direction{Direction::up};
    int cell{-1};
    bool safe{false};
    int space{0};
    double score{0.0};
    /// Territorio propio tras el movimiento, en centesimas de casilla (v1).
    int territory{0};
    /// Casillas que nadie gana porque dos llegan a la vez con la misma longitud (v1).
    int contested{0};
    /// Espacio que quedaria si se cerrase el peor cuello de la region alcanzable.
    int worst_space{0};
};

/// Puntuacion de un movimiento ya filtrado. Todos los pesos salen del config:
/// ninguna constante magica vive aqui.
double score_candidate(const State& state,
                       const Params& params,
                       const Candidate& candidate,
                       const Board& free_cells,
                       bool degraded,
                       bool deep) noexcept {
    const auto& me = state.snake(state.you);
    const auto my_length = static_cast<int>(me.length);
    double score = 0.0;

    // 1. Espacio.
    //
    // v0 cuenta el espacio que EXISTE desde la casilla candidata. v1 cuenta el que se
    // alcanza ANTES que los rivales: un pasillo que el rival sella primero nunca fue
    // nuestro. El cambio sale de que 132 de 178 muertes del torneo no tenian ninguna
    // salida ese turno -la trampa se tiende antes-. ver docs/strategy.md#s-v1
    //
    // v0 se conserva entero y seleccionable con `territory.version = 0`: es la referencia
    // fija contra la que se mide todo lo demas y no se borra nunca.
    if (deep && params.territory.version >= 1 && !degraded) {
        score += params.territory.weight * static_cast<double>(candidate.territory) /
                 static_cast<double>(State::cells * 100);
        score -= params.territory.contested_weight * static_cast<double>(candidate.contested) /
                 static_cast<double>(State::cells);
    } else {
        score += params.space.weight * static_cast<double>(candidate.space) /
                 static_cast<double>(State::cells);
    }

    // 2. Zona de cabeza: se evitan las casillas adyacentes a cabezas iguales o mas
    //    largas y se prefieren las adyacentes a cabezas estrictamente mas cortas.
    for (int i = 0; i < state.count(); ++i) {
        if (i == static_cast<int>(state.you)) {
            continue;
        }
        const auto& other = state.snakes[static_cast<unsigned>(i)];
        if (!engine::is_alive(other.status)) {
            continue;
        }
        if (!head_zone(state, i).test(candidate.cell)) {
            continue;
        }

        if (static_cast<int>(other.length) >= my_length) {
            score -= params.head.avoid_equal_or_longer;
        } else {
            score += params.head.prefer_shorter;
        }
    }

    // Salas con una sola puerta. La penalizacion es proporcional a lo que se PIERDE en el
    // peor caso, asi que un hueco abierto no paga nada y un callejon paga todo.
    if (deep && params.space.worst_case_weight > 0.0 && !degraded) {
        const auto perdido = static_cast<double>(candidate.space - candidate.worst_space);
        score -= params.space.worst_case_weight * perdido / static_cast<double>(State::cells);
        // Y si el peor caso no da ni para el propio cuerpo, es una tumba con puerta.
        if (candidate.worst_space < my_length) {
            score -= params.space.worst_case_weight;
        }
    }

    if (degraded) {
        return score;
    }

    // 3. Hazards: terminar el turno dentro cuesta salud extra. La penalizacion sube
    //    cuando la salud no cubre varios turnos de daño. ver docs/rules.md#r-06
    if (state.hazards.test(candidate.cell)) {
        const int damage = std::max(1, state.rules.hazard_damage_per_turn);
        const int turns_of_health = static_cast<int>(me.health) / damage;
        const double urgency = turns_of_health <= 2 ? params.hazard.low_health_multiplier : 1.0;
        score -= params.hazard.weight * urgency;
    }

    // 4. Comida: solo si la salud baja del umbral, o si esta practicamente gratis.
    //    No se come por comer. ver docs/rules.md#r-07
    const bool in_hazard = state.hazards.test(me.head());
    const int threshold = in_hazard ? params.food.seek_below_in_hazard : params.food.seek_below;
    const int distance =
        nearest_food_distance(free_cells, candidate.cell, state.food, State::cells);
    if (distance >= 0) {
        const bool hungry = static_cast<int>(me.health) <= threshold;
        const bool free_food = distance <= params.food.free_food_distance;
        if (hungry || free_food) {
            const double closeness =
                1.0 - static_cast<double>(distance) / static_cast<double>(State::cells);
            score += params.food.weight * closeness * (hungry ? 2.0 : 1.0);
        }
    } else if (static_cast<int>(me.health) <= threshold) {
        // Sin comida alcanzable y con hambre: el movimiento no resuelve el problema.
        score -= params.food.weight;
    }

    return score;
}

Move decide_impl(const State& state,
                 Deadline deadline,
                 const Params& params,
                 bool degraded) noexcept {
    // Escalon 3: estado imposible de razonar (serpiente propia ausente o de longitud
    // cero). Se devuelve el movimiento determinista documentado, nunca una excepcion.
    // ver docs/rules.md#r-03
    if (static_cast<int>(state.you) >= state.count() || state.snake(state.you).length == 0) {
        return Move{Direction::up, 3, 0.0, 0};
    }

    const auto& me = state.snake(state.you);
    const auto my_length = static_cast<int>(me.length);
    const Coord head = Board::coord_of(me.head());

    const Board blocked = blocked_cells(state);
    Board free_cells = Board::full().without(blocked);

    // Tail-escape: la cola propia cuenta como libre si es alcanzable en al menos los
    // turnos que tarda en liberarse. Desactivado en modo degradado.
    // ver docs/rules.md#r-04
    if (!degraded && params.space.tail_escape && me.tail_is_stacked()) {
        free_cells.set(me.tail());
    }

    std::array<Candidate, static_cast<std::size_t>(direction_count)> candidates{};
    int safe_count = 0;
    for (int d = 0; d < direction_count; ++d) {
        auto& candidate = candidates[static_cast<unsigned>(d)];
        candidate.direction = static_cast<Direction>(d);
        const Coord next = step(head, candidate.direction);
        if (!Board::in_bounds(next)) {
            continue;
        }
        candidate.cell = Board::index_of(next);
        if (blocked.test(candidate.cell)) {
            continue;
        }
        candidate.safe = true;
        ++safe_count;
    }

    // Escalon 2 del fail-safe: ningun movimiento seguro. Se elige el primero que al
    // menos siga dentro del tablero; si no hay ninguno, escalon 3.
    // ver docs/rules.md#r-03
    if (safe_count == 0) {
        for (const auto& candidate : candidates) {
            if (candidate.cell >= 0) {
                return Move{candidate.direction, 2, 0.0, 0};
            }
        }
        return Move{Direction::up, 3, 0.0, 0};
    }

    // Pasada 1: el espacio, que es lo barato y lo unico que el escalon 1 necesita.
    // El deadline se comprueba ANTES de cada candidato, no despues del bucle entero:
    // con v1 o v2 encendidos ese bucle hace cuatro Voronoi y cuatro busquedas de
    // cuellos, y comprobar solo al final permite rebasar el deadline por todo el
    // trabajo de los cuatro. ver docs/invariants.md#inv-11
    for (auto& candidate : candidates) {
        if (!candidate.safe) {
            continue;
        }
        if (deadline.expired()) {
            break;
        }
        Board reachable = free_cells;
        reachable.set(candidate.cell);
        candidate.space = eval::flood(reachable, candidate.cell).cells;
        candidate.worst_space = candidate.space;
    }

    // Pasada 2: las heuristicas caras. O se calculan para TODOS los candidatos o no se
    // usa ninguna: comparar un candidato puntuado con territorio contra otro puntuado
    // sin el es peor que no tener territorio, porque el que se quedo sin calcular saca
    // cero y se descarta por haber llegado tarde, no por ser malo.
    const bool wants_bottleneck = params.space.worst_case_weight > 0.0 && !degraded;
    const bool wants_territory = params.territory.version >= 1 && !degraded;
    bool deep = wants_bottleneck || wants_territory;
    if (deep) {
        for (auto& candidate : candidates) {
            if (!candidate.safe) {
                continue;
            }
            if (deadline.expired()) {
                deep = false;
                break;
            }
            Board reachable = free_cells;
            reachable.set(candidate.cell);
            // El espacio que quedaria si el rival tapase el peor cuello de la region. Lo
            // paga solo quien lo enciende: en un tablero abierto no hay cuellos de grado
            // 2 o 3 y el bucle no llega a hacer ningun flood extra.
            if (wants_bottleneck) {
                candidate.worst_space = eval::worst_case_space(
                    reachable, candidate.cell, params.space.worst_case_max_cuellos);
            }
            // v1: ademas del espacio que existe, el que se alcanza antes que los
            // rivales. Se calcula desde la casilla candidata, sin copiar el estado ni
            // inventar los movimientos de los demas. ver docs/strategy.md#s-v1
            if (wants_territory) {
                const auto t = eval::voronoi(state,
                                             blocked,
                                             params.territory.hazard_value_pct,
                                             static_cast<int>(state.you),
                                             candidate.cell);
                candidate.territory = t.weighted[static_cast<std::size_t>(state.you)];
                candidate.contested = t.contested;
            }
        }
    }
    if (!deep) {
        // Degradacion consistente: se vuelve a la puntuacion de v0 para todos.
        for (auto& candidate : candidates) {
            candidate.worst_space = candidate.space;
        }
    }

    // Escalon 1: el deadline ya vencio antes de puntuar nada. Se devuelve el movimiento
    // seguro con mas espacio, que es barato y nunca es mortal de inmediato.
    if (deadline.expired()) {
        const Candidate* roomiest = nullptr;
        for (const auto& candidate : candidates) {
            if (!candidate.safe) {
                continue;
            }
            if (roomiest == nullptr || candidate.space > roomiest->space) {
                roomiest = &candidate;
            }
        }
        if (roomiest != nullptr) {
            return Move{roomiest->direction, 1, 0.0, safe_count};
        }
    }

    // Se rechaza el movimiento cuyo espacio alcanzable sea menor que la longitud
    // propia, salvo que todas las opciones lo sean: entonces se juega la mayor.
    const int min_space =
        static_cast<int>(static_cast<double>(my_length) * params.space.min_space_ratio);
    int roomy_count = 0;
    for (const auto& candidate : candidates) {
        if (candidate.safe && candidate.space >= min_space) {
            ++roomy_count;
        }
    }

    const Candidate* best = nullptr;
    for (auto& candidate : candidates) {
        if (!candidate.safe) {
            continue;
        }
        if (roomy_count > 0 && candidate.space < min_space) {
            continue;
        }
        // El deadline se comprueba ANTES de puntuar, no despues: `score_candidate`
        // hace una BFS de hasta 121 turnos buscando comida, y mirar el reloj cuando ya
        // se ha pagado es enterarse tarde. Con un candidato ya puntuado siempre hay un
        // mejor movimiento conocido que devolver. ver docs/invariants.md#inv-11
        if (best != nullptr && deadline.expired()) {
            break;
        }
        candidate.score = score_candidate(state, params, candidate, free_cells, degraded, deep);
        if (best == nullptr || candidate.score > best->score) {
            best = &candidate;
        }
    }

    if (best == nullptr) {
        // Defensa: el bucle anterior siempre deja un mejor candidato cuando hay alguno
        // seguro, pero si eso cambiara, el escalon 1 sigue siendo mejor que rendirse.
        for (const auto& candidate : candidates) {
            if (!candidate.safe) {
                continue;
            }
            if (best == nullptr || candidate.space > best->space) {
                best = &candidate;
            }
        }
        if (best != nullptr) {
            return Move{best->direction, 1, 0.0, safe_count};
        }
        return Move{Direction::up, 3, 0.0, safe_count};
    }

    return Move{best->direction, 0, best->score, safe_count};
}

} // namespace

Move decide(const State& state, Deadline deadline, const Params& params) noexcept {
    try {
        const bool degraded = !engine::is_supported(state.rules.variant);
        // [v2] La busqueda solo entra en royale y con tiempo por delante. En modo
        // degradado NO: simular una variante cuyas reglas el motor no reproduce daria
        // un arbol de posiciones que no van a ocurrir, que es peor que no mirar.
        // ver docs/decisions/ADR-0022-busqueda-paranoica.md
        // Sin NINGUN movimiento seguro no hay nada que buscar: todas las ramas mueren, y
        // lo que toca es la escalera del fail-safe, que esta documentada y probada escalon
        // a escalon (ver docs/rules.md#r-03). Dejar que la busqueda conteste ahi devolvia
        // `fallback_level = 0` en posiciones sin salida, o sea que el log decia "decision
        // normal" cuando era una muerte. Lo caza el test de los cuatro escalones.
        //
        // La guarda de estado imposible (serpiente propia ausente o de longitud cero) va
        // ANTES que la busqueda por el mismo motivo: sobre un estado que el cerebro no
        // puede razonar, buscar es razonar igual.
        const bool estado_razonable =
            static_cast<int>(state.you) < state.count() && state.snake(state.you).length > 0;
        const bool hay_donde_elegir =
            estado_razonable && engine::legal_moves(state, state.you) != engine::move_mask_none;
        if (params.search.version >= 1 && !degraded && hay_donde_elegir && !deadline.expired()) {
            const SearchResult r = search(state, deadline, params);
            if (r.depth >= 1) {
                // Cinturon: la busqueda no puede devolver algo que v0 rechazaria por
                // mortal. Si lo hiciera -un bug ahi dentro- se cae a v0 en vez de morir.
                // [v11] Una raiz con puntuacion de muerte no es una decision, es una
                // rendicion: bajo el supuesto paranoico todas las ramas mueren y solo se
                // elige la que tarda mas. Con movimientos simultaneos eso casi nunca es
                // cierto -el rival tiene que adivinar-, asi que se decide con v0, que mira
                // el espacio de verdad. ver docs/experimentos.md#s-desesperacion
                const bool rendida = params.search.despair_version >= 1 &&
                                     r.score <= 0.5 * params.search.death_value;
                if (!rendida && engine::mask_has(engine::legal_moves(state, state.you), r.best)) {
                    return Move{r.best, 0, r.score, r.depth, r.depth, r.nodes, r.corto_el_reloj};
                }
            }
            // Sin una sola profundidad completada, o con un movimiento sospechoso, manda
            // v0: es la referencia que si esta medida.
        }
        return decide_impl(state, deadline, params, degraded);
    } catch (...) {
        // Escalon 3: cualquier excepcion cae al movimiento determinista documentado.
        // El servidor nunca devuelve 5xx en /move. ver docs/rules.md#r-03
        return Move{Direction::up, 3, 0.0, 0};
    }
}

Move decide_degraded(const State& state, Deadline deadline, const Params& params) noexcept {
    try {
        return decide_impl(state, deadline, params, true);
    } catch (...) {
        return Move{Direction::up, 3, 0.0, 0};
    }
}

/// Destino de los movimientos de calentamiento. Es `volatile` para que el optimizador no
/// pueda demostrar que la llamada no hace nada y borrarla, que es justo lo contrario de lo
/// que este codigo quiere. ver docs/decisions/ADR-0021-arranque-en-frio.md
namespace {
volatile int warmup_sink = 0;
} // namespace

long long warmup(const Params& params) noexcept {
    try {
        // Un estado de spawn corriente: dos serpientes de tres segmentos apilados y una
        // comida. No pretende ser representativo de nada, solo tocar el mismo codigo que
        // tocara el primer movimiento de verdad.
        State state{};
        state.snake_count = 2;
        state.you = 0;
        for (int s = 0; s < 2; ++s) {
            auto& snake = state.snakes[static_cast<unsigned>(s)];
            snake.head_slot = 0;
            snake.length = 3;
            snake.health = 100;
            snake.status = engine::Elimination::alive;
            snake.eliminated_on_turn = -1;
            const int cell = Board::index_of(s == 0 ? Coord{1, 1} : Coord{9, 9});
            for (int seg = 0; seg < 3; ++seg) {
                snake.cells[static_cast<unsigned>(seg)] = static_cast<std::uint16_t>(cell);
            }
        }
        state.food.set(Board::index_of(Coord{5, 5}));
        state.refresh_occupancy();

        const auto started = Deadline::Clock::now();
        // Deadline CORTO a proposito. Calentar es tocar el codigo para que quede
        // residente, no jugar: con la busqueda encendida por defecto, un deadline de un
        // segundo hace que `warmup()` se gaste el segundo entero -medido: 998 ms- y eso
        // lo pagaria cada `/start` de cada partida. Con 20 ms se completan las primeras
        // profundidades, que recorren exactamente las mismas funciones.
        // ver docs/decisions/ADR-0021-arranque-en-frio.md
        const Deadline holgado(started + std::chrono::milliseconds(20));
        // El resultado se descarta a proposito; lo que importa es el efecto secundario de
        // haber ejecutado el camino. El destino es `volatile` para que -O3 no se lleve la
        // llamada entera por no usarse el valor.
        warmup_sink = static_cast<int>(decide(state, holgado, params).direction);
        // Y una segunda pasada por el camino degradado, que es el que corre cuando el
        // ruleset no es royale y que no comparte todas las ramas con el normal.
        warmup_sink = static_cast<int>(decide_degraded(state, holgado, params).direction);
        return std::chrono::duration_cast<std::chrono::microseconds>(Deadline::Clock::now() -
                                                                     started)
            .count();
    } catch (...) {
        return -1;
    }
}

} // namespace snake
