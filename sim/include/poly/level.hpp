#pragma once

#include <string>
#include <vector>

#include "poly/state.hpp"

namespace poly {

// Builds the initial state from a level description (rule facts 05 §1): one line per x, n cells per
// line, each "terrain[:extra]". Tribe ids follow the row-major order of the city cells. Throws
// std::invalid_argument on malformed input, unsupported sizes, unknown tribe keys and duplicate
// tribe types (Tribes crashes on those, 05 §1.2).
GameState load_level(const std::vector<std::string>& lines, GameMode mode, ObsMode obs);

}  // namespace poly
