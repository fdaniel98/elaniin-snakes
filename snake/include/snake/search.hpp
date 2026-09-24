#pragma once

/// @file search.hpp
/// [v2] Busqueda con profundizacion iterativa. La primera vez que esta snake mira mas
/// alla del turno actual.
///
/// Por que existe, con el numero delante: `brain_v0` decide en **~100 us** sobre un
/// presupuesto de **350 ms**. Gasta el 0.03% del tiempo que tiene. Mientras tanto Hobbs y
/// Devin simulan, y las dos evaluaciones estaticas que hemos medido -Voronoi
/// (ver docs/experimentos.md#s-v1r) y cuellos (ver docs/experimentos.md#s-cuellos-r)- dieron NO
/// CONCLUYENTE contra ellos. Dos veces el mismo diagnostico: contra un rival que busca,
/// una heuristica mejor no basta. Lo que falta no es evaluacion, es profundidad.
///
/// Modelo: **paranoico**. Elegimos el movimiento que maximiza nuestra evaluacion
/// suponiendo que los rivales eligen el conjunto que mas nos perjudica. Es pesimista a
/// proposito -de verdad no coordinan- y ese sesgo esta declarado en
/// ver docs/decisions/ADR-0022-busqueda-paranoica.md, junto con lo que cuesta.
///
/// El juego es de movimientos SIMULTANEOS y esto no lo modela: nosotros elegimos primero y
/// los rivales responden viendo nuestra eleccion, asi que les regalamos informacion que no
/// tienen. Eso hace la busqueda mas cobarde que la realidad, nunca mas temeraria, y por
/// eso es un primer paso aceptable. SM-MCTS/DUCT, que si lo modela, queda para despues.
///
/// Garantias que NO cambian respecto a v0: nunca devuelve un movimiento ilegal si existe
/// uno legal, nunca excede el deadline -se corta entre nodos y se devuelve la mejor
/// jugada de la ultima profundidad COMPLETADA-, y nunca lanza.
/// ver docs/invariants.md#inv-10 y docs/invariants.md#inv-11

#include <engine/state.hpp>
#include <engine/types.hpp>

#include <snake/deadline.hpp>
#include <snake/params.hpp>

namespace snake {

struct SearchResult {
    engine::Direction best{engine::Direction::up};
    double score{0.0};
    /// Ultima profundidad COMPLETADA. 0 = no dio tiempo ni a una, y `best` viene de la
    /// ordenacion estatica.
    int depth{0};
    long long nodes{0};
    /// [v16] Veces que la tabla de transposicion ahorro un subarbol entero. 0 si esta
    /// apagada. ver docs/strategy.md#s-tabla-duelo
    long long tt_hits{0};
    /// Rivales que se simularon de verdad (los lejanos van con movimiento fijo).
    int rivals_simulated{0};
    /// Ultima profundidad en la que el mejor movimiento CAMBIO. Si es 2 y `depth` es 8,
    /// las seis ultimas profundidades confirmaron lo que ya se sabia: ahi hay tiempo que
    /// se puede devolver. ver docs/decisions/ADR-0023-cuando-parar-de-buscar.md
    int last_change_depth{0};
    /// Cierto si quien corto la busqueda fue el RELOJ y no el presupuesto de nodos.
    /// Con `search.budget_nodes` puesto, esto en cierto significa que la medicion depende
    /// de la carga de la maquina: la arena aborta.
    /// ver docs/decisions/ADR-0030-presupuesto-por-nodos.md#d-0301
    bool corto_el_reloj{false};
};

/// Evalua una posicion entera desde el punto de vista de `us`. Es la evaluacion de v0
/// reordenada para puntuar un ESTADO en vez de un movimiento candidato, que es lo que una
/// busqueda necesita en las hojas.
[[nodiscard]] double
evaluate(const engine::State11& state, engine::SnakeId us, const Params& params) noexcept;

/// Busca hasta que se acabe el deadline o la profundidad maxima del config.
[[nodiscard]] SearchResult
search(const engine::State11& state, Deadline deadline, const Params& params) noexcept;

} // namespace snake
