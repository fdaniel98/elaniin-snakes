#pragma once

/// @file config_loader.hpp
/// Frontera de JSON del proceso. Es el unico sitio donde se conoce la forma del
/// payload; `engine/` no depende de ninguna libreria de JSON.
/// ver docs/architecture.md#a-03
///
/// Los parametros del ruleset se leen por su ruta JSON exacta; lo que falte se
/// rellena con el fallback y se marca en `Ruleset::fallbacks`.
/// ver docs/rules-parametros.md#r-20

#include <string>
#include <vector>

#include <engine/ruleset.hpp>
#include <engine/state.hpp>

#include <snake/params.hpp>

#include <nlohmann/json.hpp>

namespace snake {

/// Parametros de estrategia desde el JSON de config (1:1 con `Params`).
[[nodiscard]] Params parse_params(const nlohmann::json& doc);

/// Carga `snake/config/default.json` (o el que indique `path`). Ante cualquier error
/// devuelve los defaults del struct, que son los mismos que trae el JSON.
[[nodiscard]] Params load_params(const std::string& path);

/// Claves de un config que este binario NO sabe leer, como "grupo.clave". Una clave
/// desconocida se ignora al cargar, asi que un binario viejo con un config nuevo juega
/// otra snake sin decir nada: un A/B asi mide v5 contra v5 y parece un resultado. La arena
/// aborta si esta lista no esta vacia y el servidor avisa con WARN.
/// ver docs/experimentos-instrumento.md#s-territorio-duelo-r
[[nodiscard]] std::vector<std::string> unknown_keys(const std::string& path);

/// Parametros del ruleset desde el objeto `game` del request.
[[nodiscard]] engine::Ruleset parse_ruleset(const nlohmann::json& game);

/// Variante a partir de `game.ruleset.name`. ver docs/rules-parametros.md#r-13
[[nodiscard]] engine::Variant parse_variant(const std::string& name);

/// Construye el estado desde un request completo de `/move`.
/// Devuelve false si el tablero no es 11x11 (no hay instanciacion para ese tamaño) o
/// si el payload no tiene la forma esperada; el llamante cae entonces al fail-safe.
/// ver docs/rules.md#r-11
[[nodiscard]] bool parse_state(const nlohmann::json& request, engine::State11& out);

} // namespace snake
