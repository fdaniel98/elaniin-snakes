/// @file royale_map.cpp
/// Hazards del mapa royale: el rectangulo que se encoge.
///
/// Vive aparte de `rules.cpp` por la misma razon que en la fuente: el motor oficial
/// separa el pipeline del turno (`standard.go`) del mapa que genera hazards y comida
/// (`maps/royale.go`). Aqui ademas mantiene a `rules.cpp` dentro del presupuesto por
/// tarea del pack de reglas (ver docs/INDEX.md#i-02).

#include <array>
#include <cstddef>
#include <cstdint>

#include <engine/bitboard.hpp>
#include <engine/rng.hpp>
#include <engine/rules.hpp>
#include <engine/state.hpp>

namespace engine {

template <int W, int H>
Bitboard<W, H> royale_hazards(std::uint64_t seed, int turn, int shrink_every_n_turns) noexcept {
    Bitboard<W, H> hazards;

    // El motor oficial devuelve error con una cadencia menor que 1 y el pipeline aborta
    // la partida (`maps/royale.go:50-52`). Aqui no hay a quien devolverlo: la funcion es
    // `noexcept` y responde el tablero sin hazards, que el llamante no puede distinguir
    // de un turno anterior al primer shrink. Es una PRECONDICION sin comprobar: hoy no
    // hay llamante -la arena es de la fase 4- y cuando lo haya tendra que validar antes.
    // Anotado en STATE.md como pendiente.
    if (shrink_every_n_turns < 1) {
        return hazards;
    }
    // Antes del primer shrink no hay hazard alguno. ver docs/rules.md#r-09
    if (turn < shrink_every_n_turns) {
        return hazards;
    }

    // Cada turno se regenera desde cero desde la semilla de la partida, nunca
    // incrementalmente: por eso el generador se construye aqui y no se conserva.
    //
    // Con semilla 0 el motor oficial NO re-siembra y cae a su generador global, que no es
    // reproducible (`settings.go:47-52`, ver docs/rules-parametros.md#r-99). El nuestro si
    // es determinista con 0, asi que en ese caso la divergencia es deliberada y a favor:
    // la secuencia de lados ya era nuestra de todas formas.
    // El `Rng` es el del repo, no el `math/rand` de Go, asi que la cadencia y la forma
    // son las oficiales pero la secuencia de lados es nuestra.
    // ver docs/decisions/ADR-0010-rng-del-shrink.md#d-0091
    Rng rng(seed);

    const int num_shrinks = turn / shrink_every_n_turns;
    int min_x = 0;
    int max_x = W - 1;
    int min_y = 0;
    int max_y = H - 1;
    for (int i = 0; i < num_shrinks; ++i) {
        // Un solo borde por shrink, con repeticion. ver docs/rules.md#r-09
        switch (rng.bounded(4)) {
            case 0:
                ++min_x;
                break;
            case 1:
                --max_x;
                break;
            case 2:
                ++min_y;
                break;
            default:
                --max_y;
                break;
        }
    }

    // El hazard es el complemento del rectangulo, que puede quedar vacio si se encogio
    // mas veces que ancho tiene el tablero.
    for (int x = 0; x < W; ++x) {
        for (int y = 0; y < H; ++y) {
            if (x < min_x || x > max_x || y < min_y || y > max_y) {
                hazards.set(Bitboard<W, H>::index_of(x, y));
            }
        }
    }
    return hazards;
}

template <int W, int H, int MaxSnakes>
Bitboard<W, H> spawn_food(const GameState<W, H, MaxSnakes>& s, std::uint64_t seed) noexcept {
    using Board = Bitboard<W, H>;
    Board nueva;

    // `apply()` ya incremento el contador, y el Go siembra con el turno del estado
    // ANTERIOR al hook (`maps/standard.go:65`). El turno 0 no pasa por aqui: la comida
    // inicial la coloca la colocacion, no el hook. ver docs/rules.md#r-11
    const int turno_go = s.turn - 1;
    if (turno_go < 0) {
        return nueva;
    }

    // Siembra por turno, igual que `settings.GetRand(turn)`. Sumar el turno a la semilla
    // es la forma del Go; lo que evita que semillas contiguas produzcan sorteos parecidos
    // es el splitmix64 del constructor, no la suma. ver docs/rules.md#r-10
    Rng rng(seed + static_cast<std::uint64_t>(turno_go));

    const int actual = s.food.count();
    int faltan = 0;
    if (actual < s.rules.minimum_food) {
        // Rama de minimo: el Go vuelve ANTES de tocar el generador
        // (`maps/standard.go:80-82`). Consumir un numero aqui desalinearia el stream
        // respecto del motor oficial y, peor, entre dos ramas de un A/B que hayan
        // repuesto comida distinto numero de veces.
        faltan = s.rules.minimum_food - actual;
    } else if (s.rules.food_spawn_chance > 0 &&
               (100 - static_cast<int>(rng.bounded(100))) < s.rules.food_spawn_chance) {
        faltan = 1;
    }
    if (faltan <= 0) {
        return nueva;
    }

    // Casillas desocupadas: sin cuerpo vivo y sin comida. Los hazards NO excluyen
    // (`board.go:522`). ver docs/rules.md#r-10
    std::array<std::uint16_t, static_cast<std::size_t>(W * H)> libres{};
    int n = 0;
    for (int c = 0; c < W * H; ++c) {
        if (!s.bodies.test(c) && !s.food.test(c)) {
            libres[static_cast<std::size_t>(n++)] = static_cast<std::uint16_t>(c);
        }
    }
    if (n == 0) {
        // Tablero lleno: el Go tampoco coloca nada (`maps/standard.go:91-93`).
        return nueva;
    }

    rng.shuffle(libres.data(), static_cast<std::size_t>(n));
    const int cuantas = faltan < n ? faltan : n;
    for (int i = 0; i < cuantas; ++i) {
        nueva.set(static_cast<int>(libres[static_cast<std::size_t>(i)]));
    }
    return nueva;
}

template Bitboard<7, 7> royale_hazards<7, 7>(std::uint64_t, int, int) noexcept;
template Bitboard<11, 11> royale_hazards<11, 11>(std::uint64_t, int, int) noexcept;
template Bitboard<19, 19> royale_hazards<19, 19>(std::uint64_t, int, int) noexcept;

template Bitboard<7, 7> spawn_food<7, 7, 4>(const GameState<7, 7, 4>&, std::uint64_t) noexcept;
template Bitboard<11, 11> spawn_food<11, 11, 4>(const GameState<11, 11, 4>&,
                                                std::uint64_t) noexcept;
template Bitboard<19, 19> spawn_food<19, 19, 4>(const GameState<19, 19, 4>&,
                                                std::uint64_t) noexcept;

} // namespace engine
