#pragma once

#include <string>

#include "poly/observation.hpp"
#include "poly/state.hpp"

namespace poly {

// Full native state in the oracle log's STATE schema (oracle/bridge/core/game/OracleDriver.java),
// from "STATE ..." through "END" without a trailing newline. A difftest is a text comparison.
std::string dump_state(const GameState& s);

// An observation (poly::observe) in the log's OBS schema, the oracle's dump of the agent's copy:
// "OBS" header; other tribes' monuments "{}" and their city / extra-unit lists sorted by id; a fog
// tile's city id 0 (Java's default, not -1); ghost cities (tribe -1) are not listed.
std::string dump_observation(const Observation& obs);

// A search state stepped from tribe `observer`'s observation (agent_advance), in the OBS schema
// with a "PSTATE" header: the oracle's probe dump (04 §9).
std::string dump_search_state(const GameState& s, int observer);

}  // namespace poly
