#include "search/rhea.hpp"

#include <algorithm>

#include "search/diff_heuristic.hpp"

namespace poly::search {

void RheaAgent::start(Sim& sim, const Observation& obs, std::span<const Action> legal) {
    sim.s = obs.view();
    sim.cached.clear();
    for (const Action& a : legal) {
        sim.cached.push_back(a);
    }
    sim.at_root = true;
    sim.applied = 0;
}

// 04 §1: the game isn't over and the action is in the copy's legal set (which implies the actor
// checks). The cached list is the copy's own set once an action has executed.
bool RheaAgent::feasible(const Sim& sim, const Action& a) const {
    if (sim.s.game_over) {
        return false;
    }
    const ActionList& set = sim.at_root ? root_fresh_ : sim.cached;
    return std::ranges::find(set, a) != set.end();
}

// 04 §1: uniform over the cached list without Destroy and Disband (End Turn included).
Action RheaAgent::random_action(const Sim& sim) {
    const auto allowed = [](const Action& a) {
        return a.type != ActionType::Destroy && a.type != ActionType::Disband;
    };
    const auto n = static_cast<std::uint32_t>(std::ranges::count_if(sim.cached, allowed));
    check(n > 0, "rhea: no random action (only Destroy/Disband legal)");
    std::uint32_t k = rng_.below(n);
    for (const Action& a : sim.cached) {
        if (allowed(a) && k-- == 0) {
            return a;
        }
    }
    return sim.cached[0];  // unreachable
}

// One forward-model call; a no-op keeps the cached list (stale, 04 §9).
void RheaAgent::apply(Sim& sim, const Action& a) {
    ++fm_calls_;
    ++sim.applied;
    if (agent_advance(sim.s, a, chance_)) {
        legal_actions(sim.s, sim.cached);
        sim.at_root = false;
    }
}

// 04 §3.1.
void RheaAgent::init(const Observation& obs, std::span<const Action> legal) {
    start(sim_, obs, legal);
    plan_.clear();
    while (!sim_.s.game_over && sim_.applied < kLength) {
        const Action a = random_action(sim_);
        apply(sim_, a);
        plan_.push_back(a);
    }
    value_ = diff_heuristic(obs.view(), sim_.s, obs.observer());
}

// 04 §3.2, quirk-exact: the infeasible tail stays; new random actions go after it.
void RheaAgent::shift(const Observation& obs, std::span<const Action> legal) {
    if (!plan_.empty()) {
        plan_.erase(plan_.begin());
    }
    start(sim_, obs, legal);
    for (const Action& a : plan_) {
        if (!feasible(sim_, a)) {
            break;
        }
        apply(sim_, a);
    }
    while (!sim_.s.game_over && sim_.applied < kLength) {
        const Action a = random_action(sim_);
        apply(sim_, a);
        plan_.push_back(a);
    }
    value_ = diff_heuristic(obs.view(), sim_.s, obs.observer());
}

// 04 §3.3; the mutant replaces the plan when its value is ≥ the plan's (ties replace, §2).
void RheaAgent::mutate(const Observation& obs, std::span<const Action> legal) {
    start(sim_, obs, legal);
    mutant_.clear();
    for (const Action& planned : plan_) {
        if (sim_.s.game_over) {
            break;
        }
        const bool random = static_cast<double>(rng_.next() >> 11U) * 0x1.0p-53 < kMutation;
        rate_draws_ += random ? 1 : 0;
        const Action a = !random && feasible(sim_, planned) ? planned : random_action(sim_);
        apply(sim_, a);
        mutant_.push_back(a);
    }
    ++mutations_;
    const double v = diff_heuristic(obs.view(), sim_.s, obs.observer());
    if (v >= value_) {
        plan_.swap(mutant_);
        value_ = v;
        ++accepted_;
    }
}

Action RheaAgent::act(const Observation& obs, std::span<const Action> legal) {
    fm_calls_ = 0;
    mutations_ = 0;
    accepted_ = 0;
    rate_draws_ = 0;
    legal_actions(obs.view(), root_fresh_);
    const int tick = obs.view().tick;
    if (tick != last_tick_) {
        init(obs, legal);
    } else {
        shift(obs, legal);
    }
    last_tick_ = tick;
    check(!plan_.empty(), "rhea: empty plan in a running game");
    mutate(obs, legal);  // at least one mutation (04 §2)
    while (fm_calls_ < kBudget) {
        mutate(obs, legal);
    }
    return plan_[0];
}

}  // namespace poly::search
