#pragma once

// Tribes' flat Monte Carlo baseline (agents sheet 03 §2): random rollouts of 20 actions from one
// root candidate (prioritized group, sheet 01 §3), valued by the Diff heuristic, 2,000
// forward-model calls per decision.

#include <cstdint>
#include <limits>
#include <vector>

#include "poly/chance.hpp"
#include "poly/rng.hpp"
#include "poly/rules.hpp"
#include "search/agent.hpp"

namespace poly::search {

class McAgent final : public Agent {
   public:
    static constexpr int kBudget = 2000;  // forward-model calls per decision (01 §1)
    static constexpr int kDepth = 20;     // rollout length (01 §2)

    explicit McAgent(std::uint64_t seed) : rng_(seed), chance_(rng_.next()) {}

    Action act(const Observation& obs, std::span<const Action> legal) override;

    // Forward-model calls the last searched decision used (0 if it didn't search).
    [[nodiscard]] int last_fm_calls() const { return fm_calls_; }

   private:
    double rollout(const GameState& root, int player, std::span<const Action> root_legal,
                   const Action& first);

    Rng rng_;
    SampledChance chance_;  // random outcomes inside the search
    int fm_calls_ = 0;
    std::vector<Action> candidates_;
    ActionList legal_;
    ActionList fresh_;
};

// 03 §2.2 step 1: every tribe but `player` researches ⌊tick / 2⌋ random techs (no cost, no score),
// a guess at the techs its observation hides.
void guess_opponent_techs(GameState& s, int player, Chance& chance);

// 03 §2.1 step 4c: the best candidate is the one whose running mean set the highest record; a
// record is never lowered when that candidate's mean later falls.
class RunningBest {
   public:
    explicit RunningBest(std::size_t candidates) : sum_(candidates), count_(candidates) {}
    void add(std::size_t i, double value);
    [[nodiscard]] std::size_t best() const { return best_; }

   private:
    std::vector<double> sum_;
    std::vector<int> count_;
    std::size_t best_ = 0;
    double record_ = -std::numeric_limits<double>::infinity();
};

}  // namespace poly::search
