/// @file royale_map.cpp
/// Hazards del mapa royale: el rectangulo que se encoge.
///
/// Vive aparte de `rules.cpp` por la misma razon que en la fuente: el motor oficial
/// separa el pipeline del turno (`standard.go`) del mapa que genera hazards y comida
/// (`maps/royale.go`). Aqui ademas mantiene a `rules.cpp` dentro del presupuesto por
/// tarea del pack de reglas (ver docs/INDEX.md#i-02).

#include <cstdint>

#include <engine/bitboard.hpp>
#include <engine/rng.hpp>
#include <engine/rules.hpp>

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

template Bitboard<7, 7> royale_hazards<7, 7>(std::uint64_t, int, int) noexcept;
template Bitboard<11, 11> royale_hazards<11, 11>(std::uint64_t, int, int) noexcept;
template Bitboard<19, 19> royale_hazards<19, 19>(std::uint64_t, int, int) noexcept;

} // namespace engine
