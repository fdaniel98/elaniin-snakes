#pragma once

/// @file features.hpp
/// [FASE 5 - v1] Vector de caracteristicas de una posicion, el que consumira la
/// evaluacion lineal y, mas adelante, el tuning automatico.
///
/// Stub: la interfaz esta fijada; su test vive con la etiqueta [.pending].
/// ver docs/strategy.md#s-v1

#include <stdexcept>

#include <engine/state.hpp>

namespace snake::eval {

/// Caracteristicas de una posicion desde el punto de vista de una serpiente.
struct Features {
    double space_ratio{0.0};
    double length_advantage{0.0};
    double health{0.0};
    double distance_to_food{0.0};
    double hazard_pressure{0.0};
    double turns_to_next_shrink{0.0};
    double articulation_risk{0.0};
};

/// Extrae las caracteristicas de `state` para la serpiente `id`.
template <int W, int H, int MaxSnakes>
[[nodiscard]] Features features(const engine::GameState<W, H, MaxSnakes>& state,
                                engine::SnakeId id) {
    (void)state;
    (void)id;
    throw std::logic_error("no implementado: fase 5");
}

} // namespace snake::eval
