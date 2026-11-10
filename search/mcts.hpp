#pragma once

// Tribes' MCTS baseline (agents sheet 02): UCT, C = √2, depth 20, no rollouts (a new node is valued
// by the Diff heuristic), root prioritization (sheet 01 §3), 2,000-unit budget per decision. The
// tree runs on the agent-side forward model (rule facts 04 §9) from the observation.

#include <cstdint>
#include <span>
#include <vector>

#include "poly/chance.hpp"
#include "poly/rng.hpp"
#include "poly/rules.hpp"
#include "search/agent.hpp"

namespace poly::search {

struct MctsNode {
    std::int32_t parent = -1;
    std::int32_t depth = 0;
    std::int32_t first_slot = 0;  // into the slot pool
    std::int32_t num_slots = 0;
    std::int32_t expanded = 0;  // slots with a child
    std::int32_t visits = 0;
    double total = 0.0;
    double lo = 0.0;  // min and max of the values backed up through this node (02 §2)
    double hi = 0.0;
    bool root_state = true;  // no action executed on the path: the state equals the root's
};

struct MctsSlot {
    Action action;
    std::int32_t child = -1;
};

struct ChildStats {
    double total = 0.0;
    std::int32_t visits = 0;
};

// 02 §3: the UCT value of child c of a node with `parent_visits` visits and bounds [lo, hi]; the
// mean is normalised only when lo < hi.
double uct_value(const ChildStats& c, int parent_visits, double lo, double hi);

// 02 §3: index of the child with the largest noisy UCT value, or the smallest when !maximise
// (a node where the searching tribe is not active).
std::size_t uct_select(std::span<const ChildStats> children, int parent_visits, double lo,
                       double hi, bool maximise, Rng& rng);

// 02 §5: index (into `expanded`) of the most-visited root child, or of the highest mean when all
// have the same visit count; noisy tie-break either way. `expanded` is not empty.
std::size_t final_choice(std::span<const ChildStats> expanded, Rng& rng);

class MctsAgent final : public Agent {
   public:
    static constexpr int kBudget = 2000;           // 01 §1
    static constexpr int kDepth = 20;              // 01 §2
    static constexpr int kMaxNodes = kBudget + 2;  // every iteration costs ≥ 1 and adds ≤ 1 node

    explicit MctsAgent(std::uint64_t seed);

    Action act(const Observation& obs, std::span<const Action> legal) override;

    // The last decision's search (empty if it didn't search), for tests and statistics.
    [[nodiscard]] int last_fm_calls() const { return fm_calls_; }
    [[nodiscard]] std::span<const MctsNode> nodes() const { return nodes_; }
    [[nodiscard]] std::span<const MctsSlot> slots(const MctsNode& n) const {
        return std::span(slots_).subspan(static_cast<std::size_t>(n.first_slot),
                                         static_cast<std::size_t>(n.num_slots));
    }
    [[nodiscard]] const GameState& state(std::size_t node) const { return states_[node]; }

   private:
    std::int32_t expand(std::int32_t node, std::span<const Action> root_legal);
    std::int32_t add_node(std::int32_t parent, std::span<const Action> actions);

    Rng rng_;
    SampledChance chance_;  // random outcomes inside the search
    int player_ = 0;
    int fm_calls_ = 0;
    std::vector<MctsNode> nodes_;
    std::vector<MctsSlot> slots_;
    std::vector<GameState> states_;   // states_[i] belongs to nodes_[i]; kMaxNodes, reused
    std::vector<Action> candidates_;  // root candidates
    std::vector<Action> stale_;       // a no-op child's inherited list
    std::vector<ChildStats> scratch_;
    ActionList legal_;
    ActionList root_fresh_;  // the root copy's own legal set
};

}  // namespace poly::search
