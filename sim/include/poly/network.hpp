#pragma once

// Trade network: which cities are connected to their tribe's capital (rule facts 03a §6, 03b §10).

#include "poly/state.hpp"

namespace poly {

// Recomputes tribe p's connected cities and applies the population changes. In a non-native state
// (an observation) only the active tribe's network is recomputed (03b §10).
void recompute_network_tribe(GameState& s, int p);

// Sets or clears a tile's network flag, then recomputes every tribe in id order (03b §10).
void set_network(GameState& s, int x, int y, bool on);

}  // namespace poly
