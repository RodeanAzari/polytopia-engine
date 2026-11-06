#pragma once

#include <string>

#include "poly/state.hpp"

namespace poly {

// Full native state in the oracle log's STATE schema (oracle/bridge/core/game/OracleDriver.java),
// from "STATE ..." through "END" without a trailing newline. A difftest is a text comparison.
std::string dump_state(const GameState& s);

}  // namespace poly
