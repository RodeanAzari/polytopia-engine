#pragma once

// Turn flow and action execution (rule facts 03a, 03b, 04). Legality is checked with poly::check:
// an illegal action aborts loudly; callers are expected to pass generated (legal) actions.

#include "poly/action.hpp"
#include "poly/chance.hpp"
#include "poly/state.hpp"

namespace poly {

// Advances to the next tribe whose result is still incomplete, in tribe-id order, increasing the
// tick when wrapping past the last tribe id (04 §1). Returns that tribe's id.
int next_turn_tribe(GameState& s);

// Turn start for tribe p (04 §3); makes p the active tribe and refreshes the action-derived flags.
void start_turn(GameState& s, int p);

// Turn end for the active tribe (04 §4): fresh units that can recover do so.
void end_turn(GameState& s);

// Game-end check (04 §8; Tribes' GameState.gameOver): in capitals mode a tribe that owns every
// starting capital wins; in score mode the game ends when at most one tribe has not lost; either
// mode ends when tick > the turn limit. When over, tribes are ranked and the first gets Win, the
// rest Loss. Sets and returns s.game_over. Callers run it after every action and turn end, and
// before each tick; it is not part of apply_action because Tribes' loop runs it after the action.
// Exact ties in all ranking criteria are broken by tribe id here; Tribes uses an unseeded coin.
bool update_game_over(GameState& s);

// Executes a non-End-turn action of the active tribe, then refreshes the action-derived flags.
// Every random outcome comes from `chance`.
void apply_action(GameState& s, const Action& a, Chance& chance);

// Slot of the city / unit with the given Tribes actor id; aborts if there is none.
Slot city_by_id(const GameState& s, std::int32_t id);
Slot unit_by_id(const GameState& s, std::int32_t id);

}  // namespace poly
