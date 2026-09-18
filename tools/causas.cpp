/// @file causas.cpp
/// Deriva la causa de muerte de cada serpiente a partir del JSONL del arbitro.
///
///     causas <partida.jsonl>        -> un objeto JSON por stdout
///
/// El JSONL no trae las causas, ni los movimientos, y la serpiente eliminada desaparece
/// del turno siguiente (ver docs/rules.md#r-12). Lo que si trae es el estado completo de
/// cada turno, y eso basta para ACOTAR:
///
///  - el movimiento de una serpiente que sobrevive sale de la diferencia de cabezas;
///  - el de una que muere no esta en ningun sitio, asi que se ENUMERAN sus cuatro
///    movimientos y se queda con los que llevan a un turno siguiente igual al observado.
///
/// Si todos los candidatos consistentes dan la misma causa, la causa es exacta y la firma
/// el mismo `apply()` que reprodujo 500 partidas sin divergencia en la fase 1. Si dan
/// causas distintas, se dice `ambigua` con la lista. No se elige la mas probable: eso
/// seria inventar un dato y presentarlo como medido.
///
/// El ultimo turno de toda partida no se exporta (`cli/commands/play.go:273-276`), asi
/// que las muertes de ese turno salen como `final_no_exportado`. El ganador si consta.
#include <array>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "engine/rules.hpp"
#include "engine/state.hpp"
#include "engine/types.hpp"
#include "replay/replay_harness.hpp"

namespace {

using json = nlohmann::json;
constexpr int W = 11;
constexpr int H = 11;
constexpr int MaxSnakes = 4;
using Estado = engine::GameState<W, H, MaxSnakes>;

constexpr std::array<engine::Direction, 4> kDirecciones{
    engine::Direction::up, engine::Direction::down,
    engine::Direction::left, engine::Direction::right};

const char* nombre_causa(engine::Elimination e) {
    switch (e) {
        case engine::Elimination::out_of_health: return "hambre";
        case engine::Elimination::wall_collision: return "pared";
        case engine::Elimination::self_collision: return "cuerpo_propio";
        case engine::Elimination::snake_collision: return "cuerpo_rival";
        case engine::Elimination::head_collision: return "cabezazo";
        case engine::Elimination::hazard: return "hazard";
        case engine::Elimination::alive: return "viva";
    }
    return "desconocida";
}

/// Direccion que lleva de `desde` a `hasta`, si son adyacentes en el tablero.
std::optional<engine::Direction> direccion_entre(int desde, int hasta) {
    const int xd = desde % W;
    const int yd = desde / W;
    const int xh = hasta % W;
    const int yh = hasta / W;
    if (xd == xh && yh == yd + 1) return engine::Direction::up;
    if (xd == xh && yh == yd - 1) return engine::Direction::down;
    if (yd == yh && xh == xd - 1) return engine::Direction::left;
    if (yd == yh && xh == xd + 1) return engine::Direction::right;
    return std::nullopt;
}

/// ¿El estado resultante encaja con lo que el log dice del turno siguiente?
/// Se comparan las vivas y la cabeza de cada superviviente. La comida y los hazards no,
/// porque los genera el RNG de Go y se inyectan.
bool encaja(const Estado& resultado, const std::vector<std::string>& ids,
            const std::map<std::string, int>& cabezas_siguientes) {
    for (std::size_t i = 0; i < ids.size(); ++i) {
        const bool viva = engine::is_alive(resultado.snakes[i].status);
        const auto it = cabezas_siguientes.find(ids[i]);
        if (viva != (it != cabezas_siguientes.end())) {
            return false;
        }
        if (viva && resultado.snakes[i].head() != it->second) {
            return false;
        }
    }
    return true;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "uso: causas <partida.jsonl>\n");
        return 2;
    }
    const auto partida = replay::cargar(argv[1], "");
    if (!partida) {
        std::fprintf(stderr, "ERROR no se pudo cargar %s\n", argv[1]);
        return 2;
    }
    const auto ids = replay::identidades(*partida);

    json salida;
    salida["jsonl"] = argv[1];
    salida["snakes"] = json::object();
    for (const auto& id : ids) {
        salida["snakes"][id] = {{"causa", "sobrevivio"}, {"turno", nullptr},
                                {"certeza", "n/a"}};
    }

    for (std::size_t t = 0; t + 1 < partida->turnos.size(); ++t) {
        Estado estado{};
        if (!replay::carga_estado<W, H, MaxSnakes>(partida->turnos[t], ids, estado)) {
            continue;
        }
        const int turno = partida->turnos[t].value("turn", 0);

        // Cabezas del turno siguiente: quien sigue viva y donde.
        std::map<std::string, int> cabezas;
        for (const auto& s : partida->turnos[t + 1]["board"]["snakes"]) {
            cabezas[s.value("id", "")] =
                engine::Bitboard<W, H>::index_of(s["head"].value("x", 0), s["head"].value("y", 0));
        }

        std::vector<std::size_t> muertas;
        std::array<engine::Direction, MaxSnakes> base{};
        for (std::size_t i = 0; i < ids.size(); ++i) {
            if (!engine::is_alive(estado.snakes[i].status)) {
                continue;
            }
            const auto it = cabezas.find(ids[i]);
            if (it == cabezas.end()) {
                muertas.push_back(i);
                continue;
            }
            // Superviviente: el movimiento es la diferencia de cabezas. Exacto.
            if (const auto d = direccion_entre(estado.snakes[i].head(), it->second)) {
                base[i] = *d;
            }
        }
        if (muertas.empty()) {
            continue;
        }

        // Enumeracion: 4^k con k <= 3, o sea 64 combinaciones en el peor caso.
        std::map<std::string, std::set<std::string>> causas_por_snake;
        const std::size_t combinaciones = 1U << (2 * muertas.size());
        for (std::size_t c = 0; c < combinaciones; ++c) {
            auto movimientos = base;
            for (std::size_t k = 0; k < muertas.size(); ++k) {
                movimientos[muertas[k]] = kDirecciones[(c >> (2 * k)) & 3U];
            }
            Estado copia = estado;
            engine::apply(copia, std::span<const engine::Direction>(movimientos.data(), ids.size()));
            if (!encaja(copia, ids, cabezas)) {
                continue;
            }
            for (const auto i : muertas) {
                causas_por_snake[ids[i]].insert(nombre_causa(copia.snakes[i].status));
            }
        }

        for (const auto i : muertas) {
            const auto& causas = causas_por_snake[ids[i]];
            json& registro = salida["snakes"][ids[i]];
            registro["turno"] = turno;
            if (causas.size() == 1) {
                registro["causa"] = *causas.begin();
                registro["certeza"] = "exacta";
            } else if (causas.empty()) {
                // Ningun movimiento reproduce el turno siguiente: o el log tiene algo que
                // el motor no modela, o la serpiente murio por algo que no depende de su
                // movimiento. No se adivina.
                registro["causa"] = "sin_candidato";
                registro["certeza"] = "indeterminada";
            } else {
                registro["causa"] = "ambigua";
                registro["certeza"] = "ambigua";
                registro["posibles"] = causas;
            }
        }
    }

    // El ultimo turno no se exporta: quien seguia vivo ahi y no gano, murio sin registro.
    // La ultima linea del JSONL no es un turno -`cargar` no la guarda- sino el resultado.
    std::string ganador;
    const auto lineas = replay::lineas_de(argv[1]);
    if (!lineas.empty()) {
        const auto ultima = json::parse(lineas.back(), nullptr, false);
        if (!ultima.is_discarded() && ultima.contains("winnerId") && ultima["winnerId"].is_string()) {
            ganador = ultima["winnerId"].get<std::string>();
        }
    }
    for (const auto& id : ids) {
        json& registro = salida["snakes"][id];
        if (registro["causa"] == "sobrevivio" && id != ganador) {
            registro["causa"] = "final_no_exportado";
            registro["certeza"] = "indeterminada";
        }
    }
    salida["ganador"] = ganador;
    std::printf("%s\n", salida.dump().c_str());
    return 0;
}
