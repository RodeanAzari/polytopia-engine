#include "search/mc.hpp"

#include "search/diff_heuristic.hpp"
#include "search/root_groups.hpp"

namespace poly::search {

namespace {

// Uniform among `legal`, redrawn while End Turn (03 §2.1 a, §2.2); `legal` has another action.
template <typename List>
const Action& draw_not_end_turn(const List& legal, Rng& rng) {
    const auto n = static_cast<std::uint32_t>(legal.size());
    for (;;) {
        const Action& a = legal[rng.below(n)];
        if (a.type != ActionType::EndTurn) {
            return a;
        }
    }
}

}  // namespace

void guess_opponent_techs(GameState& s, int player, Chance& chance) {
    const int steps = s.tick / 2;
    for (int t = 0; t < s.num_tribes; ++t) {
        if (t == player) {
            continue;
        }
        for (int k = 0; k < steps; ++k) {
            research_random(s.tribes[static_cast<std::size_t>(t)], chance);
        }
    }
}

void RunningBest::add(std::size_t i, double value) {
    sum_[i] += value;
    ++count_[i];
    const double mean = sum_[i] / count_[i];
    if (mean > record_) {
        record_ = mean;
        best_ = i;
    }
}

Action McAgent::act(const Observation& obs, std::span<const Action> legal) {
    fm_calls_ = 0;
    if (legal.size() == 1) {
        return legal[0];
    }
    root_candidates(legal, rng_, candidates_);
    if (candidates_.empty()) {
        return Action{.type = ActionType::EndTurn, .actor = obs.observer()};
    }
    RunningBest best(candidates_.size());
    while (fm_calls_ < kBudget) {
        const Action& first = draw_not_end_turn(candidates_, rng_);
        const auto i = static_cast<std::size_t>(&first - candidates_.data());
        best.add(i, rollout(obs.view(), obs.observer(), legal, first));
    }
    return candidates_[best.best()];
}

// 03 §2.2. Forced End Turn never triggers within kDepth steps in the Paper configuration
// (countdown 21), so it is not modelled.
double McAgent::rollout(const GameState& root, int player, std::span<const Action> root_legal,
                        const Action& first) {
    static_assert(kDepth < 21, "forced End Turn would trigger: model the countdown (03 §2.2)");
    GameState s = root;
    guess_opponent_techs(s, player, chance_);
    legal_actions(s, fresh_);  // the copy's own set; legal_ is Tribes' cached (stale) list
    bool unchanged = true;     // no action executed yet: fresh_ is s's legal set
    legal_.clear();
    for (const Action& a : root_legal) {
        legal_.push_back(a);
    }
    Action a = first;
    for (int step = 1;; ++step) {
        const ActionList& in_s = unchanged ? fresh_ : legal_;
        const bool executed = agent_advance(s, a, chance_, {in_s.begin(), in_s.end()});
        ++fm_calls_;
        if (s.game_over || step == kDepth || fm_calls_ >= kBudget) {
            break;
        }
        if (executed) {  // else Tribes keeps the stale list (rules 04 §9)
            legal_actions(s, legal_);
            unchanged = false;
        }
        check(!legal_.empty(), "mc rollout: no legal action in a running game");
        a = legal_.size() == 1 ? legal_[0] : draw_not_end_turn(legal_, rng_);
    }
    return diff_heuristic(root, s, player);
}

}  // namespace poly::search
