#pragma once

/// @file brain.hpp
/// Interfaz unica del cerebro. La usan identicamente el servidor HTTP y la arena
/// in-process, para que lo que se mide en la arena sea lo que juega en produccion.
/// ver docs/architecture.md#a-02

#include <engine/state.hpp>
#include <engine/types.hpp>

#include <snake/deadline.hpp>
#include <snake/params.hpp>

namespace snake {

/// Resultado de una decision, con lo necesario para la linea de log por movimiento.
struct Move {
    engine::Direction direction{engine::Direction::up};
    /// Escalon del fail-safe que produjo el movimiento. 0 = decision normal.
    /// ver docs/rules.md#r-03 y el contrato de degradacion de snake/CLAUDE.md
    int fallback_level{0};
    /// Valor estimado de la posicion resultante, para el log.
    double score{0.0};
    /// Movimientos candidatos evaluados.
    int considered{0};
};

/// Decide un movimiento. NUNCA lanza, NUNCA excede el deadline y NUNCA devuelve una
/// direccion inmediatamente mortal si existe alguna que no lo sea.
/// ver docs/invariants.md#inv-10 y docs/invariants.md#inv-11
Move decide(const engine::State11& state, Deadline deadline, const Params& params) noexcept;

/// Version degradada segura para variantes no soportadas (wrapped, constrictor):
/// sin tail-escape ni modelo de hazards, solo filtro duro y flood fill conservador.
/// ver docs/rules-parametros.md#r-13
Move decide_degraded(const engine::State11& state,
                     Deadline deadline,
                     const Params& params) noexcept;

} // namespace snake
