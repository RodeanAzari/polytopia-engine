#pragma once

// The agents our tools can run, by the names the tournament runner uses (ADR-007), and the seed
// mixing that derives each seat's agent seed from a game seed: shared by tools/tournament.cpp and
// tools/record.cpp, so a recorded game with a CSV row's game seed is that tournament game.

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

#include "search/agent.hpp"
#include "search/mc.hpp"
#include "search/mcts.hpp"
#include "search/osla.hpp"
#include "search/puct.hpp"
#include "search/random_agent.hpp"
#include "search/rhea.hpp"
#include "search/rule_based.hpp"

namespace polytools {

inline std::uint64_t mix(std::uint64_t a, std::uint64_t b) {  // SplitMix64 of a combined value
    std::uint64_t z = (a * 0x9E3779B97F4A7C15ULL) + b + 0x632BE59BD9B4E019ULL;
    z = (z ^ (z >> 30U)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27U)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31U);
}

// Seat i's (0 or 1) agent seed in the game with this game seed.
inline std::uint64_t agent_seed(std::uint64_t game_seed, int seat) {
    return mix(game_seed, static_cast<std::uint64_t>(seat) + 1);
}

inline std::unique_ptr<poly::search::Agent> make_agent(const std::string& name,
                                                       std::uint64_t seed) {
    if (name == "Random") {
        return std::make_unique<poly::search::RandomAgent>(seed);
    }
    if (name == "RuleBased") {
        return std::make_unique<poly::search::RuleBasedAgent>(seed);
    }
    if (name == "OSLA") {
        return std::make_unique<poly::search::OslaAgent>(seed);
    }
    if (name == "MC") {
        return std::make_unique<poly::search::McAgent>(seed);
    }
    if (name == "MCTS") {
        return std::make_unique<poly::search::MctsAgent>(seed);
    }
    if (name == "RHEA") {
        return std::make_unique<poly::search::RheaAgent>(seed);
    }
    if (name == "PUCT" || name.starts_with("PUCT:")) {  // ours, ADR-008; e.g. PUCT:c=1.5:vision=own
        const std::string_view spec =
            name.size() > 5 ? std::string_view(name).substr(5) : std::string_view{};
        return std::make_unique<poly::search::PuctAgent>(seed,
                                                         poly::search::parse_puct_params(spec));
    }
    throw std::runtime_error("unknown agent: " + name);
}

}  // namespace polytools
