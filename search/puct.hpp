#pragma once

// Our Phase 3 search (ADR-008): action-level PUCT, one node per decision (End Turn included, so the
// tree runs into the opponent's turn), priors = softmax of Tribes' Rule Based scores, leaf value =
// Diff heuristic vs. the root (no rollouts), Q normalised by the tree's min/max value, 2,000
// forward-model calls (expansions) per decision. Runs on the agent-side forward model (rule facts
// 04 §9) from the observation only.

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "poly/chance.hpp"
#include "poly/rng.hpp"
#include "poly/rules.hpp"
#include "search/agent.hpp"
#include "search/diff_heuristic.hpp"
#include "search/leaf_eval.hpp"
#include "search/macro.hpp"

namespace poly::search {

struct PuctEdge {  // its action is actions(node)[i]: contiguous, a node's legal set
    double prior = 0.0;
    std::int32_t child = -1;
    std::int32_t visits = 0;
    double total = 0.0;  // sum of backed-up values (searching player's view)
};

struct PuctNode {
    std::int32_t parent = -1;
    std::int32_t parent_edge = -1;  // index into the edge pool
    std::int32_t first_edge = 0;
    std::int32_t num_edges = 0;
    // Macro mode (ADR-010): the state's full legal set, of which the edges are a subset, in the
    // agent's legal pool (agent_advance needs exactly the state's own set). Unused otherwise.
    std::int32_t first_legal = 0;
    std::int32_t num_legal = 0;
    std::int32_t visits = 0;  // 1 (its own evaluation) + backups through it
    double total = 0.0;
    double value = 0.0;         // its own evaluation (ADR-008 §4)
    std::int16_t to_move = -1;  // active tribe in the node's state
    bool terminal = false;
};

struct PuctParams {
    double c = 1.25;                         // ADR-008 §2 start value
    double temperature = 0.125;              // ADR-008 §3 (start 1); 0.125 by SPRT 8, ADR-008 §8
    DiffVision vision = DiffVision::Player;  // ADR-008 §8: SPRT 1 (own vision beat Tribes' Diff)
    int determinizations = 4;  // ADR-009 K (0 = the observation); 4 by SPRT 2, ADR-008 §8
    bool macro = true;  // ADR-010 (goal-directed moves, pruned actions); by SPRT 3, ADR-008 §8
    int budget =
        2000;  // forward-model calls per decision: ADR-007 §4; self-play may use fewer (ADR-012)
};

// Agent variants for A/B runs (ADR-007 §5), as the tournament runner names them after "PUCT:": ""
// (the defaults) or key=value items separated by ':' (agent lists and CSV fields use ','):
// c=<double>, T=<double>, vision=active|own, K=0..8, macro=0|1, budget=<int>. Throws on anything
// else (unknown key, bad value, c, T or budget not positive).
PuctParams parse_puct_params(std::string_view spec);

// ADR-008 §3: p_i = exp((s_i − max s)/T) / Σ exp((s_j − max s)/T). `out` gets one prior per score.
void softmax_priors(std::span<const int> scores, double temperature, std::vector<double>& out);

// ADR-008 §2: min/max normalisation of q into [0, 1]; 0.5 when the tree has no spread yet.
[[nodiscard]] double normalise(double q, double lo, double hi);

// ADR-008 §2: the PUCT score of an edge, q_hat already normalised, from the view of the player to
// move: `maximise` false (an opponent node) scores 1 − q_hat.
[[nodiscard]] double puct_score(double q_hat, double prior, int parent_visits, int edge_visits,
                                double c, bool maximise);

// ADR-008 §2: index (into `edges`, the edges of node `n`) of the edge with the largest PUCT score;
// an unvisited edge's mean is the node's own (n.total / n.visits); first wins ties.
std::size_t puct_select(const PuctNode& n, std::span<const PuctEdge> edges, double lo, double hi,
                        double c, bool maximise);

// ADR-008 §5: index of the edge with the most visits; ties by higher mean, then the first.
std::size_t most_visited(std::span<const PuctEdge> edges);

class PuctAgent final : public Agent {
   public:
    static constexpr int kBudget = 2000;  // the default budget (PuctParams::budget, ADR-007 §4)
    static constexpr int kMaxNodes = kBudget + 1;
    static constexpr int kMaxIterations = 10 * kBudget;  // see search()

    // With `net` (not owned; outlives the agent), priors and leaf values come from it (ADR-011 §2)
    // instead of Rule Based scores and the Diff heuristic; everything else is unchanged.
    explicit PuctAgent(std::uint64_t seed, PuctParams params = {}, LeafEvaluator* net = nullptr);

    Action act(const Observation& obs, std::span<const Action> legal) override;

    // The last decision's search (its last tree; empty if it didn't search), for tests and
    // statistics. last_fm_calls counts every tree.
    [[nodiscard]] int last_fm_calls() const { return fm_calls_; }
    [[nodiscard]] std::span<const PuctNode> nodes() const { return nodes_; }
    [[nodiscard]] std::span<const PuctEdge> edges(const PuctNode& n) const {
        return std::span(edges_).subspan(static_cast<std::size_t>(n.first_edge),
                                         static_cast<std::size_t>(n.num_edges));
    }
    [[nodiscard]] std::span<const Action> actions(const PuctNode& n) const {
        return std::span(actions_).subspan(static_cast<std::size_t>(n.first_edge),
                                           static_cast<std::size_t>(n.num_edges));
    }
    [[nodiscard]] const GameState& state(std::size_t node) const { return states_[node]; }
    // Root edge stats summed over the last decision's trees (ADR-009 §2), in the order of the
    // root's edges: `legal`, or with macro=1 its macro subset (root_actions()).
    [[nodiscard]] std::span<const PuctEdge> summed() const { return summed_; }
    [[nodiscard]] std::span<const Action> root_actions() const { return root_legal_; }

   private:
    // The node for states_[nodes_.size()], with `actions` as its edges.
    std::int32_t add_node(std::int32_t parent, std::int32_t parent_edge,
                          std::span<const Action> actions);
    void search(std::span<const Action> legal, int budget);
    std::int32_t expand(std::int32_t node, std::int32_t e);
    void backup(std::int32_t leaf, double v);
    [[nodiscard]] double evaluate(std::int32_t node) const;
    double net_priors(const Observation& o, std::span<const Action> actions);

    PuctParams params_;
    LeafEvaluator* net_ = nullptr;
    Rng rng_;
    SampledChance chance_;  // random outcomes inside the search
    int player_ = 0;
    int fm_calls_ = 0;
    double lo_ = 0.0;  // min/max of every value backed up in this tree (ADR-008 §2)
    double hi_ = 0.0;
    GameState root_state_;  // the searched tree's root: the observation or a determinization
    std::vector<PuctEdge> summed_;  // root edge stats summed over the trees (ADR-009 §2)
    std::vector<PuctNode> nodes_;
    std::vector<PuctEdge> edges_;
    std::vector<Action> actions_;    // parallel to edges_
    std::vector<GameState> states_;  // states_[i] belongs to nodes_[i]; budget + 1, reused
    std::vector<int> scores_;
    std::vector<int> kept_scores_;
    std::vector<double> priors_;
    std::vector<Action> kept_actions_;  // net mode: the node's edges, for the evaluator
    MacroFilter macro_;
    std::vector<std::size_t> keep_;
    std::vector<Action> root_legal_;  // the root's edges: `legal`, or its macro subset (ADR-010 §5)
    std::vector<Action>
        legal_pool_;  // macro mode: each node's full legal set (PuctNode::first_legal)
    ActionList legal_;
    ActionList root_fresh_;  // the root copy's own legal set (agent_advance's membership check)
};

}  // namespace poly::search
