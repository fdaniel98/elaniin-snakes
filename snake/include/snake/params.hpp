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
    /// Latencia de ida y vuelta que se le reserva a la red. **Medida**, no estimada: 57 ms
    /// es el p99 de transporte que reporto el ARBITRO en partida real contra `us-east1`
    /// (207 de total menos los 150 de computo), y 80 le deja un 40% de holgura. Estuvo en
    /// 260 unas horas porque lo medi con `curl`, que abre un handshake TLS por peticion
    /// mientras el arbitro reusa la conexion: 4.5 veces de mas.
    ///
    /// A `timeout` 500 este numero no hace nada -manda `max_compute_ms`-; existe para que
    /// un timeout anunciado mas corto no colapse el presupuesto. Con 260, un `timeout` de
    /// 300 daba 300-260-50 = -10, que `Deadline::from_timeout` acota a 1 ms: la snake
    /// jugaria por ordenacion estatica, sin buscar.
    /// ver docs/decisions/ADR-0037-margenes-medidos.md#d-0376
    std::int32_t network_margin_ms = 80;
    std::int32_t safety_margin_ms = 50;
    /// Techo duro de computo aunque el timeout anunciado sea mayor.
    ///
    /// 150 y no 200 desde que se midio el RTT real. Lo que cuesta es ~1 nivel de
    /// profundidad, y la profundidad esta medida: de 6 a 12 niveles movio -0.0167 con el
    /// IC95 cruzando el cero (ver docs/experimentos.md#s-busq-r). Lo que compra son 50 ms
    /// de colchon contra un timeout, que no es jugar peor sino que el arbitro aplique el
    /// movimiento por defecto, que suele matar.
    /// ver docs/decisions/ADR-0037-margenes-medidos.md#d-0371
    std::int32_t max_compute_ms = 150;
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
    /// Peso del espacio que quedaria si el rival tapase el peor cuello de la region.
    /// 0 lo apaga. El flood fill ve el hueco de ahora; esto ve la sala con una sola
    /// puerta. ver docs/strategy.md#s-v1
    double worst_case_weight = 0.0;
    /// Casillas candidatas a cuello que se prueban como maximo, por movimiento.
    std::int32_t worst_case_max_cuellos = 24;
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

/// Control de territorio (v1): espacio que se alcanza ANTES que el rival, no espacio que
/// existe. ver docs/strategy.md#s-v1
struct TerritoryParams {
    /// 0 = flood fill a secas. 1 = reparto de Voronoi.
    ///
    /// Por defecto 1 desde que v4 gano su A/B (-0.3667, IC95 [-0.659, -0.074], MEJORA).
    /// Como decision de UN TURNO esto se midio y se rechazo; lo que funciona es en las
    /// HOJAS de la busqueda. ver docs/experimentos.md#s-hojas-r
    /// v0 se conserva entero y seleccionable en `snake/config/v0-baseline.json`.
    std::int32_t version = 1;
    /// Peso del territorio propio, normalizado por el tamaño del tablero.
    double weight = 120.0;
    /// Penalizacion por casilla disputada adyacente: son las que matan a dos.
    double contested_weight = 0.0;
    /// Lo que vale una casilla con hazard frente a una limpia, en porcentaje.
    std::int32_t hazard_value_pct = 50;
};

/// Config completo del cerebro.
/// [v6] La salud medida en TURNOS DE VIDA, no en puntos.
/// ver docs/decisions/ADR-0028-turnos-de-supervivencia.md
struct SurvivalParams {
    /// 0 = salud absoluta (la unidad de v0..v5). 1 = turnos de vida restantes.
    std::int32_t version = 0;
    /// Margen de vida a partir del cual tener mas deja de aportar. Con 1 de coste por
    /// turno esto son 25 de salud; dentro de un hazard de 14, son 375, o sea que dentro
    /// del hazard NUNCA se esta comodo, que es exactamente lo correcto.
    std::int32_t safe_turns = 25;
    /// Peso del margen de vida.
    double weight = 25.0;
    /// Por debajo de estos turnos se busca comida, venga o no de la ventaja de longitud.
    /// En tablero limpio son 15 de salud; dentro de un hazard de 14, salud 100 da 6.7
    /// turnos, asi que ahi se busca SIEMPRE. Es el reloj real de Royale.
    std::int32_t seek_below_turns = 15;
    /// Por debajo de estos turnos la posicion es critica y el castigo crece.
    std::int32_t critical_turns = 5;
    /// Cuanto castiga estar al borde. Es continuo en los turnos restantes, no un escalon:
    /// el escalon de v5 solo se activaba con <= 2 turnos de vida, cuando ya no daba tiempo
    /// ni a salir del hazard.
    double panic_weight = 120.0;
};

/// [v5] Control de longitud. ver docs/decisions/ADR-0026-control-de-longitud.md
struct LengthParams {
    /// 0 = la politica de v0 (comer solo con hambre, longitud con peso simbolico).
    /// 1 = ventaja de longitud como termino de primera clase.
    ///
    /// Por defecto 1 desde que v5 gano su A/B: -0.6917 de puesto contra v4, IC95
    /// [-1.012, -0.371], 26 primeros puestos de 60 y ningun cuarto. Y el mecanismo se
    /// confirmo: la ventaja de longitud media paso de negativa en 49 de 60 partidas a
    /// +0.36, y terminamos siendo los mas largos en 41 de 60 contra 15.
    /// ver docs/experimentos.md#s-longitud-r
    std::int32_t version = 1;
    /// Peso de la VENTAJA de longitud sobre el rival mas largo. Lo que decide un cabezazo
    /// no es ser largo, es ser mas largo: en 47 de 60 partidas medidas moriamos siendo
    /// iguales o mas cortos que todos los vivos.
    /// ver docs/experimentos.md#s-longitud
    double advantage_weight = 60.0;
    /// Ventaja a partir de la cual crecer deja de valer. Un cuerpo enorme tambien encierra,
    /// asi que el termino satura en vez de crecer sin fin.
    std::int32_t target_lead = 3;
    /// Cuanto pesa la cercania a la comida cuando se va por detras en longitud. Es lo que
    /// convierte "no comer por comer" en "comer cuando el arbol dice que sale a cuenta".
    double hunt_weight = 10.0;
};

/// [v2] Busqueda. ver docs/decisions/ADR-0022-busqueda-paranoica.md
struct SearchParams {
    /// 0 = decision de un turno (v0). 1 = busqueda con profundizacion iterativa.
    ///
    /// Por defecto 1: la busqueda gana su A/B contra v0 en las tres mediciones que se le
    /// han hecho. ver docs/experimentos.md#exp-resumen
    std::int32_t version = 1;
    /// Techo de profundidad. Es un tope de SEGURIDAD, no un objetivo: quien manda es el
    /// deadline, y se devuelve la mejor jugada de la ultima profundidad COMPLETADA.
    ///
    /// 64 y no 8 porque 8 era un freno de mano. Medido sobre 40 posiciones por escenario
    /// con presupuesto de 200 ms (`tools/sonda_tope.cpp`):
    ///
    ///   vivas | tope 8            | tope 64
    ///   ------|-------------------|------------------
    ///     2   | 8.00, 42 ms usados| 12.18, 188 ms
    ///     3   | 5.95              |  7.47
    ///     4   | 6.03              | 12.08
    ///
    /// Con 2 vivas -el final que decide el 1o contra el 2o- las 39 posiciones tocaban el
    /// tope y se devolvian 158 de los 200 ms sin usar. El coste en tiempo de subirlo es
    /// CERO: el deadline sigue siendo quien corta. La pila son 1096 B por estado y ~2.4 KB
    /// por nivel, o sea ~154 KB a profundidad 64, sobre los 8 MB de una pila normal.
    /// ver docs/decisions/ADR-0024-el-tope-de-profundidad.md
    std::int32_t max_depth = 64;
    /// Rivales que se simulan de verdad, los mas cercanos primero. Los demas repiten su
    /// ultimo movimiento. Cada rival simulado multiplica por ~3 el arbol, asi que este
    /// numero es el que decide si se llega a profundidad 4 o se queda en 2.
    std::int32_t max_rivals = 2;
    /// Valor de morir, en la escala de `evaluate`. Tiene que dominar cualquier otra cosa
    /// que la evaluacion pueda sumar: morir no se compensa con espacio ni con comida.
    double death_value = -100000.0;
    /// Valor de quedar el ultimo vivo.
    double win_value = 100000.0;
    /// Microsegundos que la busqueda se reserva ANTES del deadline de verdad.
    ///
    /// No es paranoia: detectar que se acabo el tiempo cuesta tiempo. Entre dos lecturas
    /// del reloj caben nodos, y desenrollar la recursion y volver tampoco es gratis. Sin
    /// reserva la sonda se pasaba entre 6 y 34 us del presupuesto, y INV-11 dice que no se
    /// excede el deadline, no que se excede poco. Con 350 ms de presupuesto esto es el
    /// 0.6%. ver docs/invariants.md#inv-11
    std::int32_t reserve_us = 2000;
    /// Cuanto vale sobrevivir un turno mas. Sin esto, la busqueda es indiferente entre
    /// morir en el turno 3 y morir en el turno 8, y prefiere la primera por llegar antes
    /// a una hoja con mas espacio.
    double survival_bonus = 30.0;
    /// Tope de nodos por movimiento. **0 = sin tope**, que es lo que corre en servidor:
    /// ahi manda el reloj y el presupuesto real lo fija `time.max_compute_ms`.
    ///
    /// La arena lo pone distinto de 0 porque una busqueda anytime cortada por reloj
    /// devuelve movimientos distintos segun la carga de la maquina, y entonces dos ramas
    /// de un A/B dejan de ser comparables aunque jueguen las mismas semillas. Con tope de
    /// nodos la busqueda es funcion del estado y nada mas.
    ///
    /// Se calibra contra el presupuesto de despliegue en la maquina de referencia y se
    /// re-calibra despues de cada cambio de rendimiento; un numero de nodos no es
    /// comparable entre commits que cambian el coste del nodo.
    /// ver docs/decisions/ADR-0030-presupuesto-por-nodos.md
    std::int32_t budget_nodes = 0;
};

struct Params {
    TimeParams time{};
    FoodParams food{};
    SpaceParams space{};
    HeadParams head{};
    HazardParams hazard{};
    TerritoryParams territory{};
    SearchParams search{};
    LengthParams length{};
    SurvivalParams survival{};
};

} // namespace snake
