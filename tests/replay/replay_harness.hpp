#pragma once

/// @file replay_harness.hpp
/// Reproduce una partida del arbitro oficial contra el motor propio, turno a turno.
///
/// El JSONL del arbitro no trae los movimientos y la serpiente que muere desaparece del
/// turno siguiente, asi que los movimientos salen del log del proxy grabador
/// (`tests/replay/recorder.py`) y no de la diferencia de cabezas.
///
/// Del log crudo al movimiento efectivo hay un paso que es una REGLA y no una
/// conveniencia: ante timeout, codigo HTTP distinto de 200, JSON invalido o direccion
/// desconocida, el arbitro reenvia el `LastMove` anterior, que arranca en `up`.
/// ver docs/rules.md#r-03
///
/// La comida y los hazards se INYECTAN del log: su generacion depende del `math/rand`
/// de Go, que no reproducimos. ver docs/rules-parametros.md#r-99

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <engine/rules.hpp>
#include <engine/state.hpp>

#include <nlohmann/json.hpp>

namespace replay {

using nlohmann::json;

/// Una linea del log del proxy grabador.
struct RespuestaCruda {
    int turn{};
    std::string snake;
    int status{};
    double elapsed_ms{};
    int timeout{500};
    std::string body;
};

/// Lo que el motor propio y el arbitro no comparten en un turno.
struct Divergencia {
    int turn{};
    std::string snake;
    std::string detalle;
};

/// Resultado de un replay. Los contadores existen para que un replay que no compara
/// nada no pueda pasar por un replay limpio.
struct Resultado {
    std::vector<Divergencia> divergencias;
    int turnos_comparados{};
    int serpientes_comparadas{};
    int movimientos_por_defecto{};
    /// Eliminaciones por causa, indexadas por `engine::Elimination`. No es una
    /// comprobacion: es COBERTURA. Un corpus que nunca mata a nadie de hambre no
    /// verifica el hambre, y sin este contador eso no se ve.
    std::array<int, 7> causas{};
    /// Turnos con dos o mas serpientes eliminadas a la vez en la misma casilla por
    /// cabeza a cabeza: el empate de longitudes. ver docs/rules.md#r-08
    int empates_cabeza{};
    /// Turnos con dos o mas eliminaciones simultaneas, de la causa que sea: es el
    /// unico caso donde `placements()` tiene que repartir rango compartido.
    /// ver docs/rules.md#r-12
    int muertes_simultaneas{};
};

struct Partida {
    std::string id;
    json cabecera;
    std::vector<json> turnos;
    std::map<std::string, std::vector<RespuestaCruda>> por_snake;
};

[[nodiscard]] inline std::vector<std::string> lineas_de(const std::string& ruta) {
    std::vector<std::string> salida;
    std::ifstream fichero(ruta);
    std::string linea;
    while (std::getline(fichero, linea)) {
        if (!linea.empty()) {
            salida.push_back(linea);
        }
    }
    return salida;
}

/// Carga el JSONL del arbitro y el log del grabador. Devuelve `nullopt` si el JSONL no
/// tiene la forma que documenta `cli/commands/output.go`: cabecera, N turnos, resultado.
[[nodiscard]] inline std::optional<Partida> cargar(const std::string& jsonl,
                                                   const std::string& moves) {
    const auto crudas = lineas_de(jsonl);
    if (crudas.size() < 3) {
        return std::nullopt;
    }

    Partida partida;
    partida.cabecera = json::parse(crudas.front(), nullptr, false);
    if (partida.cabecera.is_discarded()) {
        return std::nullopt;
    }
    partida.id = partida.cabecera.value("id", std::string{});

    for (std::size_t i = 1; i + 1 < crudas.size(); ++i) {
        json turno = json::parse(crudas[i], nullptr, false);
        if (turno.is_discarded() || !turno.contains("board")) {
            return std::nullopt;
        }
        partida.turnos.push_back(std::move(turno));
    }

    for (const auto& linea : lineas_de(moves)) {
        const json registro = json::parse(linea, nullptr, false);
        if (registro.is_discarded() || !registro.contains("snake")) {
            continue;
        }
        if (registro.value("game", std::string{}) != partida.id) {
            continue;
        }
        RespuestaCruda respuesta;
        respuesta.turn = registro.value("turn", -1);
        respuesta.snake = registro.value("snake", std::string{});
        respuesta.status = registro.value("status", 0);
        respuesta.elapsed_ms = registro.value("elapsed_ms", 0.0);
        respuesta.timeout = registro.value("timeout", 500);
        respuesta.body = registro.value("body", std::string{});
        partida.por_snake[respuesta.snake].push_back(respuesta);
    }

    return partida;
}

[[nodiscard]] inline std::optional<engine::Direction> direccion_de(const std::string& nombre) {
    if (nombre == "up") {
        return engine::Direction::up;
    }
    if (nombre == "down") {
        return engine::Direction::down;
    }
    if (nombre == "left") {
        return engine::Direction::left;
    }
    if (nombre == "right") {
        return engine::Direction::right;
    }
    return std::nullopt;
}

/// Movimiento que el arbitro habria aplicado, o `nullopt` si la respuesta no le sirvio.
///
/// Reproduce `getSnakeUpdate` (`cli/commands/play.go:455-513`): status distinto de 200,
/// JSON no parseable o `move` fuera de las cuatro literales dejan `LastMove` intacto.
/// El timeout del cliente HTTP (`cli/commands/play.go:129-135`) se reconstruye del
/// tiempo que anoto el grabador.
[[nodiscard]] inline std::optional<engine::Direction>
movimiento_aceptado(const RespuestaCruda& respuesta) {
    if (respuesta.status != 200) {
        return std::nullopt;
    }
    if (respuesta.elapsed_ms >= static_cast<double>(respuesta.timeout)) {
        return std::nullopt;
    }
    const json cuerpo = json::parse(respuesta.body, nullptr, false);
    if (cuerpo.is_discarded() || !cuerpo.is_object()) {
        return std::nullopt;
    }
    // `encoding/json` de Go casa el nombre del campo SIN distinguir mayusculas
    // (`client/models.go:102` declara `json:"move"`), asi que el arbitro acepta
    // `{"Move":"left"}` y nosotros tenemos que aceptarlo tambien. El VALOR si distingue:
    // la comparacion contra las cuatro literales es textual (`cli/commands/play.go:501`).
    for (const auto& [clave, valor] : cuerpo.items()) {
        if (clave.size() != 4) {
            continue;
        }
        std::string minusculas = clave;
        for (char& c : minusculas) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        if (minusculas != "move" || !valor.is_string()) {
            continue;
        }
        return direccion_de(valor.get<std::string>());
    }
    return std::nullopt;
}

/// Orden estable de serpientes: el del turno 0, que es el unico donde estan todas.
[[nodiscard]] inline std::vector<std::string> identidades(const Partida& partida) {
    std::vector<std::string> ids;
    for (const auto& snake : partida.turnos.front()["board"]["snakes"]) {
        ids.push_back(snake.value("id", std::string{}));
    }
    return ids;
}

/// Rectangulo no-hazard de un tablero, o `ok=false` si las casillas sin hazard no
/// forman un rectangulo.
///
/// Es la propiedad del mapa royale que SI se puede comprobar contra un log oficial sin
/// reproducir el `math/rand` de Go: el hazard es el complemento de un rectangulo, y el
/// numero de bordes movidos respecto al tablero entero es `turn / shrinkEveryNTurns`.
/// ver docs/rules.md#r-09 y ver docs/decisions/ADR-0010-rng-del-shrink.md#d-0093
struct Rectangulo {
    bool ok{};
    int min_x{};
    int max_x{};
    int min_y{};
    int max_y{};

    [[nodiscard]] int bordes_movidos(int ancho, int alto) const {
        return min_x + (ancho - 1 - max_x) + min_y + (alto - 1 - max_y);
    }
};

template <int W, int H>
[[nodiscard]] Rectangulo rectangulo_de(const engine::Bitboard<W, H>& hazards) {
    Rectangulo r;
    int min_x = W;
    int max_x = -1;
    int min_y = H;
    int max_y = -1;
    for (int x = 0; x < W; ++x) {
        for (int y = 0; y < H; ++y) {
            if (!hazards.test(engine::Bitboard<W, H>::index_of(x, y))) {
                min_x = std::min(min_x, x);
                max_x = std::max(max_x, x);
                min_y = std::min(min_y, y);
                max_y = std::max(max_y, y);
            }
        }
    }
    if (max_x < 0) {
        // Tablero entero en hazard: rectangulo vacio, que el Go tambien admite.
        r.ok = true;
        r.min_x = W;
        r.max_x = -1;
        r.min_y = H;
        r.max_y = -1;
        return r;
    }
    for (int x = 0; x < W; ++x) {
        for (int y = 0; y < H; ++y) {
            const bool dentro = x >= min_x && x <= max_x && y >= min_y && y <= max_y;
            if (dentro == hazards.test(engine::Bitboard<W, H>::index_of(x, y))) {
                return r; // ok sigue en false: el complemento no es un rectangulo
            }
        }
    }
    r.ok = true;
    r.min_x = min_x;
    r.max_x = max_x;
    r.min_y = min_y;
    r.max_y = max_y;
    return r;
}

/// Hazards de una linea del JSONL, como bitboard.
template <int W, int H> [[nodiscard]] engine::Bitboard<W, H> hazards_de(const json& turno) {
    engine::Bitboard<W, H> board;
    for (const auto& punto : turno["board"]["hazards"]) {
        board.set(engine::Bitboard<W, H>::index_of(punto.value("x", 0), punto.value("y", 0)));
    }
    return board;
}

template <int W, int H, int MaxSnakes>
[[nodiscard]] bool carga_estado(const json& turno,
                                const std::vector<std::string>& ids,
                                engine::GameState<W, H, MaxSnakes>& salida) {
    using Estado = engine::GameState<W, H, MaxSnakes>;
    using Board = typename Estado::Board;

    const json& board = turno["board"];
    if (board.value("width", 0) != W || board.value("height", 0) != H) {
        return false;
    }
    if (ids.size() > static_cast<std::size_t>(MaxSnakes)) {
        return false;
    }

    salida = Estado{};
    salida.turn = turno.value("turn", 0);
    salida.snake_count = static_cast<std::uint8_t>(ids.size());

    const json& ajustes = turno["game"]["ruleset"]["settings"];
    salida.rules.hazard_damage_per_turn = ajustes.value("hazardDamagePerTurn", 14);
    salida.rules.food_spawn_chance = ajustes.value("foodSpawnChance", 15);
    salida.rules.minimum_food = ajustes.value("minimumFood", 1);
    if (ajustes.contains("royale")) {
        salida.rules.shrink_every_n_turns = ajustes["royale"].value("shrinkEveryNTurns", 25);
    }
    salida.rules.timeout_ms = turno["game"].value("timeout", 500);
    salida.rules.map_is_royale = turno["game"].value("map", std::string{}) == "royale";

    for (const auto& punto : board["food"]) {
        salida.food.set(Board::index_of(punto.value("x", 0), punto.value("y", 0)));
    }
    for (const auto& punto : board["hazards"]) {
        salida.hazards.set(Board::index_of(punto.value("x", 0), punto.value("y", 0)));
    }

    for (std::size_t i = 0; i < ids.size(); ++i) {
        salida.snakes[i].status = engine::Elimination::wall_collision;
        salida.snakes[i].length = 0;
    }

    for (const auto& snake : board["snakes"]) {
        const std::string id = snake.value("id", std::string{});
        std::size_t indice = ids.size();
        for (std::size_t i = 0; i < ids.size(); ++i) {
            if (ids[i] == id) {
                indice = i;
                break;
            }
        }
        if (indice == ids.size()) {
            return false;
        }

        auto& destino = salida.snakes[indice];
        destino.status = engine::Elimination::alive;
        destino.eliminated_on_turn = -1;
        destino.head_slot = 0;
        destino.health = static_cast<std::uint8_t>(snake.value("health", 0));
        const json& cuerpo = snake["body"];
        if (cuerpo.size() > static_cast<std::size_t>(Estado::body_capacity)) {
            return false;
        }
        destino.length = static_cast<std::uint16_t>(cuerpo.size());
        for (std::size_t seg = 0; seg < cuerpo.size(); ++seg) {
            destino.cells[seg] = static_cast<std::uint16_t>(
                Board::index_of(cuerpo[seg].value("x", 0), cuerpo[seg].value("y", 0)));
        }
    }

    salida.refresh_occupancy();
    return true;
}

/// Resumen del reparto final de puestos de una partida reproducida.
struct Rangos {
    int count{};
    double suma{};
    float minimo{};
    float maximo{};
    bool hay_empate{};
};

/// Reproduce la partida entera desde el turno 0 y devuelve todo lo que no cuadra.
///
/// No se recarga el estado en cada turno: se arranca del turno 0 y se encadena `apply()`,
/// asi que un error de un turno se arrastra y se ve. Lo unico que se inyecta del log es
/// la comida y los hazards, porque los genera el RNG de Go.
template <int W, int H, int MaxSnakes>
[[nodiscard]] Resultado reproduce(const Partida& partida,
                                  engine::GameState<W, H, MaxSnakes>* estado_final = nullptr) {
    using Estado = engine::GameState<W, H, MaxSnakes>;
    using Board = typename Estado::Board;

    Resultado resultado;
    auto& divergencias = resultado.divergencias;
    const auto ids = identidades(partida);

    Estado estado{};
    if (!carga_estado<W, H, MaxSnakes>(partida.turnos.front(), ids, estado)) {
        divergencias.push_back({0, "", "no se pudo cargar el turno 0"});
        return resultado;
    }

    std::map<std::string, engine::Direction> ultimo;
    for (const auto& id : ids) {
        // El arbitro inicializa LastMove a "up". ver docs/rules.md#r-03
        ultimo[id] = engine::Direction::up;
    }

    for (std::size_t t = 0; t + 1 < partida.turnos.size(); ++t) {
        const json& siguiente = partida.turnos[t + 1];
        const int turno_actual = partida.turnos[t].value("turn", 0);

        std::array<engine::Direction, static_cast<std::size_t>(MaxSnakes)> movimientos{};
        for (std::size_t i = 0; i < ids.size(); ++i) {
            const auto& registros = partida.por_snake.count(ids[i]) ? partida.por_snake.at(ids[i])
                                                                    : std::vector<RespuestaCruda>{};
            for (const auto& registro : registros) {
                if (registro.turn != turno_actual) {
                    continue;
                }
                if (const auto aceptado = movimiento_aceptado(registro)) {
                    ultimo[ids[i]] = *aceptado;
                } else {
                    ++resultado.movimientos_por_defecto;
                }
                break;
            }
            movimientos[i] = ultimo[ids[i]];
        }

        std::array<bool, static_cast<std::size_t>(MaxSnakes)> vivas_antes{};
        for (std::size_t i = 0; i < ids.size(); ++i) {
            vivas_antes[i] = engine::is_alive(estado.snakes[i].status);
        }

        engine::apply(estado, std::span<const engine::Direction>(movimientos.data(), ids.size()));

        int eliminadas_este_turno = 0;
        std::map<int, int> cabezas_en_empate;
        for (std::size_t i = 0; i < ids.size(); ++i) {
            if (!vivas_antes[i] || engine::is_alive(estado.snakes[i].status)) {
                continue;
            }
            ++eliminadas_este_turno;
            ++resultado.causas[static_cast<std::size_t>(estado.snakes[i].status)];
            if (estado.snakes[i].status == engine::Elimination::head_collision) {
                ++cabezas_en_empate[estado.snakes[i].head()];
            }
        }
        if (eliminadas_este_turno >= 2) {
            ++resultado.muertes_simultaneas;
        }
        for (const auto& [celda, cuantas] : cabezas_en_empate) {
            (void)celda;
            if (cuantas >= 2) {
                // Ambas mueren en la misma casilla: solo pasa con longitudes iguales.
                ++resultado.empates_cabeza;
            }
        }

        const int turno_esperado = siguiente.value("turn", 0);
        if (estado.turn != turno_esperado) {
            divergencias.push_back(
                {turno_esperado,
                 "",
                 "el contador de turno no coincide: " + std::to_string(estado.turn) + " vs " +
                     std::to_string(turno_esperado)});
        }

        ++resultado.turnos_comparados;
        std::vector<std::string> vivas_en_log;
        for (const auto& snake : siguiente["board"]["snakes"]) {
            ++resultado.serpientes_comparadas;
            const std::string id = snake.value("id", std::string{});
            vivas_en_log.push_back(id);

            std::size_t indice = ids.size();
            for (std::size_t i = 0; i < ids.size(); ++i) {
                if (ids[i] == id) {
                    indice = i;
                }
            }
            const auto& nuestra = estado.snakes[indice];

            if (!engine::is_alive(nuestra.status)) {
                divergencias.push_back({turno_esperado,
                                        id,
                                        "la matamos y el arbitro la deja viva (causa " +
                                            std::to_string(static_cast<int>(nuestra.status)) +
                                            ")"});
                continue;
            }
            if (static_cast<int>(nuestra.health) != snake.value("health", -1)) {
                divergencias.push_back({turno_esperado,
                                        id,
                                        "salud " + std::to_string(nuestra.health) + " vs " +
                                            std::to_string(snake.value("health", -1))});
            }
            const json& cuerpo = snake["body"];
            if (static_cast<std::size_t>(nuestra.length) != cuerpo.size()) {
                divergencias.push_back({turno_esperado,
                                        id,
                                        "longitud " + std::to_string(nuestra.length) + " vs " +
                                            std::to_string(cuerpo.size())});
                continue;
            }
            for (std::size_t seg = 0; seg < cuerpo.size(); ++seg) {
                const int esperado =
                    Board::index_of(cuerpo[seg].value("x", 0), cuerpo[seg].value("y", 0));
                if (nuestra.segment(static_cast<int>(seg)) != esperado) {
                    const auto c = Board::coord_of(nuestra.segment(static_cast<int>(seg)));
                    divergencias.push_back({turno_esperado,
                                            id,
                                            "segmento " + std::to_string(seg) + " en (" +
                                                std::to_string(c.x) + "," + std::to_string(c.y) +
                                                ") y el arbitro lo pone en (" +
                                                std::to_string(cuerpo[seg].value("x", 0)) + "," +
                                                std::to_string(cuerpo[seg].value("y", 0)) + ")"});
                    break;
                }
            }
        }

        for (std::size_t i = 0; i < ids.size(); ++i) {
            const bool en_log =
                std::find(vivas_en_log.begin(), vivas_en_log.end(), ids[i]) != vivas_en_log.end();
            if (en_log) {
                continue;
            }
            if (engine::is_alive(estado.snakes[i].status)) {
                divergencias.push_back(
                    {turno_esperado, ids[i], "el arbitro la elimina y nosotros la dejamos viva"});
            }
        }

        // La comida que nos queda tiene que ser un subconjunto de la del arbitro: la
        // diferencia es lo que aparecio de nuevo, que no modelamos.
        // ver docs/rules.md#r-07 y ver docs/rules.md#r-10
        Board comida_log;
        for (const auto& punto : siguiente["board"]["food"]) {
            comida_log.set(Board::index_of(punto.value("x", 0), punto.value("y", 0)));
        }
        if (estado.food.without(comida_log).count() != 0) {
            divergencias.push_back(
                {turno_esperado, "", "nos sobra comida que el arbitro ya retiro del tablero"});
        }

        estado.food = comida_log;
        estado.hazards = Board{};
        for (const auto& punto : siguiente["board"]["hazards"]) {
            estado.hazards.set(Board::index_of(punto.value("x", 0), punto.value("y", 0)));
        }
    }

    if (estado_final != nullptr) {
        *estado_final = estado;
    }
    return resultado;
}

/// Reproduce la partida entera y devuelve su estado final, o nullopt si no se pudo
/// cargar el turno 0.
template <int W, int H, int MaxSnakes>
[[nodiscard]] std::optional<engine::GameState<W, H, MaxSnakes>>
reproduce_estado_final(const Partida& partida) {
    engine::GameState<W, H, MaxSnakes> estado{};
    const auto resultado = reproduce<W, H, MaxSnakes>(partida, &estado);
    if (!resultado.divergencias.empty() && resultado.turnos_comparados == 0) {
        return std::nullopt;
    }
    return estado;
}

/// Reproduce la partida y devuelve el reparto de puestos del estado final.
template <int W, int H, int MaxSnakes> [[nodiscard]] Rangos rangos_finales(const Partida& partida) {
    Rangos salida;
    const auto estado = reproduce_estado_final<W, H, MaxSnakes>(partida);
    if (!estado) {
        return salida;
    }

    const auto p = engine::placements(*estado);
    salida.count = p.count;
    salida.minimo = p.rank[0];
    salida.maximo = p.rank[0];
    for (int i = 0; i < p.count; ++i) {
        const float r = p.rank[static_cast<unsigned>(i)];
        salida.suma += static_cast<double>(r);
        salida.minimo = std::min(salida.minimo, r);
        salida.maximo = std::max(salida.maximo, r);
        for (int j = i + 1; j < p.count; ++j) {
            if (r == p.rank[static_cast<unsigned>(j)]) {
                salida.hay_empate = true;
            }
        }
    }
    return salida;
}

} // namespace replay
