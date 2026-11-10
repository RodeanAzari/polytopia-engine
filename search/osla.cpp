#include "search/osla.hpp"

#include <limits>

#include "poly/rules.hpp"
#include "search/diff_heuristic.hpp"
#include "search/noise.hpp"

namespace poly::search {

Action OslaAgent::act(const Observation& obs, std::span<const Action> legal) {
    if (legal.size() == 1) {
        return legal[0];
    }
    const GameState& root = obs.view();
    Action best{.type = ActionType::EndTurn, .actor = obs.observer()};
    double best_value = -std::numeric_limits<double>::infinity();
    int applied = 0;
    for (const Action& a : legal) {
        if (a.type == ActionType::EndTurn || a.type == ActionType::Destroy ||
            a.type == ActionType::Disband) {
            continue;
        }
        if (applied >= kBudget) {
            break;
        }
        GameState next = root;
        agent_advance(next, a, chance_);  // a no-op if not legal in the observation
        ++applied;
        const double value = noisy(diff_heuristic(root, next, obs.observer()), rng_);
        if (value > best_value) {
            best = a;
            best_value = value;
        }
    }
    return best;
}

}  // namespace poly::search
