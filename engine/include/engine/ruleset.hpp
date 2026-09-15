#pragma once

/// @file ruleset.hpp
/// Parametros del ruleset. Todos llegan en el request por una ruta JSON exacta
/// (ver docs/rules-parametros.md#r-20); los valores de abajo son *fallbacks*, no verdades.
/// Usar un fallback marca `used_fallback` y el servidor emite WARN (regla de oro 5).
///
/// El parser vive en `snake/src/config_loader.cpp` para que `engine/` no dependa
/// de ninguna libreria de JSON. ver docs/architecture.md#a-03

#include <cstdint>

namespace engine {

/// Variante de juego. ver docs/rules-parametros.md#r-13
enum class Variant : std::uint8_t {
    standard = 0,
    royale = 1,
    wrapped = 2,
    constrictor = 3,
    wrapped_constrictor = 4,
    solo = 5,
    unknown = 6,
};

/// Combinaciones que el cerebro soporta con todas sus heuristicas. El resto entra
/// en modo degradado seguro.
///
/// `solo` NO esta soportado a proposito: termina cuando no queda ninguna viva, mientras
/// que `is_terminal` corta con una. ver docs/rules-parametros.md#r-13
[[nodiscard]] constexpr bool is_supported(Variant v) noexcept {
    return v == Variant::standard || v == Variant::royale;
}

/// Que campos hubo que rellenar con el fallback en vez de leerlos del request.
///
/// Son `bool` sueltos y no bit-fields a proposito: el parser los pasa por referencia no
/// const, y a un bit-field no se le puede enlazar una referencia.
struct FallbackFlags {
    bool timeout = false;
    bool food_spawn_chance = false;
    bool minimum_food = false;
    bool hazard_damage = false;
    bool shrink_every_n_turns = false;
    bool variant = false;
    bool map_name = false;

    [[nodiscard]] constexpr bool any() const noexcept {
        return timeout || food_spawn_chance || minimum_food || hazard_damage ||
               shrink_every_n_turns || variant || map_name;
    }
};

/// Parametros efectivos de una partida.
///
/// Los defaults replican los del arbitro oficial (`cli/commands/play.go:97-117`),
/// salvo `shrink_every_n_turns`, donde motor (20) y arbitro (25) discrepan:
/// tomamos el del arbitro porque es quien genera las partidas que jugamos.
/// ver docs/rules.md#r-09
struct Ruleset {
    /// `game.timeout`, en milisegundos. Incluye latencia de red.
    std::int32_t timeout_ms = 500;
    /// `game.ruleset.settings.foodSpawnChance`, porcentaje 0..100.
    std::int32_t food_spawn_chance = 15;
    /// `game.ruleset.settings.minimumFood`.
    std::int32_t minimum_food = 1;
    /// `game.ruleset.settings.hazardDamagePerTurn`.
    std::int32_t hazard_damage_per_turn = 14;
    /// `game.ruleset.settings.royale.shrinkEveryNTurns`.
    std::int32_t shrink_every_n_turns = 25;
    /// `game.ruleset.name`.
    Variant variant = Variant::standard;
    /// `game.map`; solo se usa para decidir si hay hazards dinamicos.
    bool map_is_royale = false;

    FallbackFlags fallbacks{};
};

/// Constantes del motor que NO viajan en el request. ver docs/rules-parametros.md#r-21
inline constexpr int max_health = 100;
inline constexpr int start_length = 3;
inline constexpr int health_loss_per_turn = 1;

} // namespace engine
