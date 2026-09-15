#pragma once

/// @file rng.hpp
/// xoshiro256++ propio, sembrado con splitmix64.
///
/// Motivo de no usar la STL: `std::uniform_int_distribution`, `std::shuffle`,
/// `std::sample` y `std::random_device` no tienen algoritmo especificado y difieren
/// entre libstdc++ y libc++, asi que romperian la reproducibilidad de la arena.
/// El gate falla si aparecen bajo engine/ o arena/ (check 5).
///
/// El motor no tiene estado global de RNG: toda aleatoriedad recibe un `Rng&`.
/// ver docs/invariants.md#inv-08

#include <array>
#include <cstddef>
#include <cstdint>

namespace engine {

/// splitmix64, usado solo para expandir una semilla de 64 bits al estado de 256.
[[nodiscard]] constexpr std::uint64_t splitmix64(std::uint64_t& state) noexcept {
    state += 0x9E3779B97F4A7C15ULL;
    std::uint64_t z = state;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

class Rng {
public:
    /// Semilla cero incluida: splitmix64 la expande a un estado no degenerado.
    constexpr explicit Rng(std::uint64_t seed) noexcept {
        std::uint64_t sm = seed;
        for (std::uint64_t& word : state_) {
            word = splitmix64(sm);
        }
    }

    /// Construccion desde un estado explicito, para los vectores de test de referencia.
    constexpr Rng(std::uint64_t s0, std::uint64_t s1, std::uint64_t s2, std::uint64_t s3) noexcept
        : state_{s0, s1, s2, s3} {}

    /// xoshiro256++: `rotl(s[0] + s[3], 23) + s[0]`.
    constexpr std::uint64_t next() noexcept {
        const std::uint64_t result = rotl(state_[0] + state_[3], 23) + state_[0];
        const std::uint64_t t = state_[1] << 17U;

        state_[2] ^= state_[0];
        state_[3] ^= state_[1];
        state_[1] ^= state_[2];
        state_[0] ^= state_[3];
        state_[2] ^= t;
        state_[3] = rotl(state_[3], 45);

        return result;
    }

    /// Entero uniforme en [0, bound) por reduccion de Lemire con rechazo.
    /// Sesgo cero y una sola division en el caso peor.
    constexpr std::uint64_t bounded(std::uint64_t bound) noexcept {
        if (bound <= 1) {
            return 0;
        }
        const std::uint64_t threshold = (~bound + 1U) % bound; // 2^64 mod bound
        while (true) {
            const std::uint64_t r = next();
            if (r >= threshold) {
                return r % bound;
            }
        }
    }

    /// Baraja de Fisher-Yates hacia atras, determinista y sin dependencias de la STL.
    template <typename T> constexpr void shuffle(T* data, std::size_t count) noexcept {
        for (std::size_t i = count; i > 1; --i) {
            const std::size_t j = static_cast<std::size_t>(bounded(i));
            T tmp = data[i - 1];
            data[i - 1] = data[j];
            data[j] = tmp;
        }
    }

    [[nodiscard]] constexpr std::uint64_t state(int i) const noexcept {
        return state_[static_cast<unsigned>(i)];
    }

private:
    [[nodiscard]] static constexpr std::uint64_t rotl(std::uint64_t x, unsigned k) noexcept {
        return (x << k) | (x >> (64U - k));
    }

    std::array<std::uint64_t, 4> state_{};
};

} // namespace engine
