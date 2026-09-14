#pragma once

/// @file rules.hpp
/// Reglas del juego. Toda la semantica esta derivada del codigo Go citado en
/// docs/rules.md; ninguna afirmacion de este archivo es original.
///
/// Instanciado explicitamente para 7x7, 11x11 y 19x19 en `engine/src/rules.cpp`.

#include <array>
#include <span>

#include <engine/state.hpp>
#include <engine/types.hpp>

namespace engine {

/// Orden final de la partida. Las serpientes eliminadas en el mismo turno comparten
/// **rango promediado** (dos simultaneas en 3o/4o dan 3.5 a ambas): el motor oficial
/// no les asigna orden, asi que desempatar por indice o por asiento seria inventarlo.
/// ver docs/rules.md#r-12
template <int MaxSnakes>
struct PlacementsT {
    std::array<float, MaxSnakes> rank{};
    int count{};
};

/// Movimiento que aplica el motor oficial cuando la respuesta es invalida o ausente:
/// repite el ultimo movimiento deducido de cabeza y cuello, y cae a `up` si no puede.
/// ver docs/rules.md#r-03
template <int W, int H, int MaxSnakes>
[[nodiscard]] Direction default_move(const GameState<W, H, MaxSnakes>& s, SnakeId id) noexcept;

/// Aplica un turno completo con movimientos **simultaneos** y muta `s` (copy-make).
///
/// Orden de fases identico al del pipeline oficial: fin de partida, movimiento,
/// hambre, daño de hazard, alimentacion, eliminacion. ver docs/rules.md#r-02
///
/// `apply` NO filtra direcciones: acepta la inmediatamente mortal. Filtrar aqui
/// impediria reproducir los logs del arbitro en el test diferencial.
/// ver docs/rules.md#r-03
///
/// `moves[i]` es el movimiento de la serpiente `i`. Las eliminadas se ignoran.
template <int W, int H, int MaxSnakes>
Status apply(GameState<W, H, MaxSnakes>& s, std::span<const Direction> moves) noexcept;

/// Direcciones que no son inmediatamente mortales: dentro del tablero y sin chocar
/// contra un segmento que seguira ocupado el proximo turno.
///
/// Es una AYUDA para el cerebro, no una regla: el motor acepta cualquier direccion.
/// La casilla de cola cuenta como libre salvo que los dos ultimos segmentos esten
/// apilados. ver docs/rules.md#r-04
template <int W, int H, int MaxSnakes>
[[nodiscard]] MoveMask legal_moves(const GameState<W, H, MaxSnakes>& s, SnakeId id) noexcept;

/// Cierto cuando queda una serpiente viva o ninguna. Se evalua al PRINCIPIO del turno.
/// ver docs/rules.md#r-12
template <int W, int H, int MaxSnakes>
[[nodiscard]] bool is_terminal(const GameState<W, H, MaxSnakes>& s) noexcept;

/// Orden final derivado del turno de eliminacion, con rango compartido promediado.
/// ver docs/rules.md#r-12
template <int W, int H, int MaxSnakes>
[[nodiscard]] PlacementsT<MaxSnakes> placements(const GameState<W, H, MaxSnakes>& s) noexcept;

/// Casillas de hazard de royale tras `turn` turnos, dadas la semilla de la partida y
/// `shrink_every_n_turns`.
///
/// [FASE 1] Solo tiene sentido en la arena in-process: el payload de `/move` no trae
/// la semilla, asi que en partida real el lado del proximo shrink no es conocible.
/// ver docs/rules.md#r-09
template <int W, int H>
[[nodiscard]] Bitboard<W, H> royale_hazards(std::uint64_t seed, int turn,
                                            int shrink_every_n_turns);

extern template Direction default_move<7, 7, 4>(const GameState<7, 7, 4>&, SnakeId) noexcept;
extern template Direction default_move<11, 11, 4>(const GameState<11, 11, 4>&, SnakeId) noexcept;
extern template Direction default_move<19, 19, 4>(const GameState<19, 19, 4>&, SnakeId) noexcept;

extern template Status apply<7, 7, 4>(GameState<7, 7, 4>&, std::span<const Direction>) noexcept;
extern template Status apply<11, 11, 4>(GameState<11, 11, 4>&, std::span<const Direction>) noexcept;
extern template Status apply<19, 19, 4>(GameState<19, 19, 4>&, std::span<const Direction>) noexcept;

extern template MoveMask legal_moves<7, 7, 4>(const GameState<7, 7, 4>&, SnakeId) noexcept;
extern template MoveMask legal_moves<11, 11, 4>(const GameState<11, 11, 4>&, SnakeId) noexcept;
extern template MoveMask legal_moves<19, 19, 4>(const GameState<19, 19, 4>&, SnakeId) noexcept;

extern template bool is_terminal<7, 7, 4>(const GameState<7, 7, 4>&) noexcept;
extern template bool is_terminal<11, 11, 4>(const GameState<11, 11, 4>&) noexcept;
extern template bool is_terminal<19, 19, 4>(const GameState<19, 19, 4>&) noexcept;

extern template PlacementsT<4> placements<7, 7, 4>(const GameState<7, 7, 4>&) noexcept;
extern template PlacementsT<4> placements<11, 11, 4>(const GameState<11, 11, 4>&) noexcept;
extern template PlacementsT<4> placements<19, 19, 4>(const GameState<19, 19, 4>&) noexcept;

extern template Bitboard<7, 7> royale_hazards<7, 7>(std::uint64_t, int, int);
extern template Bitboard<11, 11> royale_hazards<11, 11>(std::uint64_t, int, int);
extern template Bitboard<19, 19> royale_hazards<19, 19>(std::uint64_t, int, int);

} // namespace engine
