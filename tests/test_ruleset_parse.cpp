/// @file test_ruleset_parse.cpp
/// Falla si el parser no recupera CADA parametro por su ruta JSON exacta, y si
/// `snake/config/default.json` deja de ser 1:1 con `snake::Params`.
/// ver docs/rules-parametros.md#r-20

#include <algorithm>
#include <fstream>
#include <string>

#include <snake/config_loader.hpp>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

using nlohmann::json;

namespace {

json full_game() {
    return json::parse(R"({
      "id": "g",
      "ruleset": {
        "name": "royale",
        "version": "cli",
        "settings": {
          "foodSpawnChance": 17,
          "minimumFood": 3,
          "hazardDamagePerTurn": 22,
          "royale": { "shrinkEveryNTurns": 11 }
        }
      },
      "map": "royale",
      "timeout": 450,
      "source": "league"
    })");
}

} // namespace

TEST_CASE("ruleset: cada parametro se lee por su ruta exacta", "[ruleset][r-20]") {
    const engine::Ruleset rules = snake::parse_ruleset(full_game());

    REQUIRE(rules.timeout_ms == 450);
    REQUIRE(rules.variant == engine::Variant::royale);
    REQUIRE(rules.map_is_royale);
    REQUIRE(rules.food_spawn_chance == 17);
    REQUIRE(rules.minimum_food == 3);
    REQUIRE(rules.hazard_damage_per_turn == 22);
    REQUIRE(rules.shrink_every_n_turns == 11);
    REQUIRE_FALSE(rules.fallbacks.any());
}

TEST_CASE("ruleset: shrinkEveryNTurns esta anidado, no es un campo plano", "[ruleset][r-20]") {
    json game = full_game();
    // Se mueve el parametro al nivel plano: el parser NO debe encontrarlo ahi.
    game["ruleset"]["settings"].erase("royale");
    game["ruleset"]["settings"]["shrinkEveryNTurns"] = 11;

    const engine::Ruleset rules = snake::parse_ruleset(game);
    REQUIRE(rules.shrink_every_n_turns == 25);
    REQUIRE(rules.fallbacks.shrink_every_n_turns);
}

TEST_CASE("ruleset: lo que falta cae al fallback y queda marcado", "[ruleset][r-20]") {
    const engine::Ruleset rules = snake::parse_ruleset(json::object());

    REQUIRE(rules.timeout_ms == 500);
    REQUIRE(rules.hazard_damage_per_turn == 14);
    REQUIRE(rules.shrink_every_n_turns == 25);
    REQUIRE(rules.fallbacks.timeout);
    REQUIRE(rules.fallbacks.hazard_damage);
    REQUIRE(rules.fallbacks.shrink_every_n_turns);
    REQUIRE(rules.fallbacks.variant);
    REQUIRE(rules.fallbacks.map_name);
    REQUIRE(rules.fallbacks.any());
}

TEST_CASE("ruleset: nombres de variante", "[ruleset][r-13]") {
    REQUIRE(snake::parse_variant("standard") == engine::Variant::standard);
    REQUIRE(snake::parse_variant("royale") == engine::Variant::royale);
    REQUIRE(snake::parse_variant("wrapped") == engine::Variant::wrapped);
    REQUIRE(snake::parse_variant("constrictor") == engine::Variant::constrictor);
    REQUIRE(snake::parse_variant("wrapped_constrictor") == engine::Variant::wrapped_constrictor);
    REQUIRE(snake::parse_variant("solo") == engine::Variant::solo);
    REQUIRE(snake::parse_variant("lo-que-sea") == engine::Variant::unknown);

    REQUIRE(engine::is_supported(engine::Variant::royale));
    REQUIRE(engine::is_supported(engine::Variant::standard));
    REQUIRE_FALSE(engine::is_supported(engine::Variant::wrapped));
    REQUIRE_FALSE(engine::is_supported(engine::Variant::constrictor));
    // `solo` NO esta soportado: termina cuando no queda NINGUNA viva (solo.go:12-19),
    // mientras que nuestro is_terminal corta con una. ver docs/rules-parametros.md#r-13
    REQUIRE_FALSE(engine::is_supported(engine::Variant::solo));
}

TEST_CASE("params: default.json es 1:1 con snake::Params", "[params]") {
    const std::string path = std::string(BSR_CONFIG_DIR) + "/default.json";
    std::ifstream file(path);
    REQUIRE(file.is_open());

    const json doc = json::parse(file, nullptr, false);
    REQUIRE_FALSE(doc.is_discarded());

    // 1. Todo campo del struct aparece en el JSON con el mismo valor que su default.
    const snake::Params from_json = snake::parse_params(doc);
    const snake::Params defaults{};
    REQUIRE(from_json.time.network_margin_ms == defaults.time.network_margin_ms);
    REQUIRE(from_json.time.safety_margin_ms == defaults.time.safety_margin_ms);
    REQUIRE(from_json.time.max_compute_ms == defaults.time.max_compute_ms);
    REQUIRE(from_json.food.seek_below == defaults.food.seek_below);
    REQUIRE(from_json.food.seek_below_in_hazard == defaults.food.seek_below_in_hazard);
    REQUIRE(from_json.food.free_food_distance == defaults.food.free_food_distance);
    REQUIRE(from_json.food.weight == defaults.food.weight);
    REQUIRE(from_json.space.min_space_ratio == defaults.space.min_space_ratio);
    REQUIRE(from_json.space.weight == defaults.space.weight);
    REQUIRE(from_json.space.tail_escape == defaults.space.tail_escape);
    REQUIRE(from_json.space.worst_case_weight == defaults.space.worst_case_weight);
    REQUIRE(from_json.space.worst_case_max_cuellos == defaults.space.worst_case_max_cuellos);
    REQUIRE(from_json.head.avoid_equal_or_longer == defaults.head.avoid_equal_or_longer);
    REQUIRE(from_json.head.prefer_shorter == defaults.head.prefer_shorter);
    REQUIRE(from_json.hazard.weight == defaults.hazard.weight);
    REQUIRE(from_json.hazard.low_health_multiplier == defaults.hazard.low_health_multiplier);
    REQUIRE(from_json.territory.version == defaults.territory.version);
    REQUIRE(from_json.territory.weight == defaults.territory.weight);
    REQUIRE(from_json.territory.contested_weight == defaults.territory.contested_weight);
    REQUIRE(from_json.territory.hazard_value_pct == defaults.territory.hazard_value_pct);
    REQUIRE(from_json.search.version == defaults.search.version);
    REQUIRE(from_json.search.max_depth == defaults.search.max_depth);
    REQUIRE(from_json.search.max_rivals == defaults.search.max_rivals);
    REQUIRE(from_json.search.death_value == defaults.search.death_value);
    REQUIRE(from_json.search.win_value == defaults.search.win_value);
    REQUIRE(from_json.search.survival_bonus == defaults.search.survival_bonus);
    REQUIRE(from_json.search.reserve_us == defaults.search.reserve_us);
    REQUIRE(from_json.search.budget_nodes == defaults.search.budget_nodes);
    REQUIRE(from_json.length.version == defaults.length.version);
    REQUIRE(from_json.length.advantage_weight == defaults.length.advantage_weight);
    REQUIRE(from_json.length.target_lead == defaults.length.target_lead);
    REQUIRE(from_json.length.hunt_weight == defaults.length.hunt_weight);
    REQUIRE(from_json.survival.version == defaults.survival.version);
    REQUIRE(from_json.survival.safe_turns == defaults.survival.safe_turns);
    REQUIRE(from_json.survival.weight == defaults.survival.weight);
    REQUIRE(from_json.survival.seek_below_turns == defaults.survival.seek_below_turns);
    REQUIRE(from_json.survival.critical_turns == defaults.survival.critical_turns);
    REQUIRE(from_json.survival.panic_weight == defaults.survival.panic_weight);

    // 2. Y al reves: ningun grupo ni clave sobra en el JSON. Si alguien añade una clave
    //    al config sin añadirla al struct, este test la caza.
    const json expected_keys = json::parse(R"({
      "time": ["network_margin_ms", "safety_margin_ms", "max_compute_ms"],
      "food": ["seek_below", "seek_below_in_hazard", "free_food_distance", "weight"],
      "space": ["min_space_ratio", "weight", "tail_escape", "worst_case_weight",
                "worst_case_max_cuellos"],
      "head": ["avoid_equal_or_longer", "prefer_shorter"],
      "hazard": ["weight", "low_health_multiplier"],
      "territory": ["version", "weight", "contested_weight", "hazard_value_pct"],
      "search": ["version", "max_depth", "max_rivals", "death_value", "win_value",
                 "survival_bonus", "reserve_us", "budget_nodes"],
      "length": ["version", "advantage_weight", "target_lead", "hunt_weight"],
      "survival": ["version", "safe_turns", "weight", "seek_below_turns",
                   "critical_turns", "panic_weight"]
    })");

    for (const auto& [group, keys] : doc.items()) {
        if (group.rfind('_', 0) == 0) {
            continue; // comentarios
        }
        REQUIRE(expected_keys.contains(group));
        for (const auto& [key, value] : keys.items()) {
            const bool known =
                std::find(expected_keys[group].begin(), expected_keys[group].end(), key) !=
                expected_keys[group].end();
            INFO("clave inesperada en default.json: " << group << "." << key);
            REQUIRE(known);
        }
        REQUIRE(keys.size() == expected_keys[group].size());
    }
    for (const auto& [group, keys] : expected_keys.items()) {
        INFO("grupo ausente en default.json: " << group);
        REQUIRE(doc.contains(group));
    }
}

TEST_CASE("parse_state: rechaza tableros sin instanciacion", "[ruleset][r-11]") {
    json request = json::parse(R"({
      "turn": 3,
      "board": {"width": 19, "height": 19, "food": [], "hazards": [], "snakes": [
        {"id":"a","name":"a","health":100,"body":[{"x":1,"y":1},{"x":1,"y":1},{"x":1,"y":1}],
         "head":{"x":1,"y":1},"length":3,"latency":"1","shout":"","squad":"",
         "customizations":{"color":"#000000","head":"default","tail":"default"}}
      ]},
      "you": {"id":"a"},
      "game": {"timeout": 500}
    })");

    engine::State11 state;
    REQUIRE_FALSE(snake::parse_state(request, state));

    request["board"]["width"] = 11;
    request["board"]["height"] = 11;
    REQUIRE(snake::parse_state(request, state));
    REQUIRE(state.snake_count == 1);
    REQUIRE(state.turn == 3);
    REQUIRE(state.snake(0).length == 3);
    REQUIRE(state.snake(0).tail_is_stacked());
}
