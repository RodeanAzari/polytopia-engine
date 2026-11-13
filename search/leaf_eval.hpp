#pragma once

// A network evaluating search nodes for PUCT (ADR-011 §2, ADR-012 §1): priors over a node's edges
// and a value for the player to move, from that player's observation only (no peeking: it receives
// an Observation, never a GameState). Implementations may batch across threads and block while a
// batch fills, so evaluate() must be thread-safe.

#include <span>
#include <vector>

#include "poly/action.hpp"
#include "poly/observation.hpp"

namespace poly::search {

class LeafEvaluator {
   public:
    LeafEvaluator() = default;
    LeafEvaluator(const LeafEvaluator&) = delete;
    LeafEvaluator& operator=(const LeafEvaluator&) = delete;
    LeafEvaluator(LeafEvaluator&&) = delete;
    LeafEvaluator& operator=(LeafEvaluator&&) = delete;
    virtual ~LeafEvaluator() = default;

    // `priors` gets one probability per action of `actions` (the actions of obs.observer(), who is
    // to move; they sum to 1), `value` the expected result for obs.observer() in [-1, 1].
    virtual void evaluate(const Observation& obs, std::span<const Action> actions,
                          std::vector<double>& priors, double& value) = 0;
};

}  // namespace poly::search
