#include "search/puct.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

#include "poly/observation.hpp"
#include "search/determinize.hpp"
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

namespace {

double positive(const std::string& key, const std::string& value) {
    std::size_t used = 0;
    double v = 0.0;
    try {
        v = std::stod(value, &used);
    } catch (const std::logic_error&) {
        used = 0;
    }
    if (used == 0 || used != value.size() || !(v > 0.0) || !std::isfinite(v)) {
        std::string msg = "puct: ";
        msg += key;
        msg += " must be a positive number, got '";
        msg += value;
        msg += "'";
        throw std::invalid_argument(msg);
    }
    return v;
}

int whole(const std::string& key, const std::string& value) {
    const double v = positive(key, value);
    if (v != std::floor(v) || v > 1e6) {
        throw std::invalid_argument("puct: " + key + " must be a whole number, got '" + value +
                                    "'");
    }
    return static_cast<int>(v);
}

}  // namespace

PuctParams parse_puct_params(std::string_view spec) {
    PuctParams p;
    while (!spec.empty()) {
        const std::size_t sep = spec.find(':');
        const std::string_view item = spec.substr(0, sep);
        spec = sep == std::string_view::npos ? std::string_view{} : spec.substr(sep + 1);
        const std::size_t eq = item.find('=');
        if (eq == std::string_view::npos) {
            throw std::invalid_argument("puct: expected key=value, got '" + std::string(item) +
                                        "'");
        }
        const std::string key(item.substr(0, eq));
        const std::string value(item.substr(eq + 1));
        if (key == "c" || key == "T") {
            const double v = positive(key, value);
            (key == "c" ? p.c : p.temperature) = v;
        } else if (key == "budget") {
            p.budget = whole(key, value);
        } else if (key == "K" && (value.size() == 1 && value[0] >= '0' && value[0] <= '8')) {
            p.determinizations = value[0] - '0';
        } else if (key == "macro" && (value == "0" || value == "1")) {
            p.macro = value == "1";
        } else if (key == "vision" && (value == "active" || value == "own")) {
            p.vision = value == "own" ? DiffVision::Player : DiffVision::ActiveTribe;
        } else {
            throw std::invalid_argument("puct: unknown parameter '" + std::string(item) + "'");
        }
    }
    return p;
}

PuctAgent::PuctAgent(std::uint64_t seed, PuctParams params, LeafEvaluator* net)
    : params_(params),
      net_(net),
      rng_(seed),
      chance_(rng_.next()),
      states_(static_cast<std::size_t>(params.budget) + 1) {
    check(params.budget > 0, "puct: budget must be positive");
    nodes_.reserve(states_.size());
}

std::int32_t PuctAgent::add_node(std::int32_t parent, std::int32_t parent_edge,
                                 std::span<const Action> actions) {
    check(nodes_.size() < states_.size(), "puct: node pool exhausted");
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
        // Rule Based scores: the priors, or with a network only the macro filter's input.
        const bool macro_here = params_.macro && index != 0;
        if (net_ == nullptr || macro_here) {
            long long convert_scored = 0;
            rule_based_scores(o, actions, scores_, convert_scored);
        }
        // ADR-010: below the root, macro mode keeps a subset (the root's set is reduced in act()).
        keep_.clear();
        if (macro_here) {
            n.first_legal = static_cast<std::int32_t>(legal_pool_.size());
            n.num_legal = static_cast<std::int32_t>(actions.size());
            legal_pool_.insert(legal_pool_.end(), actions.begin(), actions.end());
            macro_.select(o, actions, scores_, keep_);
        } else {
            for (std::size_t i = 0; i < actions.size(); ++i) {
                keep_.push_back(i);
            }
        }
        if (net_ != nullptr) {
            n.value = net_priors(o, actions);
        } else {
            kept_scores_.clear();
            for (const std::size_t i : keep_) {
                kept_scores_.push_back(scores_[i]);
            }
            softmax_priors(kept_scores_, params_.temperature, priors_);
        }
        n.num_edges = static_cast<std::int32_t>(keep_.size());
        for (std::size_t k = 0; k < keep_.size(); ++k) {
            edges_.push_back(PuctEdge{.prior = priors_[k]});
            actions_.push_back(actions[keep_[k]]);
        }
    }
    nodes_.push_back(n);
    if (net_ == nullptr || n.terminal) {
        nodes_.back().value = evaluate(index);
    }
    return index;
}

// ADR-011 §2: the network's priors over the kept edges (into priors_) and its value, from the
// mover's observation `o`, returned in the searching player's view (two players: negated at the
// opponent's nodes).
double PuctAgent::net_priors(const Observation& o, std::span<const Action> actions) {
    kept_actions_.clear();
    for (const std::size_t i : keep_) {
        kept_actions_.push_back(actions[i]);
    }
    double v = 0.0;
    net_->evaluate(o, kept_actions_, priors_, v);
    check(priors_.size() == keep_.size(), "puct: evaluator returned the wrong prior count");
    return o.observer() == player_ ? v : -v;
}

// ADR-008 §4: Diff vs. the root from the searching player's view; a finished game is ±(the
// largest magnitude seen so far + 1), so it ranks beyond every non-terminal value.
// With a network, only finished games are evaluated here: the exact result, +1 / 0 (ranking tie,
// ADR-007 §3) / -1, on the network's value scale.
double PuctAgent::evaluate(std::int32_t node) const {
    const GameState& s = states_[static_cast<std::size_t>(node)];
    if (net_ != nullptr) {
        if (s.ranking_tied) {
            return 0.0;
        }
        return s.tribes[static_cast<std::size_t>(player_)].result == Result::Win ? 1.0 : -1.0;
    }
    const double diff = diff_heuristic(root_state_, s, player_, params_.vision);
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
    check(c < states_.size(), "puct: node pool exhausted");
    states_[c] = states_[static_cast<std::size_t>(node)];
    // The copy's own legal set: the root copy's, or below the root the node's edges (macro mode:
    // the node's full set, of which the edges are a subset; ADR-010).
    std::span<const Action> in_s(root_fresh_.begin(), root_fresh_.end());
    if (node != 0) {
        const PuctNode& n = nodes_[static_cast<std::size_t>(node)];
        const std::span<const Action> own =
            params_.macro ? std::span<const Action>(legal_pool_)
                                .subspan(static_cast<std::size_t>(n.first_legal),
                                         static_cast<std::size_t>(n.num_legal))
                          : actions(n);
        legal_.clear();  // a copy: actions_ and legal_pool_ grow in add_node
        for (const Action& a : own) {
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

// One tree from root_state_ with `budget` forward-model calls (ADR-008).
void PuctAgent::search(std::span<const Action> legal, int budget) {
    nodes_.clear();
    edges_.clear();
    actions_.clear();
    legal_pool_.clear();
    states_[0] = root_state_;
    legal_actions(root_state_, root_fresh_);
    lo_ = kInf;
    hi_ = -kInf;
    add_node(-1, -1, legal);  // the root's edges: the environment's legal set
    backup(0, nodes_[0].value);
    // Revisiting a finished game costs no forward-model call, so a tree whose every path ends
    // in one would never spend the budget: iterations are capped too.
    const int stop = fm_calls_ + budget;
    for (int it = 0; fm_calls_ < stop && it < 10 * budget; ++it) {
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
    // ADR-010 §5: the root's edges are the macro subset of `legal`, chosen once from the agent's
    // own observation, so every determinized tree shares them.
    root_legal_.assign(legal.begin(), legal.end());
    if (params_.macro) {
        long long convert_scored = 0;
        rule_based_scores(obs, legal, scores_, convert_scored);
        macro_.select(obs, legal, scores_, keep_);
        root_legal_.clear();
        for (const std::size_t i : keep_) {
            root_legal_.push_back(legal[i]);
        }
        if (root_legal_.size() == 1) {
            return root_legal_[0];
        }
    }
    // ADR-009 §2: K determinizations, one tree each with an equal share of the budget, root visits
    // summed (the root's edges are the environment's legal set in every tree); K = 0 searches the
    // observation itself.
    const int trees = std::max(1, params_.determinizations);
    summed_.assign(root_legal_.size(), PuctEdge{});
    for (int k = 0; k < trees; ++k) {
        root_state_ = params_.determinizations == 0 ? obs.view() : determinize(obs, rng_, chance_);
        search(root_legal_, params_.budget / trees);
        const auto root_edges = edges(nodes_[0]);
        for (std::size_t i = 0; i < root_edges.size(); ++i) {
            summed_[i].visits += root_edges[i].visits;
            summed_[i].total += root_edges[i].total;
        }
    }
    return root_legal_[most_visited(summed_)];  // ADR-008 §5
}

}  // namespace poly::search
