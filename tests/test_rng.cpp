/// @file test_rng.cpp
/// Vectores de referencia de xoshiro256++. Los del estado {1,2,3,4} son los de la
/// implementacion de referencia en C de Blackman y Vigna; los sembrados con
/// splitmix64 se generaron con una implementacion independiente en Python durante el
/// bloque A y se commitean como regresion: si cambian, la arena deja de ser
/// reproducible. ver docs/invariants.md#inv-08

#include <array>
#include <cstdint>

#include <catch2/catch_test_macros.hpp>

#include <engine/rng.hpp>

TEST_CASE("rng: vectores de referencia con estado {1,2,3,4}", "[rng]") {
    engine::Rng rng(1, 2, 3, 4);
    REQUIRE(rng.next() == 0x2800001ULL);
    REQUIRE(rng.next() == 0x3800067ULL);
    REQUIRE(rng.next() == 0xcc00003800067ULL);
    REQUIRE(rng.next() == 0xcc201994400b2ULL);
    REQUIRE(rng.next() == 0x8012a2019ac433cdULL);
}

TEST_CASE("rng: splitmix64 expande la semilla 0 a un estado no degenerado", "[rng]") {
    std::uint64_t state = 0;
    REQUIRE(engine::splitmix64(state) == 0xe220a8397b1dcdafULL);
    REQUIRE(engine::splitmix64(state) == 0x6e789e6aa1b965f4ULL);
    REQUIRE(engine::splitmix64(state) == 0x06c45d188009454fULL);
    REQUIRE(engine::splitmix64(state) == 0xf88bb8a8724c81ecULL);
}

TEST_CASE("rng: sembrado por semilla, vectores fijos", "[rng]") {
    engine::Rng zero(0);
    REQUIRE(zero.next() == 0x53175d61490b23dfULL);
    REQUIRE(zero.next() == 0x61da6f3dc380d507ULL);
    REQUIRE(zero.next() == 0x5c0fdf91ec9a7bfcULL);

    engine::Rng answer(42);
    REQUIRE(answer.next() == 0xd0764d4f4476689fULL);
    REQUIRE(answer.next() == 0x519e4174576f3791ULL);
    REQUIRE(answer.next() == 0xfbe07cfb0c24ed8cULL);
}

TEST_CASE("rng: bounded respeta el rango y cubre todos los valores", "[rng]") {
    engine::Rng rng(7);
    REQUIRE(rng.bounded(0) == 0);
    REQUIRE(rng.bounded(1) == 0);

    constexpr std::uint64_t bound = 6;
    std::array<int, bound> hits{};
    for (int i = 0; i < 20000; ++i) {
        const std::uint64_t v = rng.bounded(bound);
        REQUIRE(v < bound);
        ++hits[static_cast<unsigned>(v)];
    }
    for (const int count : hits) {
        REQUIRE(count > 0);
    }
}

TEST_CASE("rng: shuffle es una permutacion y es determinista", "[rng]") {
    std::array<int, 8> a{0, 1, 2, 3, 4, 5, 6, 7};
    std::array<int, 8> b = a;

    engine::Rng rng_a(123);
    engine::Rng rng_b(123);
    rng_a.shuffle(a.data(), a.size());
    rng_b.shuffle(b.data(), b.size());
    REQUIRE(a == b);

    std::array<bool, 8> seen{};
    for (const int v : a) {
        REQUIRE(v >= 0);
        REQUIRE(v < 8);
        REQUIRE_FALSE(seen[static_cast<unsigned>(v)]);
        seen[static_cast<unsigned>(v)] = true;
    }

    engine::Rng rng_c(124);
    std::array<int, 8> c{0, 1, 2, 3, 4, 5, 6, 7};
    rng_c.shuffle(c.data(), c.size());
    REQUIRE(c != a);
}
