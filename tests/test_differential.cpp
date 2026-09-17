/// @file test_differential.cpp
/// Replay de los JSONL del arbitro oficial: se reproduce cada partida turno a turno
/// inyectando la comida y los hazards del log (el RNG del motor es `math/rand` de Go y
/// no lo reproducimos, ver docs/rules-parametros.md#r-99) y se exige estado identico,
/// incluido el movimiento por defecto ante respuestas invalidas.
///
/// El corpus commiteado son las partidas de `tests/corpus/`, elegidas por cubrir causas
/// de muerte, tamaños de tablero y numero de serpientes. Las >=500 de la DoD se
/// regeneran con `scripts/gen-replays.sh`.
/// ver docs/decisions/ADR-0011-corpus-del-diferencial.md#d-0101

#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "replay/replay_harness.hpp"

namespace {

namespace fs = std::filesystem;

struct CasoDeCorpus {
    std::string id;
    std::string jsonl;
    std::string moves;
    int width{};
    int height{};
};

/// Lee `index.json` del corpus: es quien dice el tamaño de tablero de cada partida, y
/// por tanto que instanciacion del motor hay que usar.
[[nodiscard]] std::vector<CasoDeCorpus> corpus() {
    std::vector<CasoDeCorpus> casos;
    // Por defecto, el corpus commiteado. La corrida de >=500 partidas de la DoD apunta
    // aqui con BSR_CORPUS_DIR sin recompilar.
    // ver docs/decisions/ADR-0011-corpus-del-diferencial.md#d-0101
    const char* desde_entorno = std::getenv("BSR_CORPUS_DIR");
    const fs::path raiz{desde_entorno != nullptr ? desde_entorno : BSR_CORPUS_DIR};
    const fs::path indice = raiz / "index.json";
    if (!fs::exists(indice)) {
        return casos;
    }

    std::ifstream fichero(indice);
    const replay::json doc = replay::json::parse(fichero, nullptr, false);
    if (doc.is_discarded() || !doc.is_array()) {
        return casos;
    }

    for (const auto& entrada : doc) {
        CasoDeCorpus caso;
        caso.id = entrada.value("id", std::string{});
        caso.width = entrada.value("width", 0);
        caso.height = entrada.value("height", 0);
        caso.jsonl = (raiz / (caso.id + ".jsonl")).string();
        caso.moves = (raiz / (caso.id + ".moves.jsonl")).string();
        if (fs::exists(caso.jsonl) && fs::exists(caso.moves)) {
            casos.push_back(caso);
        }
    }
    return casos;
}

[[nodiscard]] replay::Resultado reproduce(const CasoDeCorpus& caso) {
    const auto partida = replay::cargar(caso.jsonl, caso.moves);
    if (!partida) {
        return {{{0, "", "JSONL ilegible o sin la forma cabecera/turnos/resultado"}}, 0, 0, 0};
    }
    if (caso.width == 7 && caso.height == 7) {
        return replay::reproduce<7, 7, 4>(*partida);
    }
    if (caso.width == 19 && caso.height == 19) {
        return replay::reproduce<19, 19, 4>(*partida);
    }
    return replay::reproduce<11, 11, 4>(*partida);
}

} // namespace

TEST_CASE("diferencial: el corpus commiteado existe", "[differential]") {
    // Un test diferencial que pasa por no encontrar partidas es un fallo de fase.
    const auto casos = corpus();
    REQUIRE(casos.size() >= 25);

    int con_7 = 0;
    int con_19 = 0;
    for (const auto& caso : casos) {
        con_7 += caso.width == 7 ? 1 : 0;
        con_19 += caso.width == 19 ? 1 : 0;
    }
    // El motor esta instanciado en tres tamaños; el corpus tiene que tocarlos.
    // ver docs/invariants.md#inv-05
    REQUIRE(con_7 >= 1);
    REQUIRE(con_19 >= 1);
}

TEST_CASE("diferencial: replay del corpus sin divergencia", "[differential]") {
    int divergencias = 0;
    int turnos = 0;
    int serpientes = 0;
    int por_defecto = 0;
    int empates = 0;
    int simultaneas = 0;
    std::array<int, 7> causas{};

    for (const auto& caso : corpus()) {
        const auto resultado = reproduce(caso);
        for (const auto& d : resultado.divergencias) {
            UNSCOPED_INFO(caso.id << " turno " << d.turn << " [" << d.snake << "] " << d.detalle);
        }
        divergencias += static_cast<int>(resultado.divergencias.size());
        turnos += resultado.turnos_comparados;
        serpientes += resultado.serpientes_comparadas;
        por_defecto += resultado.movimientos_por_defecto;
        empates += resultado.empates_cabeza;
        simultaneas += resultado.muertes_simultaneas;
        for (std::size_t i = 0; i < causas.size(); ++i) {
            causas[i] += resultado.causas[i];
        }
        CHECK(resultado.divergencias.empty());
    }

    UNSCOPED_INFO("turnos comparados: " << turnos << ", serpientes: " << serpientes
                                        << ", movimientos por defecto: " << por_defecto
                                        << ", empates cabeza a cabeza: " << empates
                                        << ", muertes simultaneas: " << simultaneas);
    UNSCOPED_INFO("eliminaciones por causa: hambre="
                  << causas[1] << " pared=" << causas[2] << " propia=" << causas[3]
                  << " rival=" << causas[4] << " cabeza=" << causas[5] << " hazard=" << causas[6]);
    REQUIRE(divergencias == 0);
    // Un replay que no compara nada no es un replay limpio.
    REQUIRE(turnos >= 300);
    REQUIRE(serpientes >= 600);
    // Si ninguna respuesta invalida llego a aplicarse, la rama del LastMove del arbitro
    // no se esta ejercitando y el corpus no sirve para lo que se genero.
    // ver docs/rules.md#r-03
    REQUIRE(por_defecto >= 20);

    // Cobertura, no correccion: una regla que el corpus nunca ejercita no esta
    // verificada por el, aunque el replay salga limpio. Se comprueba aqui para que el
    // agujero se vea en vez de suponerse cubierto.
    for (std::size_t causa = 1; causa < causas.size(); ++causa) {
        UNSCOPED_INFO("causa sin cubrir: " << causa);
        CHECK(causas[causa] > 0);
    }
    // El empate de longitudes en cabeza a cabeza y las muertes simultaneas son los dos
    // casos que mas facilmente se quedan fuera. ver docs/rules.md#r-08 y docs/rules.md#r-12
    REQUIRE(empates >= 1);
    REQUIRE(simultaneas >= 1);
}

TEST_CASE("diferencial: los hazards del arbitro son el complemento de un rectangulo",
          "[differential][r-09]") {
    // No se compara el schedule -nuestra secuencia de lados no es la del arbitro
    // (ver docs/decisions/ADR-0010-rng-del-shrink.md#d-0091)-, sino la propiedad que si
    // es derivable del log: complemento de un rectangulo, anidado y monotono, con tantos
    // bordes movidos como shrinks lleva la partida. ver docs/rules.md#r-09
    int turnos_con_hazard = 0;

    for (const auto& caso : corpus()) {
        const auto partida = replay::cargar(caso.jsonl, caso.moves);
        REQUIRE(partida);
        if (partida->cabecera.value("map", std::string{}) != "royale") {
            continue;
        }
        const int cadencia =
            partida->cabecera["ruleset"]["settings"]["royale"].value("shrinkEveryNTurns", 25);
        REQUIRE(cadencia >= 1);

        int previos = -1;
        for (const auto& linea : partida->turnos) {
            const int turno = linea.value("turn", 0);
            const int hazards = static_cast<int>(linea["board"]["hazards"].size());

            if (turno < cadencia) {
                // Antes del primer shrink no hay hazard alguno.
                INFO(caso.id << " turno " << turno);
                REQUIRE(hazards == 0);
                continue;
            }
            if (hazards > 0) {
                ++turnos_con_hazard;
            }
            // El rectangulo solo encoge: el numero de casillas en hazard nunca baja.
            INFO(caso.id << " turno " << turno);
            REQUIRE(hazards >= previos);
            previos = hazards;

            const int esperados = turno / cadencia;
            if (caso.width == 7) {
                const auto r = replay::rectangulo_de(replay::hazards_de<7, 7>(linea));
                REQUIRE(r.ok);
                REQUIRE(r.bordes_movidos(7, 7) == esperados);
            } else if (caso.width == 19) {
                const auto r = replay::rectangulo_de(replay::hazards_de<19, 19>(linea));
                REQUIRE(r.ok);
                REQUIRE(r.bordes_movidos(19, 19) == esperados);
            } else {
                const auto r = replay::rectangulo_de(replay::hazards_de<11, 11>(linea));
                REQUIRE(r.ok);
                REQUIRE(r.bordes_movidos(11, 11) == esperados);
            }
        }
    }

    REQUIRE(turnos_con_hazard >= 100);
}
