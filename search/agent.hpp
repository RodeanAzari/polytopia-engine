#pragma once

// A player in a game (ADR-007 tournaments). Agents see only their observation (CLAUDE.md: no
// peeking through the fog); the legal actions come from the environment (04 §7). Each agent owns
// its RNG, seeded by the runner from the game seed, so a game replays exactly.

#include <span>

#include "poly/action.hpp"
#include "poly/observation.hpp"

namespace poly::search {

class Agent {
   public:
    Agent() = default;
    Agent(const Agent&) = delete;
    Agent& operator=(const Agent&) = delete;
    Agent(Agent&&) = delete;
    Agent& operator=(Agent&&) = delete;
    virtual ~Agent() = default;

    // One decision of the observing tribe. Normally one of `legal`; anything else is a no-op and
    // the agent is asked again, as in Tribes (agents sheet 04 §4).
    virtual Action act(const Observation& obs, std::span<const Action> legal) = 0;
};

}  // namespace poly::search
