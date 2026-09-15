#pragma once

/// @file voronoi.hpp
/// [FASE 5 - v1] Control de territorio por BFS simultaneo desde todas las cabezas,
/// con hazards ponderados a la baja.
///
/// Stub: la interfaz esta fijada para que la heuristica v1 no cambie la firma del
/// evaluador. Su test vive con la etiqueta [.pending].
/// ver docs/strategy.md#s-v1

#include <array>
#include <cstddef>
#include <stdexcept>

#include <engine/state.hpp>

namespace snake::eval {

/// Casillas que cada serpiente alcanza antes que las demas.
template <int MaxSnakes> struct Territory {
    std::array<int, static_cast<std::size_t>(MaxSnakes)> cells{};
    int contested{0};
};

/// Reparto del tablero por distancia minima desde cada cabeza, resolviendo los empates
/// a favor de la serpiente mas larga.
template <int W, int H, int MaxSnakes>
[[nodiscard]] Territory<MaxSnakes> voronoi(const engine::GameState<W, H, MaxSnakes>& state) {
    (void)state;
    throw std::logic_error("no implementado: fase 5");
}

} // namespace snake::eval
