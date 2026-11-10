#include "search/mcts.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

#include "search/diff_heuristic.hpp"
#include "search/noise.hpp"
#include "search/root_groups.hpp"

namespace poly::search {

namespace {

constexpr double kEps = 1e-6;
constexpr double kInf = std::numeric_limits<double>::infinity();

double mean(const ChildStats& c) { return c.total / (c.visits + kEps); }

}  // namespace

double uct_value(const ChildStats& c, int parent_visits, double lo, double hi) {
    const double m = mean(c);
    const double exploit = lo < hi ? (m - lo) / (hi - lo) : m;
    return exploit +
           (std::numbers::sqrt2 * std::sqrt(std::log(parent_visits + 1.0) / (c.visits + kEps)));
}

std::size_t uct_select(std::span<const ChildStats> children, int parent_visits, double lo,
                       double hi, bool maximise, Rng& rng) {
    std::size_t best = 0;
    double best_u = maximise ? -kInf : kInf;
    for (std::size_t i = 0; i < children.size(); ++i) {
        const double u = noisy(uct_value(children[i], parent_visits, lo, hi), rng);
        if (maximise ? u > best_u : u < best_u) {
            best = i;
            best_u = u;
        }
    }
    return best;
}

std::size_t final_choice(std::span<const ChildStats> expanded, Rng& rng) {
    bool all_equal = true;
    for (const ChildStats& c : expanded) {
        all_equal = all_equal && c.visits == expanded[0].visits;
    }
    std::size_t best = 0;
    double best_v = -kInf;
    for (std::size_t i = 0; i < expanded.size(); ++i) {
        const double v =
            noisy(all_equal ? mean(expanded[i]) : static_cast<double>(expanded[i].visits), rng);
        if (v > best_v) {
            best = i;
            best_v = v;
        }
    }
    return best;
}

MctsAgent::MctsAgent(std::uint64_t seed)
    : rng_(seed), chance_(rng_.next()), states_(static_cast<std::size_t>(kMaxNodes)) {
    nodes_.reserve(static_cast<std::size_t>(kMaxNodes));
}

std::int32_t MctsAgent::add_node(std::int32_t parent, std::span<const Action> actions) {
    check(nodes_.size() < static_cast<std::size_t>(kMaxNodes), "mcts: node pool exhausted");
    MctsNode n;
    n.parent = parent;
    n.depth = parent < 0 ? 0 : nodes_[static_cast<std::size_t>(parent)].depth + 1;
    n.first_slot = static_cast<std::int32_t>(slots_.size());
    n.num_slots = static_cast<std::int32_t>(actions.size());
    n.lo = kInf;  // unset: lo ≥ hi, so means stay unnormalised (02 §3)
    n.hi = -kInf;
    for (const Action& a : actions) {
        slots_.push_back(MctsSlot{.action = a});
    }
    nodes_.push_back(n);
    return static_cast<std::int32_t>(nodes_.size() - 1);
}

// 02 §2 expansion of `node`: a uniformly random empty slot's action applied to a copy of its state.
std::int32_t MctsAgent::expand(std::int32_t node, std::span<const Action> root_legal) {
    const MctsNode& n = nodes_[static_cast<std::size_t>(node)];
    auto k =
        static_cast<std::int32_t>(rng_.below(static_cast<std::uint32_t>(n.num_slots - n.expanded)));
    std::int32_t slot = n.first_slot;
    for (;; ++slot) {
        if (slots_[static_cast<std::size_t>(slot)].child < 0 && k-- == 0) {
            break;
        }
    }
    const auto child_index = static_cast<std::size_t>(nodes_.size());
    GameState& s = states_[child_index];
    s = states_[static_cast<std::size_t>(node)];
    const bool executed = agent_advance(s, slots_[static_cast<std::size_t>(slot)].action, chance_);
    ++fm_calls_;
    std::span<const Action> actions;
    if (executed) {
        legal_actions(s, legal_);
        actions = std::span(legal_.begin(), legal_.end());
    } else if (node == 0) {  // stale list (04 §9): at the root, the environment's
        actions = root_legal;
    } else {  // stale list: the parent's (its slots are its whole list below the root)
        stale_.clear();
        for (const MctsSlot& sl : slots(nodes_[static_cast<std::size_t>(node)])) {
            stale_.push_back(sl.action);
        }
        actions = stale_;
    }
    const std::int32_t child = add_node(node, actions);
    slots_[static_cast<std::size_t>(slot)].child = child;
    ++nodes_[static_cast<std::size_t>(node)].expanded;
    return child;
}

Action MctsAgent::act(const Observation& obs, std::span<const Action> legal) {
    fm_calls_ = 0;
    nodes_.clear();
    slots_.clear();
    if (legal.size() == 1) {
        return legal[0];
    }
    root_candidates(legal, rng_, candidates_);
    if (candidates_.empty()) {
        return Action{.type = ActionType::EndTurn, .actor = obs.observer()};
    }
    player_ = obs.observer();
    const GameState& root = obs.view();
    states_[0] = root;
    add_node(-1, candidates_);
    while (fm_calls_ <= kBudget) {
        // Selection (02 §2): descend fully expanded nodes by UCT until a node can be expanded.
        std::int32_t node = 0;
        for (;;) {
            const MctsNode& n = nodes_[static_cast<std::size_t>(node)];
            const GameState& s = states_[static_cast<std::size_t>(node)];
            if (s.game_over || n.depth >= kDepth || n.num_slots == 0) {
                break;  // evaluated again
            }
            if (n.expanded < n.num_slots) {
                node = expand(node, legal);
                break;
            }
            scratch_.clear();
            for (const MctsSlot& sl : slots(n)) {
                const MctsNode& c = nodes_[static_cast<std::size_t>(sl.child)];
                scratch_.push_back({.total = c.total, .visits = c.visits});
            }
            const std::size_t i =
                uct_select(scratch_, n.visits, n.lo, n.hi, s.active_tribe == player_, rng_);
            node = slots_[static_cast<std::size_t>(n.first_slot) + i].child;
            ++fm_calls_;
        }
        // Evaluation and backup.
        const double value = diff_heuristic(root, states_[static_cast<std::size_t>(node)], player_);
        for (std::int32_t i = node; i >= 0; i = nodes_[static_cast<std::size_t>(i)].parent) {
            MctsNode& n = nodes_[static_cast<std::size_t>(i)];
            ++n.visits;
            n.total += value;
            n.lo = std::min(n.lo, value);
            n.hi = std::max(n.hi, value);
        }
    }
    // Final choice (02 §5).
    const MctsNode& r = nodes_[0];
    scratch_.clear();
    std::vector<std::size_t> index;
    const auto root_slots = slots(r);
    for (std::size_t i = 0; i < root_slots.size(); ++i) {
        const MctsSlot& sl = root_slots[i];
        if (sl.child >= 0) {
            const MctsNode& c = nodes_[static_cast<std::size_t>(sl.child)];
            scratch_.push_back({.total = c.total, .visits = c.visits});
            index.push_back(i);
        }
    }
    if (scratch_.empty()) {
        return candidates_[0];
    }
    return root_slots[index[final_choice(scratch_, rng_)]].action;
}

}  // namespace poly::search
