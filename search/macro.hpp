#pragma once

// Macro mode of PUCT (ADR-010): which of a node's legal actions become edges. Every kept edge is
// still one primitive legal action; only the set shrinks:
// - each unit's moves: at most one per goal (attack, siege, capture, explore, defend, Rule Based's
//   pick), the destination closest to the goal's target;
// - Destroy and Disband dropped; Send stars: only the largest amount per target tribe;
// - everything else kept.
// Computed from the observation only (no peeking through the fog).

#include <cstddef>
#include <span>
#include <vector>

#include "poly/action.hpp"
#include "poly/observation.hpp"

namespace poly::search {

struct Tile {
    int x = 0;
    int y = 0;
};

// Holds its buffers between calls (one per search: no heap allocation per node, CLAUDE.md §5).
class MacroFilter {
   public:
    // Indices into `legal` (ascending) of the actions macro mode keeps. `rb_scores[i]` is Rule
    // Based's score of legal[i] for the observer (the "Rule Based's pick" goal). If the reduction
    // would leave only End Turn while other actions are legal, every action is kept (ADR-010 §5).
    void select(const Observation& obs, std::span<const Action> legal,
                std::span<const int> rb_scores, std::vector<std::size_t>& keep);

   private:
    void collect_targets(const Observation& obs);
    void keep_unit_moves(const GameState& v, std::span<const Action> legal,
                         std::span<const int> rb_scores, std::size_t first);

    std::vector<Tile> enemy_units_, enemy_cities_, villages_, own_cities_, defend_;
    std::vector<bool> kept_, done_;
    std::vector<std::size_t> moves_;
};

}  // namespace poly::search
