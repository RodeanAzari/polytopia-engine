#pragma once

// Tribes' Rule Based agent, reimplemented from agents sheet 05 (facts only, ADR-002 amendment):
// every legal action gets an integer score from the observation; the agent plays a uniformly random
// action among those with the best score (never one below -1).

#include <span>
#include <vector>

#include "poly/rng.hpp"
#include "search/agent.hpp"

namespace poly::search {

class RuleBasedAgent final : public Agent {
   public:
    explicit RuleBasedAgent(std::uint64_t seed) : rng_(seed) {}

    Action act(const Observation& obs, std::span<const Action> legal) override;

    // How often Convert was scored (sheet 05: Tribes would throw there; reported as a deviation).
    [[nodiscard]] long long convert_scored() const { return convert_scored_; }

   private:
    Rng rng_;
    long long convert_scored_ = 0;
};

// Sheet 05 §2: the score of action a for the observer of `obs`. `convert_scored` counts Convert.
int rule_based_score(const Observation& obs, const Action& a, long long& convert_scored);

// The scores of all `actions` (one observation), as rule_based_score gives them, into `out`; the
// state is scanned once for the whole batch (PUCT priors, ADR-008 §3).
void rule_based_scores(const Observation& obs, std::span<const Action> actions,
                       std::vector<int>& out, long long& convert_scored);

}  // namespace poly::search
