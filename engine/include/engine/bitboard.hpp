#pragma once

/// @file bitboard.hpp
/// Bitboard parametrizado en tiempo de compilacion. `ceil(W*H/64)` palabras de 64
/// bits; el bit `y*W + x` representa la casilla `(x,y)`.
///
/// Ninguna operacion asigna memoria. ver docs/invariants.md#inv-03

#include <array>
#include <bit>
#include <cstdint>

#include <engine/types.hpp>

namespace engine {

template <int W, int H>
class Bitboard {
    static_assert(W > 0 && H > 0, "tablero vacio");
    static_assert(W <= 32 && H <= 32, "el desplazamiento por filas asume W < 64");

public:
    static constexpr int width = W;
    static constexpr int height = H;
    static constexpr int cells = W * H;
    static constexpr int word_count = (cells + 63) / 64;

    using Word = std::uint64_t;

    constexpr Bitboard() noexcept = default;

    [[nodiscard]] static constexpr int index_of(int x, int y) noexcept { return y * W + x; }

    [[nodiscard]] static constexpr int index_of(Coord c) noexcept {
        return index_of(static_cast<int>(c.x), static_cast<int>(c.y));
    }

    [[nodiscard]] static constexpr Coord coord_of(int index) noexcept {
        return Coord{static_cast<std::int8_t>(index % W), static_cast<std::int8_t>(index / W)};
    }

    [[nodiscard]] static constexpr bool in_bounds(Coord c) noexcept {
        return c.x >= 0 && c.y >= 0 && static_cast<int>(c.x) < W && static_cast<int>(c.y) < H;
    }

    constexpr void set(int index) noexcept {
        words_[static_cast<unsigned>(index) / 64U] |= Word{1} << (static_cast<unsigned>(index) % 64U);
    }

    constexpr void reset(int index) noexcept {
        words_[static_cast<unsigned>(index) / 64U] &=
            ~(Word{1} << (static_cast<unsigned>(index) % 64U));
    }

    [[nodiscard]] constexpr bool test(int index) const noexcept {
        return (words_[static_cast<unsigned>(index) / 64U] &
                (Word{1} << (static_cast<unsigned>(index) % 64U))) != 0;
    }

    constexpr void set(Coord c) noexcept { set(index_of(c)); }

    constexpr void reset(Coord c) noexcept { reset(index_of(c)); }

    [[nodiscard]] constexpr bool test(Coord c) const noexcept { return test(index_of(c)); }

    constexpr void clear() noexcept { words_.fill(0); }

    [[nodiscard]] constexpr bool any() const noexcept {
        for (Word w : words_) {
            if (w != 0) return true;
        }
        return false;
    }

    [[nodiscard]] constexpr bool none() const noexcept { return !any(); }

    [[nodiscard]] constexpr int count() const noexcept {
        int total = 0;
        for (Word w : words_) {
            total += std::popcount(w);
        }
        return total;
    }

    /// Indice del primer bit puesto, o -1 si esta vacio.
    [[nodiscard]] constexpr int first() const noexcept {
        for (int i = 0; i < word_count; ++i) {
            if (words_[static_cast<unsigned>(i)] != 0) {
                return i * 64 + std::countr_zero(words_[static_cast<unsigned>(i)]);
            }
        }
        return -1;
    }

    /// Quita y devuelve el primer bit puesto, o -1 si esta vacio.
    constexpr int pop_first() noexcept {
        const int index = first();
        if (index >= 0) reset(index);
        return index;
    }

    constexpr Bitboard& operator|=(const Bitboard& o) noexcept {
        for (int i = 0; i < word_count; ++i) {
            words_[static_cast<unsigned>(i)] |= o.words_[static_cast<unsigned>(i)];
        }
        return *this;
    }

    constexpr Bitboard& operator&=(const Bitboard& o) noexcept {
        for (int i = 0; i < word_count; ++i) {
            words_[static_cast<unsigned>(i)] &= o.words_[static_cast<unsigned>(i)];
        }
        return *this;
    }

    constexpr Bitboard& operator^=(const Bitboard& o) noexcept {
        for (int i = 0; i < word_count; ++i) {
            words_[static_cast<unsigned>(i)] ^= o.words_[static_cast<unsigned>(i)];
        }
        return *this;
    }

    [[nodiscard]] constexpr Bitboard operator|(const Bitboard& o) const noexcept {
        Bitboard r = *this;
        r |= o;
        return r;
    }

    [[nodiscard]] constexpr Bitboard operator&(const Bitboard& o) const noexcept {
        Bitboard r = *this;
        r &= o;
        return r;
    }

    [[nodiscard]] constexpr Bitboard operator^(const Bitboard& o) const noexcept {
        Bitboard r = *this;
        r ^= o;
        return r;
    }

    /// Complemento dentro del tablero: los bits sobrantes de la ultima palabra
    /// se quedan a cero para que `count()` no los cuente.
    [[nodiscard]] constexpr Bitboard operator~() const noexcept {
        Bitboard r;
        for (int i = 0; i < word_count; ++i) {
            r.words_[static_cast<unsigned>(i)] = ~words_[static_cast<unsigned>(i)];
        }
        r.trim();
        return r;
    }

    [[nodiscard]] constexpr Bitboard without(const Bitboard& o) const noexcept {
        Bitboard r;
        for (int i = 0; i < word_count; ++i) {
            r.words_[static_cast<unsigned>(i)] =
                words_[static_cast<unsigned>(i)] & ~o.words_[static_cast<unsigned>(i)];
        }
        return r;
    }

    [[nodiscard]] friend constexpr bool operator==(const Bitboard& a, const Bitboard& b) noexcept {
        return a.words_ == b.words_;
    }

    /// Desplaza hacia `y+1`. ver docs/rules.md#r-01
    [[nodiscard]] constexpr Bitboard north() const noexcept { return shifted_up(W); }

    /// Desplaza hacia `y-1`.
    [[nodiscard]] constexpr Bitboard south() const noexcept { return shifted_down(W); }

    /// Desplaza hacia `x+1`, descartando la columna derecha para no envolver.
    [[nodiscard]] constexpr Bitboard east() const noexcept {
        return without(column(W - 1)).shifted_up(1);
    }

    /// Desplaza hacia `x-1`, descartando la columna izquierda.
    [[nodiscard]] constexpr Bitboard west() const noexcept {
        return without(column(0)).shifted_down(1);
    }

    /// Dilatacion ortogonal: la casilla y sus 4 vecinas. Es el nucleo del flood fill.
    [[nodiscard]] constexpr Bitboard expand() const noexcept {
        return *this | north() | south() | east() | west();
    }

    /// Mascara de una columna completa.
    [[nodiscard]] static constexpr Bitboard column(int x) noexcept {
        Bitboard r;
        for (int y = 0; y < H; ++y) {
            r.set(index_of(x, y));
        }
        return r;
    }

    /// Mascara de una fila completa.
    [[nodiscard]] static constexpr Bitboard row(int y) noexcept {
        Bitboard r;
        for (int x = 0; x < W; ++x) {
            r.set(index_of(x, y));
        }
        return r;
    }

    /// Todas las casillas del tablero.
    [[nodiscard]] static constexpr Bitboard full() noexcept {
        Bitboard r;
        for (int i = 0; i < word_count; ++i) {
            r.words_[static_cast<unsigned>(i)] = ~Word{0};
        }
        r.trim();
        return r;
    }

    /// Rectangulo inclusivo. Lo usa el modelo de hazards de royale.
    /// ver docs/rules.md#r-09
    [[nodiscard]] static constexpr Bitboard rect(int min_x, int min_y, int max_x,
                                                 int max_y) noexcept {
        Bitboard r;
        for (int y = min_y; y <= max_y; ++y) {
            if (y < 0 || y >= H) continue;
            for (int x = min_x; x <= max_x; ++x) {
                if (x < 0 || x >= W) continue;
                r.set(index_of(x, y));
            }
        }
        return r;
    }

private:
    constexpr void trim() noexcept {
        constexpr int used = cells % 64;
        if constexpr (used != 0) {
            words_[word_count - 1] &= (Word{1} << used) - 1;
        }
    }

    /// Desplazamiento hacia indices mayores. `n` menor que 64 por el static_assert de W.
    [[nodiscard]] constexpr Bitboard shifted_up(int n) const noexcept {
        Bitboard r;
        const unsigned shift = static_cast<unsigned>(n);
        const unsigned inv = 64U - shift;
        for (int i = word_count - 1; i >= 0; --i) {
            Word v = words_[static_cast<unsigned>(i)] << shift;
            if (i > 0) v |= words_[static_cast<unsigned>(i - 1)] >> inv;
            r.words_[static_cast<unsigned>(i)] = v;
        }
        r.trim();
        return r;
    }

    /// Desplazamiento hacia indices menores.
    [[nodiscard]] constexpr Bitboard shifted_down(int n) const noexcept {
        Bitboard r;
        const unsigned shift = static_cast<unsigned>(n);
        const unsigned inv = 64U - shift;
        for (int i = 0; i < word_count; ++i) {
            Word v = words_[static_cast<unsigned>(i)] >> shift;
            if (i + 1 < word_count) v |= words_[static_cast<unsigned>(i + 1)] << inv;
            r.words_[static_cast<unsigned>(i)] = v;
        }
        r.trim();
        return r;
    }

    std::array<Word, word_count> words_{};
};

/// Alias por defecto del formato objetivo. ver docs/rules.md#r-20
using Board11 = Bitboard<11, 11>;
using Board7 = Bitboard<7, 7>;
using Board19 = Bitboard<19, 19>;

} // namespace engine
