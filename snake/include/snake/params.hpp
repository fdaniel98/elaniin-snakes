#pragma once

/// @file params.hpp
/// Parametros de estrategia y del time manager. La estructura es 1:1 con
/// `snake/config/default.json`; `tests/test_ruleset_parse.cpp` falla si divergen.
///
/// Los margenes de tiempo son normativos: cambiarlos exige aprobacion humana y un ADR.
/// ver docs/performance.md#p-01

#include <cstdint>

namespace snake {

/// Presupuesto de latencia. `deadline = timeout - network_margin - safety_margin`.
/// El timeout del request INCLUYE la latencia de red.
struct TimeParams {
    std::int32_t network_margin_ms = 100;
    std::int32_t safety_margin_ms = 50;
    /// Techo duro de computo aunque el timeout anunciado sea mayor.
    std::int32_t max_compute_ms = 350;
};

/// Cuando merece la pena ir a por comida. No se come por comer.
struct FoodParams {
    /// Se busca comida si la salud baja de aqui.
    std::int32_t seek_below = 50;
    /// Umbral mas alto dentro de hazard: perder salud ahi cuesta el doble.
    std::int32_t seek_below_in_hazard = 75;
    /// Se acepta comida gratis si esta a esta distancia o menos y no hay riesgo.
    std::int32_t free_food_distance = 2;
    /// Peso de la cercania a la comida cuando se esta buscando.
    double weight = 6.0;
};

/// Control de espacio: el flood fill manda sobre todo lo demas.
struct SpaceParams {
    /// Se rechaza un movimiento cuyo espacio alcanzable sea menor que
    /// `longitud_propia * ratio`, salvo que todos lo sean.
    double min_space_ratio = 1.0;
    /// Peso del espacio alcanzable normalizado por el tamaño del tablero.
    double weight = 100.0;
    /// La cola propia cuenta como alcanzable si se llega en al menos estos turnos.
    bool tail_escape = true;
};

/// Zona de cabeza: casillas adyacentes a cabezas rivales.
struct HeadParams {
    /// Penalizacion por quedar adyacente a la cabeza de una serpiente igual o mas larga.
    double avoid_equal_or_longer = 80.0;
    /// Bonus por quedar adyacente a la cabeza de una estrictamente mas corta.
    double prefer_shorter = 8.0;
};

/// Hazards: terminar el turno dentro cuesta `hazardDamagePerTurn` extra.
struct HazardParams {
    /// Penalizacion base por terminar el turno en hazard.
    double weight = 20.0;
    /// Multiplicador cuando la salud restante no cubre varios turnos de daño.
    double low_health_multiplier = 4.0;
};

/// Config completo del cerebro.
struct Params {
    TimeParams time{};
    FoodParams food{};
    SpaceParams space{};
    HeadParams head{};
    HazardParams hazard{};
};

} // namespace snake
