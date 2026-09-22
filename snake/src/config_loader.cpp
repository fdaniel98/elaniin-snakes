/// @file config_loader.cpp
/// Parseo del request y del config. Cada lectura del ruleset usa la ruta JSON exacta
/// verificada contra el codigo del arbitro; `settings` NO es plano.
/// ver docs/rules-parametros.md#r-20

#include <fstream>

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
