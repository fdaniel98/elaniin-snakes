#pragma once

/// @file arena.hpp
/// Self-play in-process: variantes compiladas del cerebro enfrentandose sin HTTP.
///
/// Lo que la arena compra es velocidad y reproducibilidad. Lo que NO compra, y conviene
/// tenerlo delante antes de leer un veredicto suyo: aqui solo juegan configuraciones
/// NUESTRAS. Las snakes del zoo son contenedores que hablan HTTP y su codigo no entra en
/// nuestro binario, asi que un resultado de arena es sobre self-play y no sobre el campo.
/// Nada entra en `default.json` por un veredicto de arena.
/// ver docs/decisions/ADR-0031-que-mide-la-arena.md#d-0311
///
/// El presupuesto es por NODOS y nunca por reloj: una busqueda anytime cortada por tiempo
/// devuelve movimientos distintos segun la carga de la maquina.
/// ver docs/decisions/ADR-0030-presupuesto-por-nodos.md#d-0301

#include <array>
#include <cstdint>
#include <span>

#include <engine/rules.hpp>
#include <engine/ruleset.hpp>
#include <engine/state.hpp>

#include <snake/params.hpp>

namespace arena {

using State = engine::State11;
inline constexpr int max_contendientes = engine::State11::max_snakes;

struct ArenaConfig {
    /// Semilla de la partida. **0 esta prohibido**: es el valor con el que el motor
    /// oficial cae a su generador global no reproducible, y aunque el nuestro si es
    /// determinista con 0, el Training Room no debe pasarlo.
    /// ver docs/rules-parametros.md#r-99
    std::uint64_t seed{1};
    /// Parametros del juego. `timeout_ms` no se usa: aqui el presupuesto es por nodos.
    engine::Ruleset rules{};
    /// Corte duro. Una partida de 11x11 con cuatro serpientes no llega ni de lejos; esto
    /// existe para que un bug no cuelgue un torneo de miles de partidas.
    int max_turns{3000};
};

/// Por que se acabo una partida.
enum class Final : std::uint8_t {
    ok,          ///< Termino sola: queda una viva o ninguna.
    tope_turnos, ///< Se alcanzo `max_turns`. La partida NO cuenta.
    invalida,    ///< Alguna busqueda la corto el reloj: el resultado depende de la maquina.
    /// Algun contendiente venia con `search.budget_nodes` en 0. No se juega siquiera: sin
    /// tope de nodos y con un deadline inalcanzable la busqueda iria hasta `max_depth`, y
    /// lo que saliera no seria reproducible ni terminaria en un tiempo util.
    sin_presupuesto,
    /// El ruleset no describe una partida jugable: hoy, cadencia de shrink menor que 1 en
    /// mapa royale. El motor oficial aborta la partida en ese caso
    /// (`maps/royale.go:50-52`), y `royale_hazards()` devuelve un tablero SIN hazards que
    /// el llamante no puede distinguir de un turno temprano. Callarse eso convertiria una
    /// partida sin zona de peligro en una medicion que parece normal.
    /// ver docs/rules.md#r-09
    reglas_invalidas,
};

struct Resultado {
    /// Puesto con rango compartido promediado. ver docs/rules.md#r-12
    float puesto{0.0F};
    int turnos_vividos{0};
    engine::Elimination causa{engine::Elimination::alive};
    /// Movimientos decididos y nodos gastados, para calibrar `budget_nodes`.
    int movimientos{0};
    long long nodos{0};
    int profundidad_max{0};
    /// Veces que la busqueda se quedo sin reloj antes que sin nodos. Con presupuesto por
    /// nodos esto tiene que ser 0, y si no lo es la partida sale `invalida`.
    int cortes_por_reloj{0};
};

struct Partida {
    Final final{Final::ok};
    int turnos{0};
    int contendientes{0};
    std::array<Resultado, max_contendientes> snakes{};
};

/// Juega una partida entera. Determinista: misma semilla y mismos `Params` dan la misma
/// partida, en cualquier maquina y bajo cualquier carga, siempre que
/// `search.budget_nodes` sea distinto de 0 en todos los contendientes.
///
/// El asiento `i` juega con `contendientes[i]`. Rotar los asientos entre partidas es
/// trabajo del llamante, no de aqui.
[[nodiscard]] Partida play(const ArenaConfig& cfg,
                           std::span<const snake::Params> contendientes) noexcept;

} // namespace arena
