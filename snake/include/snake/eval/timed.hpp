#pragma once

/// @file timed.hpp
/// Espacio CON RELOJ: casillas alcanzables contando que los cuerpos se retiran.
///
/// El flood fill de `floodfill.hpp` congela los cuerpos: solo la punta de la cola cuenta
/// como libre. En un final apretado eso miente en las dos direcciones. Un bolsillo de 6
/// casillas cerrado por nuestro propio cuello parece igual de malo que un pasillo de 6 que
/// acaba en nuestra cola -que se va retirando y deja pasar-. Aqui cada segmento se libera
/// cuando le toca: el segmento `j` de una serpiente de longitud `L` deja su casilla tras
/// `L - j` turnos, si nadie come.
///
/// Se llega a una casilla en el turno `t` si un vecino se alcanzo antes y la casilla ya
/// esta libre en `t`. Esperar dentro de lo alcanzado solo se permite si hay sitio para
/// hacerlo: con `alcanzado >= t` casillas se pudo dar vueltas `t` turnos. Es una cota
/// optimista -ignora que nuestro propio rastro ocupa y que las cabezas rivales avanzan-,
/// pero separa el pasillo que acaba en la cola del bolsillo que acaba en el cuello, que es
/// justo lo que las siete derrotas del leaderboard del 2026-09-25 no separaban.
/// ver docs/experimentos-duelo.md#s-liga-0925

#include <array>
#include <cstdint>

#include <engine/bitboard.hpp>
#include <engine/rules.hpp>
#include <engine/state.hpp>

namespace snake::eval {

/// Nucleo: casillas alcanzables partiendo de `inicio` en el turno `t0`. Ver arriba.
template <typename State>
[[nodiscard]] int timed_reach(const State& s, int inicio, int t0) noexcept {
    using Board = typename State::Board;
    constexpr int k_cells = State::cells;

    // Turno en que se libera cada casilla (0 = libre ya). Un segmento apilado sobre otro
    // hereda el mayor, que es el del segmento mas cercano a la cabeza.
    std::array<std::uint8_t, static_cast<std::size_t>(k_cells)> libera{};
    int maximo = 0;
    for (int i = 0; i < s.count(); ++i) {
        const auto& sn = s.snakes[static_cast<unsigned>(i)];
        if (!engine::is_alive(sn.status)) {
            continue;
        }
        const int largo = static_cast<int>(sn.length);
        for (int j = 0; j < largo; ++j) {
            const int t = largo - j > 255 ? 255 : largo - j;
            auto& l = libera[static_cast<std::size_t>(sn.segment(j))];
            if (t > l) {
                l = static_cast<std::uint8_t>(t);
            }
            maximo = t > maximo ? t : maximo;
        }
    }
    if (t0 > 0 && libera[static_cast<std::size_t>(inicio)] > t0) {
        return -1;
    }

    // Por cubos de turno: `bloqueado` pierde cada turno las casillas que se liberan.
    std::array<Board, 256> cubo{};
    Board bloqueado;
    for (int c = 0; c < k_cells; ++c) {
        const int t = libera[static_cast<std::size_t>(c)];
        if (t > t0 && c != inicio) {
            cubo[static_cast<std::size_t>(t)].set(c);
            bloqueado.set(c);
        }
    }

    Board visto;
    visto.set(inicio);
    Board frente = visto;
    const int tope = k_cells + maximo;
    for (int t = t0 + 1; t <= tope; ++t) {
        if (t <= 255) {
            bloqueado = bloqueado.without(cubo[static_cast<std::size_t>(t)]);
        }
        // Turnos que llevamos moviendonos: sin una casilla mas que turnos andados no ha
        // habido sitio para esperar, y solo avanza el frente.
        const int andados = t - t0;
        const Board desde = visto.count() > andados ? visto : frente;
        const Board nuevo = desde.expand().without(bloqueado).without(visto);
        if (nuevo.none() && visto.count() <= andados) {
            break; // ni se avanza ni hay sitio para esperar a que algo se libere
        }
        visto |= nuevo;
        frente = nuevo.none() ? frente : nuevo;
        if (visto.count() >= k_cells) {
            break;
        }
    }
    return visto.count();
}

/// Casillas alcanzables desde la cabeza de `us` si se mueve en `dir`. -1 si `dir` sale
/// del tablero o choca con algo que sigue ocupado el turno que viene.
template <typename State>
[[nodiscard]] int timed_space(const State& s, engine::SnakeId us, engine::Direction dir) noexcept {
    using Board = typename State::Board;
    const auto next = engine::step(Board::coord_of(s.snake(us).head()), dir);
    if (!Board::in_bounds(next)) {
        return -1;
    }
    return timed_reach(s, Board::index_of(next), 1);
}

/// Lo mismo desde donde esta la cabeza ahora, sin mover: es lo que mira una hoja.
template <typename State>
[[nodiscard]] int timed_space_here(const State& s, engine::SnakeId us) noexcept {
    return timed_reach(s, s.snake(us).head(), 0);
}

} // namespace snake::eval
