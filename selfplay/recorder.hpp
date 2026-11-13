#pragma once

// An agent that plays as the Phase 3 PUCT agent (ADR-008/009/010) and records each searched
// decision for training (ADR-011 §4): the observer's features and the search's summed root visits
// (ADR-009 §2) keyed by network output index. Decisions the agent didn't search (a single legal
// action, or a single macro candidate) are not recorded. It plays exactly as the wrapped agent.

#include <cstdint>
#include <span>
#include <vector>

#include "search/agent.hpp"
#include "search/puct.hpp"
#include "selfplay/record.hpp"

namespace poly::selfplay {

class RecordingPuct final : public search::Agent {
   public:
    RecordingPuct(std::uint64_t seed, search::PuctParams params) : inner_(seed, params) {}

    Action act(const Observation& obs, std::span<const Action> legal) override;

    // The decisions recorded so far (moved out by the caller when the game ends).
    std::vector<Decision>& decisions() { return decisions_; }

   private:
    search::PuctAgent inner_;
    Features features_;
    std::vector<Decision> decisions_;
};

}  // namespace poly::selfplay
