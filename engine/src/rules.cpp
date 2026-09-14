/// @file rules.cpp
/// Implementacion de las reglas. Cada bloque cita el anchor de docs/rules.md del que
/// deriva. Ninguna funcion asigna memoria. ver docs/invariants.md#inv-03

#include <engine/rules.hpp>

#include <algorithm>
#include <cstddef>
#include <limits>
#include <stdexcept>

namespace engine {

namespace {

/// Indices de las serpientes ordenados por longitud descendente. El motor oficial
/// hace lo mismo para atribuir la eliminacion a la mas larga en colisiones multiples.
/// ver docs/rules.md#r-08
template <int MaxSnakes>
struct ByLength {
    std::array<std::uint8_t, MaxSnakes> order{};
    int count{};
};

template <int W, int H, int MaxSnakes>
ByLength<MaxSnakes> order_by_length(const GameState<W, H, MaxSnakes>& s) noexcept {
    ByLength<MaxSnakes> result;
    result.count = static_cast<int>(s.snake_count);
    for (int i = 0; i < result.count; ++i) {
        result.order[static_cast<unsigned>(i)] = static_cast<std::uint8_t>(i);
    }
    // Insercion, no std::stable_sort: con 4 elementos es mas rapido y, sobre todo,
    // std::stable_sort puede pedir memoria temporal. ver docs/invariants.md#inv-03
    for (int i = 1; i < result.count; ++i) {
        const std::uint8_t key = result.order[static_cast<unsigned>(i)];
        const auto key_length = s.snakes[key].length;
        int j = i - 1;
        while (j >= 0 && s.snakes[result.order[static_cast<unsigned>(j)]].length < key_length) {
            result.order[static_cast<unsigned>(j + 1)] = result.order[static_cast<unsigned>(j)];
            --j;
        }
        result.order[static_cast<unsigned>(j + 1)] = key;
    }
    return result;
}

/// Cierto si la cabeza de `snake` cae sobre un segmento de `other` que no sea su cabeza.
/// El cuello entra por aqui: no es una regla especial. ver docs/rules.md#r-08
template <typename Snake>
bool head_hits_body(const Snake& snake, const Snake& other) noexcept {
    const int head = snake.head();
    for (int i = 1; i < static_cast<int>(other.length); ++i) {
        if (other.segment(i) == head) return true;
    }
    return false;
}

} // namespace

template <int W, int H, int MaxSnakes>
Direction default_move(const GameState<W, H, MaxSnakes>& s, SnakeId id) noexcept {
    using Board = typename GameState<W, H, MaxSnakes>::Board;
    const auto& snake = s.snake(id);
    if (snake.length < 2) return Direction::up;
    const Coord head = Board::coord_of(snake.head());
    const Coord neck = Board::coord_of(snake.neck());
    if (head == neck) return Direction::up;
    return direction_between(neck, head);
}

template <int W, int H, int MaxSnakes>
MoveMask legal_moves(const GameState<W, H, MaxSnakes>& s, SnakeId id) noexcept {
    using Board = typename GameState<W, H, MaxSnakes>::Board;
    const auto& me = s.snake(id);
    if (!is_alive(me.status)) return move_mask_none;

    // Casillas que seguiran ocupadas el proximo turno: todos los cuerpos vivos menos
    // las colas que avanzan. Una cola apilada NO se libera. ver docs/rules.md#r-04
    Board blocked;
    for (int i = 0; i < static_cast<int>(s.snake_count); ++i) {
        const auto& other = s.snakes[static_cast<unsigned>(i)];
        if (!is_alive(other.status)) continue;
        const int last = static_cast<int>(other.length) - 1;
        for (int seg = 0; seg <= last; ++seg) {
            if (seg == last && !other.tail_is_stacked()) continue;
            blocked.set(other.segment(seg));
        }
    }

    const Coord head = Board::coord_of(me.head());
    MoveMask mask = move_mask_none;
    for (int d = 0; d < direction_count; ++d) {
        const auto dir = static_cast<Direction>(d);
        const Coord next = step(head, dir);
        if (!Board::in_bounds(next)) continue;
        if (blocked.test(Board::index_of(next))) continue;
        mask = static_cast<MoveMask>(mask | to_mask(dir));
    }
    return mask;
}

template <int W, int H, int MaxSnakes>
bool is_terminal(const GameState<W, H, MaxSnakes>& s) noexcept {
    return s.alive_count() <= 1;
}

template <int W, int H, int MaxSnakes>
Status apply(GameState<W, H, MaxSnakes>& s, std::span<const Direction> moves) noexcept {
    using State = GameState<W, H, MaxSnakes>;
    using Board = typename State::Board;

    // Fase 1: fin de partida. Se evalua ANTES del movimiento, como el pipeline oficial.
    // ver docs/rules.md#r-02
    if (is_terminal(s)) return Status::game_over;

    const int n = static_cast<int>(s.snake_count);
    std::array<bool, MaxSnakes> out_of_bounds{};

    // Fase 2: movimiento simultaneo. ver docs/rules.md#r-03
    for (int i = 0; i < n; ++i) {
        auto& snake = s.snakes[static_cast<unsigned>(i)];
        if (!is_alive(snake.status)) continue;
        if (snake.length == 0) return Status::error;

        const Direction dir = (static_cast<std::size_t>(i) < moves.size())
                                  ? moves[static_cast<std::size_t>(i)]
                                  : default_move(s, static_cast<SnakeId>(i));
        const Coord next = step(Board::coord_of(snake.head()), dir);
        if (!Board::in_bounds(next)) {
            // Un indice de casilla no puede representar una posicion fuera del tablero:
            // se marca y la eliminacion se decide en la fase 6, que es donde el motor
            // oficial la resuelve, y solo despues de la muerte por hambre.
            out_of_bounds[static_cast<unsigned>(i)] = true;
            continue;
        }
        snake.advance(Board::index_of(next));
    }

    // Fase 3: hambre. ver docs/rules.md#r-05
    for (int i = 0; i < n; ++i) {
        auto& snake = s.snakes[static_cast<unsigned>(i)];
        if (!is_alive(snake.status)) continue;
        snake.health = static_cast<std::uint8_t>(
            std::max(0, static_cast<int>(snake.health) - health_loss_per_turn));
    }

    // Fase 4: daño de hazard. Solo la cabeza, y NO se aplica si hay comida en esa
    // casilla. ver docs/rules.md#r-06
    for (int i = 0; i < n; ++i) {
        auto& snake = s.snakes[static_cast<unsigned>(i)];
        if (!is_alive(snake.status)) continue;
        if (out_of_bounds[static_cast<unsigned>(i)]) continue;
        const int head = snake.head();
        if (!s.hazards.test(head)) continue;
        if (s.food.test(head)) continue;

        const int health = static_cast<int>(snake.health) - s.rules.hazard_damage_per_turn;
        snake.health = static_cast<std::uint8_t>(std::clamp(health, 0, max_health));
        if (snake.health == 0) {
            snake.status = Elimination::hazard;
            snake.eliminated_on_turn = s.turn + 1;
        }
    }

    // Fase 5: alimentacion. Varias serpientes pueden comer la misma casilla.
    // ver docs/rules.md#r-07
    Board eaten;
    for (int i = 0; i < n; ++i) {
        auto& snake = s.snakes[static_cast<unsigned>(i)];
        if (!is_alive(snake.status)) continue;
        if (out_of_bounds[static_cast<unsigned>(i)]) continue;
        const int head = snake.head();
        if (!s.food.test(head)) continue;
        snake.grow();
        snake.health = static_cast<std::uint8_t>(max_health);
        eaten.set(head);
    }
    s.food = s.food.without(eaten);

    // Fase 6: eliminacion. ver docs/rules.md#r-08
    // 6a. Hambre y fuera de tablero se aplican YA: dejan de bloquear este mismo turno.
    for (int i = 0; i < n; ++i) {
        auto& snake = s.snakes[static_cast<unsigned>(i)];
        if (!is_alive(snake.status)) continue;
        if (snake.health == 0) {
            snake.status = Elimination::out_of_health;
            snake.eliminated_on_turn = s.turn + 1;
            continue;
        }
        if (out_of_bounds[static_cast<unsigned>(i)]) {
            snake.status = Elimination::wall_collision;
            snake.eliminated_on_turn = s.turn + 1;
        }
    }

    // 6b. Colisiones: se recolectan sin aplicar, para que una serpiente muerta por
    // colision siga bloqueando a las demas este turno.
    const auto by_length = order_by_length(s);
    std::array<Elimination, MaxSnakes> pending{};
    for (auto& cause : pending) cause = Elimination::alive;

    for (int i = 0; i < n; ++i) {
        const auto& snake = s.snakes[static_cast<unsigned>(i)];
        if (!is_alive(snake.status)) continue;

        if (head_hits_body(snake, snake)) {
            pending[static_cast<unsigned>(i)] = Elimination::self_collision;
            continue;
        }

        bool collided = false;
        for (int k = 0; k < by_length.count; ++k) {
            const int j = static_cast<int>(by_length.order[static_cast<unsigned>(k)]);
            if (j == i) continue;
            const auto& other = s.snakes[static_cast<unsigned>(j)];
            if (!is_alive(other.status)) continue;
            if (head_hits_body(snake, other)) {
                pending[static_cast<unsigned>(i)] = Elimination::snake_collision;
                collided = true;
                break;
            }
        }
        if (collided) continue;

        // Cabeza a cabeza: pierde la de longitud MENOR O IGUAL, asi que con longitudes
        // iguales mueren las dos. ver docs/rules.md#r-08
        for (int k = 0; k < by_length.count; ++k) {
            const int j = static_cast<int>(by_length.order[static_cast<unsigned>(k)]);
            if (j == i) continue;
            const auto& other = s.snakes[static_cast<unsigned>(j)];
            if (!is_alive(other.status)) continue;
            if (other.head() == snake.head() && snake.length <= other.length) {
                pending[static_cast<unsigned>(i)] = Elimination::head_collision;
                break;
            }
        }
    }

    for (int i = 0; i < n; ++i) {
        if (pending[static_cast<unsigned>(i)] == Elimination::alive) continue;
        auto& snake = s.snakes[static_cast<unsigned>(i)];
        snake.status = pending[static_cast<unsigned>(i)];
        snake.eliminated_on_turn = s.turn + 1;
    }

    // El contador de turno no lo toca el ruleset oficial sino el arbitro; aqui lo lleva
    // el motor porque no hay arbitro. ver docs/rules.md#r-02
    ++s.turn;
    s.refresh_occupancy();

    return is_terminal(s) ? Status::game_over : Status::ok;
}

template <int W, int H, int MaxSnakes>
PlacementsT<MaxSnakes> placements(const GameState<W, H, MaxSnakes>& s) noexcept {
    PlacementsT<MaxSnakes> result;
    const int n = static_cast<int>(s.snake_count);
    result.count = n;

    // Clave de orden: viva > muerta, y entre muertas gana la que murio mas tarde.
    const auto key = [&s](int i) {
        const auto& snake = s.snakes[static_cast<unsigned>(i)];
        return is_alive(snake.status) ? std::numeric_limits<std::int32_t>::max()
                                      : snake.eliminated_on_turn;
    };

    for (int i = 0; i < n; ++i) {
        // Rango compartido promediado: 1 + (numero de estrictamente mejores) +
        // (numero de empatadas - 1) / 2. Nunca se desempata por indice ni por asiento.
        // ver docs/rules.md#r-12
        int better = 0;
        int tied = 0;
        for (int j = 0; j < n; ++j) {
            if (key(j) > key(i)) {
                ++better;
            } else if (key(j) == key(i)) {
                ++tied;
            }
        }
        result.rank[static_cast<unsigned>(i)] =
            static_cast<float>(better + 1) + static_cast<float>(tied - 1) / 2.0F;
    }
    return result;
}

template <int W, int H>
Bitboard<W, H> royale_hazards(std::uint64_t seed, int turn, int shrink_every_n_turns) {
    (void)seed;
    (void)turn;
    (void)shrink_every_n_turns;
    // El schedule de shrink solo es reproducible dentro de la arena in-process, porque
    // depende del `math/rand` de Go y la semilla no viaja en el payload de /move.
    // ver docs/rules.md#r-09 y docs/rules.md#r-99
    throw std::logic_error("no implementado: fase 1");
}

template Direction default_move<7, 7, 4>(const GameState<7, 7, 4>&, SnakeId) noexcept;
template Direction default_move<11, 11, 4>(const GameState<11, 11, 4>&, SnakeId) noexcept;
template Direction default_move<19, 19, 4>(const GameState<19, 19, 4>&, SnakeId) noexcept;

template Status apply<7, 7, 4>(GameState<7, 7, 4>&, std::span<const Direction>) noexcept;
template Status apply<11, 11, 4>(GameState<11, 11, 4>&, std::span<const Direction>) noexcept;
template Status apply<19, 19, 4>(GameState<19, 19, 4>&, std::span<const Direction>) noexcept;

template MoveMask legal_moves<7, 7, 4>(const GameState<7, 7, 4>&, SnakeId) noexcept;
template MoveMask legal_moves<11, 11, 4>(const GameState<11, 11, 4>&, SnakeId) noexcept;
template MoveMask legal_moves<19, 19, 4>(const GameState<19, 19, 4>&, SnakeId) noexcept;

template bool is_terminal<7, 7, 4>(const GameState<7, 7, 4>&) noexcept;
template bool is_terminal<11, 11, 4>(const GameState<11, 11, 4>&) noexcept;
template bool is_terminal<19, 19, 4>(const GameState<19, 19, 4>&) noexcept;

template PlacementsT<4> placements<7, 7, 4>(const GameState<7, 7, 4>&) noexcept;
template PlacementsT<4> placements<11, 11, 4>(const GameState<11, 11, 4>&) noexcept;
template PlacementsT<4> placements<19, 19, 4>(const GameState<19, 19, 4>&) noexcept;

template Bitboard<7, 7> royale_hazards<7, 7>(std::uint64_t, int, int);
template Bitboard<11, 11> royale_hazards<11, 11>(std::uint64_t, int, int);
template Bitboard<19, 19> royale_hazards<19, 19>(std::uint64_t, int, int);

} // namespace engine
