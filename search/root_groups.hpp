#pragma once

// Root prioritization of Tribes' MC and MCTS baselines (agents sheet 01 §3): one action group
// (City, Unit or Tribe) is drawn per decision, and only its actions are root candidates.

#include <span>
#include <vector>

#include "poly/action.hpp"
#include "poly/rng.hpp"

namespace poly::search {

// Fills `out` with the drawn group's root candidates from `legal` (in their order); `out` is left
// empty when no group is available (the agent then ends its turn). Availability and membership:
// City: a city action other than Destroy exists; candidates exclude Destroy. Unit: any unit action
// exists; candidates include Disband. Tribe: more than one tribe action; candidates include End
// Turn. The group is uniform over the available ones, in the order City, Unit, Tribe.
void root_candidates(std::span<const Action> legal, Rng& rng, std::vector<Action>& out);

}  // namespace poly::search
