#pragma once

// Tribes' RHEA baseline in the paper configuration (agents sheet 04): population 1, i.e. a
// random-mutation hill climber over one 20-action plan (End Turn included, so later actions are
// the opponent's), mutation rate 0.1, Diff heuristic, 2,000 forward-model calls per decision.
// The plan is kept between calls of a turn and shifted (with Tribes' stale-tail quirk, 04 §3.2).

#include <cstdint>
#include <span>
#include <vector>

#include "poly/chance.hpp"
#include "poly/rng.hpp"
#include "poly/rules.hpp"
#include "search/agent.hpp"

namespace poly::search {

class RheaAgent final : public Agent {
   public:
    static constexpr int kBudget = 2000;      // 01 §1
    static constexpr int kLength = 20;        // 01 §2
    static constexpr double kMutation = 0.1;  // 04, paper configuration

    explicit RheaAgent(std::uint64_t seed) : rng_(seed), chance_(rng_.next()) {}

    Action act(const Observation& obs, std::span<const Action> legal) override;

    // The last decision's search, for tests and statistics.
    [[nodiscard]] int last_fm_calls() const { return fm_calls_; }
    [[nodiscard]] int last_mutations() const { return mutations_; }
    [[nodiscard]] int last_accepted() const { return accepted_; }
    // Mutant genes drawn at random by the mutation rate (not by infeasibility).
    [[nodiscard]] int last_rate_draws() const { return rate_draws_; }
    [[nodiscard]] std::span<const Action> plan() const { return plan_; }

   private:
    // A copy of the observation being stepped, with Tribes' cached legal list (04 §9).
    struct Sim {
        GameState s;
        ActionList cached;    // the copy's list: the environment's until an action executes
        bool at_root = true;  // no action executed yet
        int applied = 0;      // forward-model calls on this copy
    };

    static void start(Sim& sim, const Observation& obs, std::span<const Action> legal);
    [[nodiscard]] bool feasible(const Sim& sim, const Action& a) const;
    Action random_action(const Sim& sim);
    void apply(Sim& sim, const Action& a);

    void init(const Observation& obs, std::span<const Action> legal);
    void shift(const Observation& obs, std::span<const Action> legal);
    void mutate(const Observation& obs, std::span<const Action> legal);

    Rng rng_;
    SampledChance chance_;  // random outcomes inside the search
    std::vector<Action> plan_;
    double value_ = 0.0;
    int last_tick_ = -1;
    int fm_calls_ = 0;
    int mutations_ = 0;
    int accepted_ = 0;
    int rate_draws_ = 0;
    ActionList root_fresh_;  // the copy's own legal set at the root (feasibility there)
    std::vector<Action> mutant_;
    Sim sim_;
};

}  // namespace poly::search
