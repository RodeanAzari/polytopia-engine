#pragma once

// One uniform-random game on the true state, in Tribes' turn loop (rule facts 04 §2): the workload
// of `make bench` (tools/bench.cpp) and the games `make replay` records (tools/record.cpp). Same
// seed, same game.

#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "poly/chance.hpp"
#include "poly/level.hpp"
#include "poly/rng.hpp"
#include "poly/rules.hpp"

namespace polytools {

// A level written by OracleBench: file level-<players>-<mode>-<seed>.txt, first line the mode, then
// the level lines.
struct LevelFile {
    poly::GameState state;
    std::uint64_t seed = 0;
    int players = 0;
    std::string mode;
};

inline LevelFile load_level_file(const std::string& path, poly::ObsMode obs = poly::ObsMode::Full) {
    std::ifstream in(path);
    LevelFile f;
    if (!in || !std::getline(in, f.mode) || (f.mode != "Capitals" && f.mode != "Score")) {
        throw std::runtime_error("cannot read " + path);
    }
    std::vector<std::string> lines;
    for (std::string line; std::getline(in, line);) {
        lines.push_back(line);
    }
    const std::string name = path.substr(path.rfind('/') + 1);
    if (!name.starts_with("level-")) {
        throw std::runtime_error("not a level-<players>-<mode>-<seed>.txt file: " + path);
    }
    f.seed = std::stoull(name.substr(name.rfind('-') + 1));
    f.players = std::stoi(name.substr(6, name.find('-', 6) - 6));
    poly::SampledChance load_chance(f.seed);  // partial obs: meeting techs at load (03b §1)
    f.state = poly::load_level(
        lines, f.mode == "Capitals" ? poly::GameMode::Capitals : poly::GameMode::Score, obs,
        load_chance);
    return f;
}

struct RandomGameStats {
    long long steps = 0;  // agent decisions, End turn included
    long long turns = 0;
};

enum class Event : std::uint8_t { TurnStart, Action, TurnEnd };

// Plays from s to the end. on_event(state, event, action) runs after each turn start, executed
// action and turn end; `action` is meaningful for Event::Action only (End turn is reported as the
// TurnEnd that follows it).
template <class OnEvent>
RandomGameStats play_random(poly::GameState& s, std::uint64_t seed, OnEvent&& on_event) {
    poly::SampledChance chance(seed);
    poly::Rng agent(seed ^ 0x5DEECE66DULL);
    poly::ActionList legal;
    RandomGameStats r;
    while (!s.game_over) {
        const int before = s.tick;
        const int p = poly::next_turn_tribe(s);
        if (s.tick != before && poly::update_game_over(s)) {  // Tribes checks before each tick
            break;
        }
        ++r.turns;
        poly::start_turn(s, p);
        on_event(s, Event::TurnStart, poly::Action{});
        poly::legal_actions(s, legal);
        bool ask = true;  // Tribes' continueTurn: decided before the chosen action runs
        while (ask) {
            const poly::Action a =
                legal[static_cast<int>(agent.below(static_cast<std::uint32_t>(legal.size())))];
            ++r.steps;
            ask = legal.size() != 1 || legal[0].type != poly::ActionType::EndTurn;
            if (a.type == poly::ActionType::EndTurn) {
                break;
            }
            poly::apply_action(s, a, chance);
            on_event(s, Event::Action, a);
            poly::legal_actions(s, legal);
            if (poly::update_game_over(s)) {
                break;
            }
        }
        poly::end_turn(s);
        poly::update_game_over(s);
        on_event(s, Event::TurnEnd, poly::Action{});
    }
    return r;
}

}  // namespace polytools
