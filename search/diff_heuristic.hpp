#pragma once

// Tribes' Diff heuristic (agents sheet 01 §4), the evaluation of its OSLA, MC, MCTS and RHEA
// baselines. Reproduces Tribes' quirks (active-tribe vision, units without a home city not
// counted); it is for the baselines, not for our own agents.

#include "poly/state.hpp"

namespace poly::search {

// Value of `later` relative to `ref` for `player`; both are the player's agent-side states (its
// observation, or a state stepped from it). 01 §4.2:
//   d_P − mean(d_i, i ≠ P) + Δ(P's connected cities) + Δ(visible tiles of the active tribe).
[[nodiscard]] double diff_heuristic(const GameState& ref, const GameState& later, int player);

}  // namespace poly::search
