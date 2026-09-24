/// @file config_loader.cpp
/// Parseo del request y del config. Cada lectura del ruleset usa la ruta JSON exacta
/// verificada contra el codigo del arbitro; `settings` NO es plano.
/// ver docs/rules-parametros.md#r-20

#include <algorithm>
#include <fstream>
#include <utility>
#include <vector>

#include <snake/config_loader.hpp>

namespace snake {

namespace {

using nlohmann::json;

/// Lee un entero por su ruta y marca el fallback si no esta.
int read_int(const json& node, const char* key, int fallback, bool& used_fallback) {
    if (node.is_object()) {
        const auto it = node.find(key);
        if (it != node.end() && it->is_number_integer()) {
            return it->get<int>();
        }
    }
    used_fallback = true;
    return fallback;
}

double read_double(const json& node, const char* key, double fallback) {
    if (node.is_object()) {
        const auto it = node.find(key);
        if (it != node.end() && it->is_number()) {
            return it->get<double>();
        }
    }
    return fallback;
}

int read_plain_int(const json& node, const char* key, int fallback) {
    bool ignored = false;
    return read_int(node, key, fallback, ignored);
}

bool read_bool(const json& node, const char* key, bool fallback) {
    if (node.is_object()) {
        const auto it = node.find(key);
        if (it != node.end() && it->is_boolean()) {
            return it->get<bool>();
        }
    }
    return fallback;
}

const json& child(const json& node, const char* key) {
    static const json empty = json::object();
    if (node.is_object()) {
        const auto it = node.find(key);
        if (it != node.end()) {
            return *it;
        }
    }
    return empty;
}

} // namespace

engine::Variant parse_variant(const std::string& name) {
    if (name == "standard") {
        return engine::Variant::standard;
    }
    if (name == "royale") {
        return engine::Variant::royale;
    }
    if (name == "wrapped") {
        return engine::Variant::wrapped;
    }
    if (name == "constrictor") {
        return engine::Variant::constrictor;
    }
    if (name == "wrapped_constrictor") {
        return engine::Variant::wrapped_constrictor;
    }
    if (name == "solo") {
        return engine::Variant::solo;
    }
    return engine::Variant::unknown;
}

engine::Ruleset parse_ruleset(const json& game) {
    engine::Ruleset rules;

    rules.timeout_ms = read_int(game, "timeout", rules.timeout_ms, rules.fallbacks.timeout);

    const json& ruleset = child(game, "ruleset");
    if (ruleset.is_object() && ruleset.contains("name") && ruleset["name"].is_string()) {
        rules.variant = parse_variant(ruleset["name"].get<std::string>());
    } else {
        rules.fallbacks.variant = true;
    }

    if (game.is_object() && game.contains("map") && game["map"].is_string()) {
        rules.map_is_royale = game["map"].get<std::string>() == "royale";
    } else {
        rules.fallbacks.map_name = true;
    }

    const json& settings = child(ruleset, "settings");
    rules.food_spawn_chance = read_int(
        settings, "foodSpawnChance", rules.food_spawn_chance, rules.fallbacks.food_spawn_chance);
    rules.minimum_food =
        read_int(settings, "minimumFood", rules.minimum_food, rules.fallbacks.minimum_food);
    rules.hazard_damage_per_turn = read_int(settings,
                                            "hazardDamagePerTurn",
                                            rules.hazard_damage_per_turn,
                                            rules.fallbacks.hazard_damage);

    // `royale` es un objeto anidado dentro de `settings`, no un campo plano.
    const json& royale = child(settings, "royale");
    rules.shrink_every_n_turns = read_int(royale,
                                          "shrinkEveryNTurns",
                                          rules.shrink_every_n_turns,
                                          rules.fallbacks.shrink_every_n_turns);

    return rules;
}

Params parse_params(const json& doc) {
    Params params;

    const json& time = child(doc, "time");
    params.time.network_margin_ms =
        read_plain_int(time, "network_margin_ms", params.time.network_margin_ms);
    params.time.safety_margin_ms =
        read_plain_int(time, "safety_margin_ms", params.time.safety_margin_ms);
    params.time.max_compute_ms = read_plain_int(time, "max_compute_ms", params.time.max_compute_ms);

    const json& food = child(doc, "food");
    params.food.seek_below = read_plain_int(food, "seek_below", params.food.seek_below);
    params.food.seek_below_in_hazard =
        read_plain_int(food, "seek_below_in_hazard", params.food.seek_below_in_hazard);
    params.food.free_food_distance =
        read_plain_int(food, "free_food_distance", params.food.free_food_distance);
    params.food.weight = read_double(food, "weight", params.food.weight);

    const json& space = child(doc, "space");
    params.space.min_space_ratio =
        read_double(space, "min_space_ratio", params.space.min_space_ratio);
    params.space.weight = read_double(space, "weight", params.space.weight);
    params.space.tail_escape = read_bool(space, "tail_escape", params.space.tail_escape);

    params.space.worst_case_weight =
        read_double(space, "worst_case_weight", params.space.worst_case_weight);
    params.space.worst_case_max_cuellos =
        read_plain_int(space, "worst_case_max_cuellos", params.space.worst_case_max_cuellos);

    const json& head = child(doc, "head");
    params.head.avoid_equal_or_longer =
        read_double(head, "avoid_equal_or_longer", params.head.avoid_equal_or_longer);
    params.head.prefer_shorter = read_double(head, "prefer_shorter", params.head.prefer_shorter);

    const json& hazard = child(doc, "hazard");
    params.hazard.weight = read_double(hazard, "weight", params.hazard.weight);
    params.hazard.low_health_multiplier =
        read_double(hazard, "low_health_multiplier", params.hazard.low_health_multiplier);

    const json& territory = child(doc, "territory");
    params.territory.version = read_plain_int(territory, "version", params.territory.version);
    params.territory.weight = read_double(territory, "weight", params.territory.weight);
    params.territory.contested_weight =
        read_double(territory, "contested_weight", params.territory.contested_weight);
    params.territory.hazard_value_pct =
        read_plain_int(territory, "hazard_value_pct", params.territory.hazard_value_pct);

    const json& search = child(doc, "search");
    params.search.version = read_plain_int(search, "version", params.search.version);
    params.search.max_depth = read_plain_int(search, "max_depth", params.search.max_depth);
    params.search.max_rivals = read_plain_int(search, "max_rivals", params.search.max_rivals);
    params.search.death_value = read_double(search, "death_value", params.search.death_value);
    params.search.win_value = read_double(search, "win_value", params.search.win_value);
    params.search.survival_bonus =
        read_double(search, "survival_bonus", params.search.survival_bonus);
    params.search.reserve_us = read_plain_int(search, "reserve_us", params.search.reserve_us);
    params.search.budget_nodes = read_plain_int(search, "budget_nodes", params.search.budget_nodes);
    params.search.despair_version =
        read_plain_int(search, "despair_version", params.search.despair_version);

    const json& duel = child(doc, "duel");
    params.duel.version = read_plain_int(duel, "version", params.duel.version);
    params.duel.prefer_shorter = read_double(duel, "prefer_shorter", params.duel.prefer_shorter);
    params.duel.pressure_weight = read_double(duel, "pressure_weight", params.duel.pressure_weight);
    params.duel.length_version = read_plain_int(duel, "length_version", params.duel.length_version);
    params.duel.length_weight = read_double(duel, "length_weight", params.duel.length_weight);
    params.duel.hunt_weight = read_double(duel, "hunt_weight", params.duel.hunt_weight);
    params.duel.territory_version =
        read_plain_int(duel, "territory_version", params.duel.territory_version);
    params.duel.territory_scale = read_double(duel, "territory_scale", params.duel.territory_scale);
    params.duel.trap_version = read_plain_int(duel, "trap_version", params.duel.trap_version);
    params.duel.trap_weight = read_double(duel, "trap_weight", params.duel.trap_weight);
    params.duel.trap_trigger_ratio =
        read_double(duel, "trap_trigger_ratio", params.duel.trap_trigger_ratio);
    params.duel.trap_max_cuellos =
        read_plain_int(duel, "trap_max_cuellos", params.duel.trap_max_cuellos);
    params.duel.survival_version =
        read_plain_int(duel, "survival_version", params.duel.survival_version);
    params.duel.survival_weight = read_double(duel, "survival_weight", params.duel.survival_weight);
    params.duel.tail_loop_weight =
        read_double(duel, "tail_loop_weight", params.duel.tail_loop_weight);

    const json& length = child(doc, "length");
    params.length.version = read_plain_int(length, "version", params.length.version);
    params.length.advantage_weight =
        read_double(length, "advantage_weight", params.length.advantage_weight);
    params.length.target_lead = read_plain_int(length, "target_lead", params.length.target_lead);
    params.length.hunt_weight = read_double(length, "hunt_weight", params.length.hunt_weight);

    const json& survival = child(doc, "survival");
    params.survival.version = read_plain_int(survival, "version", params.survival.version);
    params.survival.safe_turns = read_plain_int(survival, "safe_turns", params.survival.safe_turns);
    params.survival.weight = read_double(survival, "weight", params.survival.weight);
    params.survival.seek_below_turns =
        read_plain_int(survival, "seek_below_turns", params.survival.seek_below_turns);
    params.survival.critical_turns =
        read_plain_int(survival, "critical_turns", params.survival.critical_turns);
    params.survival.panic_weight =
        read_double(survival, "panic_weight", params.survival.panic_weight);

    return params;
}

std::vector<std::string> unknown_keys(const std::string& path) {
    // Una sola lista de lo que el cargador lee: si alguien añade un campo a parse_params y
    // no aqui, el test 1:1 de test_ruleset_parse.cpp lo caza por el otro lado.
    static const std::vector<std::pair<std::string, std::vector<std::string>>> conocidas = {
        {"time", {"network_margin_ms", "safety_margin_ms", "max_compute_ms"}},
        {"food", {"seek_below", "seek_below_in_hazard", "free_food_distance", "weight"}},
        {"space",
         {"min_space_ratio",
          "weight",
          "tail_escape",
          "worst_case_weight",
          "worst_case_max_cuellos"}},
        {"head", {"avoid_equal_or_longer", "prefer_shorter"}},
        {"hazard", {"weight", "low_health_multiplier"}},
        {"territory", {"version", "weight", "contested_weight", "hazard_value_pct"}},
        {"search",
         {"version",
          "max_depth",
          "max_rivals",
          "death_value",
          "win_value",
          "survival_bonus",
          "reserve_us",
          "budget_nodes",
          "despair_version"}},
        {"duel",
         {"version",
          "prefer_shorter",
          "pressure_weight",
          "length_version",
          "length_weight",
          "hunt_weight",
          "territory_version",
          "territory_scale",
          "trap_version",
          "trap_weight",
          "trap_trigger_ratio",
          "trap_max_cuellos",
          "survival_version",
          "survival_weight",
          "tail_loop_weight"}},
        {"length", {"version", "advantage_weight", "target_lead", "hunt_weight"}},
        {"survival",
         {"version", "safe_turns", "weight", "seek_below_turns", "critical_turns", "panic_weight"}},
    };
    std::vector<std::string> fuera;
    std::ifstream file(path);
    const json doc = json::parse(file, nullptr, false);
    if (doc.is_discarded() || !doc.is_object()) {
        return fuera;
    }
    for (const auto& [grupo, valor] : doc.items()) {
        if (grupo.rfind('_', 0) == 0) {
            continue; // comentarios y metadatos: _comment, _version
        }
        const auto g = std::find_if(conocidas.begin(), conocidas.end(), [&](const auto& par) {
            return par.first == grupo;
        });
        if (g == conocidas.end()) {
            fuera.push_back(grupo);
            continue;
        }
        if (!valor.is_object()) {
            continue;
        }
        for (const auto& [clave, v] : valor.items()) {
            (void)v;
            if (std::find(g->second.begin(), g->second.end(), clave) == g->second.end()) {
                fuera.push_back(grupo + "." + clave);
            }
        }
    }
    return fuera;
}

Params load_params(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return Params{};
    }
    const json doc = json::parse(file, nullptr, false);
    if (doc.is_discarded()) {
        return Params{};
    }
    return parse_params(doc);
}

bool parse_state(const json& request, engine::State11& out) {
    if (!request.is_object()) {
        return false;
    }

    const json& board = child(request, "board");
    if (!board.is_object()) {
        return false;
    }
    if (read_plain_int(board, "width", 0) != engine::State11::width) {
        return false;
    }
    if (read_plain_int(board, "height", 0) != engine::State11::height) {
        return false;
    }

    out = engine::State11{};
    out.turn = read_plain_int(request, "turn", 0);
    out.rules = parse_ruleset(child(request, "game"));

    const json& food = child(board, "food");
    if (food.is_array()) {
        for (const auto& point : food) {
            const int x = read_plain_int(point, "x", -1);
            const int y = read_plain_int(point, "y", -1);
            const engine::Coord coord{static_cast<std::int8_t>(x), static_cast<std::int8_t>(y)};
            if (engine::State11::Board::in_bounds(coord)) {
                out.food.set(coord);
            }
        }
    }

    const json& hazards = child(board, "hazards");
    if (hazards.is_array()) {
        for (const auto& point : hazards) {
            const int x = read_plain_int(point, "x", -1);
            const int y = read_plain_int(point, "y", -1);
            const engine::Coord coord{static_cast<std::int8_t>(x), static_cast<std::int8_t>(y)};
            if (engine::State11::Board::in_bounds(coord)) {
                out.hazards.set(coord);
            }
        }
    }

    const json& snakes = child(board, "snakes");
    if (!snakes.is_array() || snakes.empty()) {
        return false;
    }
    if (snakes.size() > static_cast<std::size_t>(engine::State11::max_snakes)) {
        return false;
    }

    std::string my_id;
    const json& you = child(request, "you");
    if (you.is_object() && you.contains("id") && you["id"].is_string()) {
        my_id = you["id"].get<std::string>();
    }

    int index = 0;
    bool found_me = false;
    for (const auto& snake_json : snakes) {
        const json& body = child(snake_json, "body");
        if (!body.is_array() || body.empty()) {
            return false;
        }
        if (body.size() > static_cast<std::size_t>(engine::State11::body_capacity)) {
            return false;
        }

        auto& snake = out.snakes[static_cast<unsigned>(index)];
        snake.head_slot = 0;
        snake.length = static_cast<std::uint16_t>(body.size());
        snake.health = static_cast<std::uint8_t>(read_plain_int(snake_json, "health", 0));
        snake.status = engine::Elimination::alive;
        snake.eliminated_on_turn = -1;

        std::size_t segment = 0;
        for (const auto& point : body) {
            const int x = read_plain_int(point, "x", -1);
            const int y = read_plain_int(point, "y", -1);
            const engine::Coord coord{static_cast<std::int8_t>(x), static_cast<std::int8_t>(y)};
            if (!engine::State11::Board::in_bounds(coord)) {
                return false;
            }
            snake.cells[segment] =
                static_cast<std::uint16_t>(engine::State11::Board::index_of(coord));
            ++segment;
        }

        if (!my_id.empty() && snake_json.is_object() && snake_json.contains("id") &&
            snake_json["id"].is_string() && snake_json["id"].get<std::string>() == my_id) {
            out.you = static_cast<engine::SnakeId>(index);
            found_me = true;
        }
        ++index;
    }

    out.snake_count = static_cast<std::uint8_t>(index);
    out.refresh_occupancy();
    return found_me;
}

} // namespace snake
