/// @file test_differential.cpp
/// [FASE 1] Replay de los JSONL del arbitro oficial: se reproduce cada partida turno a
/// turno inyectando la comida y los hazards del log (el RNG del motor es `math/rand`
/// de Go y no lo reproducimos desde C++, ver docs/rules.md#r-99) y se exige estado
/// identico, incluido el movimiento por defecto ante respuestas invalidas.
///
/// Registrado con la etiqueta [.pending]: no corre por defecto, asi que `ctest` pasa en
/// verde y el pendiente queda VISIBLE en la lista de tests, no invisible.

#include <stdexcept>

#include <catch2/catch_test_macros.hpp>

#include <engine/rules.hpp>

namespace {

/// Carga un JSONL del CLI y reproduce la partida. Pendiente de la fase 1.
void replay_jsonl(const char* path) {
    (void)path;
    throw std::logic_error("no implementado: fase 1");
}

} // namespace

TEST_CASE("diferencial: replay de un JSONL del arbitro oficial", "[.pending][differential]") {
    REQUIRE_THROWS_AS(replay_jsonl("docs/results/ejemplo.jsonl"), std::logic_error);
}

TEST_CASE("diferencial: 500 partidas sin divergencia", "[.pending][differential]") {
    REQUIRE_THROWS_AS(replay_jsonl("docs/results/gauntlet.jsonl"), std::logic_error);
}
