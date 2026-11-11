#include "search/puct.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include "poly/observation.hpp"
#include "search/diff_heuristic.hpp"
#include "search/rule_based.hpp"

namespace poly::search {

namespace {

constexpr double kInf = std::numeric_limits<double>::infinity();

}  // namespace

void softmax_priors(std::span<const int> scores, double temperature, std::vector<double>& out) {
    out.clear();
    if (scores.empty()) {
        return;
    }
    const int top = *std::ranges::max_element(scores);
    double sum = 0.0;
    for (const int s : scores) {
        out.push_back(std::exp((s - top) / temperature));
        sum += out.back();
    }
    for (double& p : out) {
        p /= sum;
    }
}

double normalise(double q, double lo, double hi) { return lo < hi ? (q - lo) / (hi - lo) : 0.5; }

double puct_score(double q_hat, double prior, int parent_visits, int edge_visits, double c,
                  bool maximise) {
    return (maximise ? q_hat : 1.0 - q_hat) +
           (c * prior * std::sqrt(static_cast<double>(parent_visits)) / (1.0 + edge_visits));
}

PuctAgent::PuctAgent(std::uint64_t seed, PuctParams params)
    : params_(params),
      rng_(seed),
      chance_(rng_.next()),
      states_(static_cast<std::size_t>(kMaxNodes)) {
    nodes_.reserve(static_cast<std::size_t>(kMaxNodes));
}

std::int32_t PuctAgent::add_node(std::int32_t parent, std::int32_t parent_edge,
                                 std::span<const Action> actions) {
    check(nodes_.size() < static_cast<std::size_t>(kMaxNodes), "puct: node pool exhausted");
    const auto index = static_cast<std::int32_t>(nodes_.size());
    const GameState& s = states_[static_cast<std::size_t>(index)];
    PuctNode n{.parent = parent,
               .parent_edge = parent_edge,
               .first_edge = static_cast<std::int32_t>(edges_.size()),
               .to_move = s.active_tribe,
               .terminal = s.game_over};
    if (!n.terminal) {
        // ADR-008 §3: Rule Based scores from the player to move's own observation of this state.
        const Observation o = observe(s, s.active_tribe);
        long long convert_scored = 0;
        scores_.clear();
        for (const Action& a : actions) {
            scores_.push_back(rule_based_score(o, a, convert_scored));
        }
        softmax_priors(scores_, params_.temperature, priors_);
        n.num_edges = static_cast<std::int32_t>(actions.size());
        for (std::size_t i = 0; i < actions.size(); ++i) {
            edges_.push_back(PuctEdge{.prior = priors_[i]});
            actions_.push_back(actions[i]);
        }
    }
    nodes_.push_back(n);
    nodes_.back().value = evaluate(index);
    return index;
}

// ADR-008 §4: Diff vs. the root from the searching player's view; a finished game is ±(the
// largest magnitude seen so far + 1), so it ranks beyond every non-terminal value.
double PuctAgent::evaluate(std::int32_t node) const {
    const GameState& s = states_[static_cast<std::size_t>(node)];
    const double diff = diff_heuristic(*root_, s, player_);
    if (!s.game_over) {
        return diff;
    }
    const double bound = std::max({std::abs(lo_), std::abs(hi_), std::abs(diff)}) + 1.0;
    return s.tribes[static_cast<std::size_t>(player_)].result == Result::Win ? bound : -bound;
}

std::size_t puct_select(const PuctNode& n, std::span<const PuctEdge> edges, double lo, double hi,
                        double c, bool maximise) {
    const double fpu = n.total / n.visits;
    std::size_t best = 0;
    double best_u = -kInf;
    for (std::size_t i = 0; i < edges.size(); ++i) {
        const PuctEdge& e = edges[i];
        const double q = e.visits > 0 ? e.total / e.visits : fpu;
        const double u = puct_score(normalise(q, lo, hi), e.prior, n.visits, e.visits, c, maximise);
        if (u > best_u) {
            best = i;
            best_u = u;
        }
    }
    return best;
}

void PuctAgent::backup(std::int32_t leaf, double v) {
    lo_ = std::min(lo_, v);
    hi_ = std::max(hi_, v);
    for (std::int32_t i = leaf; i >= 0;) {
        PuctNode& n = nodes_[static_cast<std::size_t>(i)];
        ++n.visits;
        n.total += v;
        if (n.parent_edge >= 0) {
            PuctEdge& e = edges_[static_cast<std::size_t>(n.parent_edge)];
            ++e.visits;
            e.total += v;
        }
        i = n.parent;
    }
}

std::size_t most_visited(std::span<const PuctEdge> edges) {
    std::size_t best = 0;
    for (std::size_t i = 1; i < edges.size(); ++i) {
        const PuctEdge& a = edges[i];
        const PuctEdge& b = edges[best];
        const bool more = a.visits > b.visits || (a.visits == b.visits && a.visits > 0 &&
                                                  a.total / a.visits > b.total / b.visits);
        if (more) {
            best = i;
        }
    }
    return best;
}

// One forward-model call (ADR-008 §4): edge `e` of `node` applied to a copy of its state.
std::int32_t PuctAgent::expand(std::int32_t node, std::int32_t e) {
    const auto c = static_cast<std::size_t>(nodes_.size());
    states_[c] = states_[static_cast<std::size_t>(node)];
    // The copy's own legal set: the root copy's, or the node's edges (its own set below the root).
    std::span<const Action> in_s(root_fresh_.begin(), root_fresh_.end());
    if (node != 0) {
        legal_.clear();  // a copy: actions_ grows in add_node
        for (const Action& a : actions(nodes_[static_cast<std::size_t>(node)])) {
            legal_.push_back(a);
        }
        in_s = std::span(legal_.begin(), legal_.end());
    }
    const bool executed =
        agent_advance(states_[c], actions_[static_cast<std::size_t>(e)], chance_, in_s);
    ++fm_calls_;
    if (executed) {
        legal_actions(states_[c], legal_);
        in_s = std::span(legal_.begin(), legal_.end());
    }  // else: unchanged state, the same own legal set
    const std::int32_t leaf = add_node(node, e, in_s);
    edges_[static_cast<std::size_t>(e)].child = leaf;
    return leaf;
}

Action PuctAgent::act(const Observation& obs, std::span<const Action> legal) {
    fm_calls_ = 0;
    nodes_.clear();
    edges_.clear();
    actions_.clear();
    if (legal.size() == 1) {
        return legal[0];
    }
    player_ = obs.observer();
    root_ = &obs.view();
    states_[0] = obs.view();
    legal_actions(obs.view(), root_fresh_);
    lo_ = kInf;
    hi_ = -kInf;
    add_node(-1, -1, legal);  // the root's edges: the environment's legal set
    backup(0, nodes_[0].value);

    // Revisiting a finished game costs no forward-model call, so a tree whose every path ends
    // in one would never spend the budget: iterations are capped too.
    for (int it = 0; fm_calls_ < kBudget && it < kMaxIterations; ++it) {
        std::int32_t node = 0;
        for (;;) {
            const PuctNode& n = nodes_[static_cast<std::size_t>(node)];
            if (n.terminal || n.num_edges == 0) {
                backup(node, n.value);  // revisit: its value again
                break;
            }
            const std::int32_t e =
                n.first_edge + static_cast<std::int32_t>(puct_select(
                                   n, edges(n), lo_, hi_, params_.c, n.to_move == player_));
            const std::int32_t child = edges_[static_cast<std::size_t>(e)].child;
            if (child < 0) {
                const std::int32_t leaf = expand(node, e);
                backup(leaf, nodes_[static_cast<std::size_t>(leaf)].value);
                break;
            }
            node = child;
        }
    }
    return legal[most_visited(edges(nodes_[0]))];  // ADR-008 §5
}

}  // namespace poly::search
