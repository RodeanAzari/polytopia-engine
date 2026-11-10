#pragma once

// Tribes' Random agent (agents sheet 05 §3): uniform over all legal actions, one draw per call.

#include "poly/rng.hpp"
#include "search/agent.hpp"

namespace poly::search {

class RandomAgent final : public Agent {
   public:
    explicit RandomAgent(std::uint64_t seed) : rng_(seed) {}

    Action act(const Observation& /*obs*/, std::span<const Action> legal) override {
        return legal[rng_.below(static_cast<std::uint32_t>(legal.size()))];
    }

   private:
    Rng rng_;
};

}  // namespace poly::search
