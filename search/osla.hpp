#pragma once

// Tribes' OSLA baseline (agents sheet 03 §1): every legal action except End Turn, Destroy and
// Disband is applied once to a copy of the observation (rule facts 04 §9) and valued with the Diff
// heuristic plus a tiny noise tie-break; the best is played. End Turn only when nothing else is
// evaluated.

#include <cstdint>

#include "poly/chance.hpp"
#include "poly/rng.hpp"
#include "poly/rules.hpp"
#include "search/agent.hpp"

namespace poly::search {

class OslaAgent final : public Agent {
   public:
    static constexpr int kBudget = 2000;  // forward-model calls per decision (01 §1)

    explicit OslaAgent(std::uint64_t seed) : rng_(seed), chance_(rng_.next()) {}

    Action act(const Observation& obs, std::span<const Action> legal) override;

   private:
    Rng rng_;                // tie-break noise
    SampledChance chance_;   // random outcomes inside the search
    ActionList root_fresh_;  // the copy's own legal set (agent_advance's membership check)
};

}  // namespace poly::search
