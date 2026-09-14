/// @file brain_v0.cpp
/// Baseline COMPLETO y jugable. No es un stub: es la referencia fija contra la que se
/// mide toda version posterior, y no se borra nunca. ver docs/strategy.md#s-v0
///
/// Cada punto de la especificacion del baseline tiene su fixture en tests/fixtures.

#include <snake/brain.hpp>

#include <algorithm>
#include <array>
#include <exception>

#include <engine/rules.hpp>

#include <snake/eval/floodfill.hpp>

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
Board blocked_cells(const State& state) noexcept {
    Board blocked;
    for (int i = 0; i < static_cast<int>(state.snake_count); ++i) {
        const auto& other = state.snakes[static_cast<unsigned>(i)];
        if (!engine::is_alive(other.status)) continue;
        const int last = static_cast<int>(other.length) - 1;
        for (int seg = 0; seg <= last; ++seg) {
            if (seg == last && !other.tail_is_stacked()) continue;
            blocked.set(other.segment(seg));
        }
    }
    return blocked;
}

/// Distancia en turnos a la comida mas cercana alcanzable, o -1.
int nearest_food_distance(const Board& free_cells, int start, const Board& food,
                          int max_turns) noexcept {
    if (start < 0) return -1;
    if (food.test(start)) return 0;

    Board frontier;
    frontier.set(start);
    Board seen = frontier;
    for (int turn = 1; turn <= max_turns; ++turn) {
        const Board next = (frontier.expand() & free_cells).without(seen);
        if (next.none()) return -1;
        if ((next & food).any()) return turn;
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
};

/// Puntuacion de un movimiento ya filtrado. Todos los pesos salen del config:
/// ninguna constante magica vive aqui.
double score_candidate(const State& state, const Params& params, const Candidate& candidate,
                       const Board& free_cells, bool degraded) noexcept {
    const auto& me = state.snake(state.you);
    const auto my_length = static_cast<int>(me.length);
    double score = 0.0;

    // 1. Espacio alcanzable, normalizado por el tamaño del tablero.
    score += params.space.weight * static_cast<double>(candidate.space) /
             static_cast<double>(State::cells);

    // 2. Zona de cabeza: se evitan las casillas adyacentes a cabezas iguales o mas
    //    largas y se prefieren las adyacentes a cabezas estrictamente mas cortas.
    for (int i = 0; i < static_cast<int>(state.snake_count); ++i) {
        if (i == static_cast<int>(state.you)) continue;
        const auto& other = state.snakes[static_cast<unsigned>(i)];
        if (!engine::is_alive(other.status)) continue;
        if (!head_zone(state, i).test(candidate.cell)) continue;

        if (static_cast<int>(other.length) >= my_length) {
            score -= params.head.avoid_equal_or_longer;
        } else {
            score += params.head.prefer_shorter;
        }
    }

    if (degraded) return score;

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

Move decide_impl(const State& state, Deadline deadline, const Params& params,
                 bool degraded) noexcept {
    // Escalon 3: estado imposible de razonar (serpiente propia ausente o de longitud
    // cero). Se devuelve el movimiento determinista documentado, nunca una excepcion.
    // ver docs/rules.md#r-03
    if (state.you >= state.snake_count || state.snake(state.you).length == 0) {
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

    std::array<Candidate, direction_count> candidates{};
    int safe_count = 0;
    for (int d = 0; d < direction_count; ++d) {
        auto& candidate = candidates[static_cast<unsigned>(d)];
        candidate.direction = static_cast<Direction>(d);
        const Coord next = step(head, candidate.direction);
        if (!Board::in_bounds(next)) continue;
        candidate.cell = Board::index_of(next);
        if (blocked.test(candidate.cell)) continue;
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

    for (auto& candidate : candidates) {
        if (!candidate.safe) continue;
        Board reachable = free_cells;
        reachable.set(candidate.cell);
        candidate.space = eval::flood(reachable, candidate.cell).cells;
    }

    // Escalon 1: el deadline ya vencio antes de puntuar nada. Se devuelve el movimiento
    // seguro con mas espacio, que es barato y nunca es mortal de inmediato.
    if (deadline.expired()) {
        const Candidate* roomiest = nullptr;
        for (const auto& candidate : candidates) {
            if (!candidate.safe) continue;
            if (roomiest == nullptr || candidate.space > roomiest->space) roomiest = &candidate;
        }
        if (roomiest != nullptr) {
            return Move{roomiest->direction, 1, 0.0, safe_count};
        }
    }

    // Se rechaza el movimiento cuyo espacio alcanzable sea menor que la longitud
    // propia, salvo que todas las opciones lo sean: entonces se juega la mayor.
    const int min_space = static_cast<int>(static_cast<double>(my_length) *
                                           params.space.min_space_ratio);
    int roomy_count = 0;
    for (const auto& candidate : candidates) {
        if (candidate.safe && candidate.space >= min_space) ++roomy_count;
    }

    const Candidate* best = nullptr;
    for (auto& candidate : candidates) {
        if (!candidate.safe) continue;
        if (roomy_count > 0 && candidate.space < min_space) continue;
        candidate.score = score_candidate(state, params, candidate, free_cells, degraded);
        if (best == nullptr || candidate.score > best->score) {
            best = &candidate;
        }
        // El deadline se comprueba entre candidatos: siempre hay un mejor movimiento
        // conocido que devolver. ver docs/invariants.md#inv-11
        if (deadline.expired()) break;
    }

    if (best == nullptr) {
        // Defensa: el bucle anterior siempre deja un mejor candidato cuando hay alguno
        // seguro, pero si eso cambiara, el escalon 1 sigue siendo mejor que rendirse.
        for (const auto& candidate : candidates) {
            if (!candidate.safe) continue;
            if (best == nullptr || candidate.space > best->space) best = &candidate;
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

} // namespace snake
