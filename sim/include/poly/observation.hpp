#pragma once

// What an agent sees (rule facts 04 §7): the filtered copy Tribes hands to its agents, in both
// observability modes. Search and training only ever get this (CLAUDE.md: no peeking through the
// fog; ADR-005 §2: a distinct type, so a true GameState can't be passed where an Observation is
// expected); the replay difftest compares it with Tribes' own copy before every decision.
// Legal actions come from the environment, not from the observation: ~6% of fog decisions differ
// (04 §7).

#include "poly/state.hpp"

namespace poly {

class Observation;

// The observation of tribe `observer`, a masked GameState of the same shape:
// - tiles the observer can't see: Terrain::Fog, nothing else; seen tiles: resources masked by the
//   observer's tech;
// - other tribes: stars, kills, pacifist counter, techs, monuments and all lists hidden, vision all
//   set; their visible cities and units are listed (cities, tribe_units) in slot order;
// - other tribes' cities on visible tiles: population, production and unit list hidden; on hidden
//   tiles: kept only as a ghost (id, tribe -1, nothing else) so that a capital or a seen tile's
//   owner still names it; other tribes' units on hidden tiles are gone, visible ones have no home
//   city and no kills;
// - every city's points-worth is 0 (Tribes' copy doesn't carry it, own cities included).
[[nodiscard]] Observation observe(const GameState& s, int observer);

class Observation {
   public:
    [[nodiscard]] int observer() const { return observer_; }
    [[nodiscard]] const GameState& view() const { return masked_; }  // already masked

   private:
    friend Observation observe(const GameState& s, int observer);
    Observation(const GameState& s, int observer) : masked_(s), observer_(observer) {}

    GameState masked_;
    int observer_;
};

}  // namespace poly
