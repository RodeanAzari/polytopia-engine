#pragma once

// ADR-009: one plausible full value for the tribe-level state an observation hides from its
// observer (other tribes' techs, stars and city population), built from the Observation alone plus
// the caller's random draws, so it can't leak what the observer doesn't see (the type admits no
// true GameState). Fogged tiles stay fog; other tribes' units keep no home city; production
// bonuses, kills and monuments stay hidden (0 / absent).

#include "poly/chance.hpp"
#include "poly/observation.hpp"
#include "poly/rng.hpp"

namespace poly::search {

// For every other tribe q in the game:
// - techs: its tribe type's starting tech, the techs its visible units, and the buildings on tiles
// of
//   its visible cities, require (with their parents), then random researches (research_random)
//   until it has 1 + ⌊tick / 2⌋ (the starting tech plus Tribes' MC estimate, agents sheet 03), or
//   more if the evidence says so;
// - each visible city's population: uniform below its population need (reaching it levels up);
// - stars: its visible income (Σ city production) × a uniform number of saved turns in {0, 1, 2}.
[[nodiscard]] GameState determinize(const Observation& obs, Rng& rng, Chance& chance);

}  // namespace poly::search
