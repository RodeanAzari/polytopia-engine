#pragma once

// Turn flow and action execution (rule facts 03a, 03b, 04). Legality is checked with poly::check:
// an illegal action aborts loudly; callers are expected to pass generated (legal) actions.

#include <span>

#include "poly/action.hpp"
#include "poly/chance.hpp"
#include "poly/inline_vector.hpp"
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
// Exact ties in all ranking criteria are broken by tribe id here (and s.ranking_tied is set);
// Tribes uses an unseeded coin.
bool update_game_over(GameState& s);

// Executes a non-End-turn action of the active tribe, then refreshes the action-derived flags.
// Every random outcome comes from `chance`.
void apply_action(GameState& s, const Action& a, Chance& chance);

// One step of Tribes' agent-side forward model (rule facts 04 §9), as its search agents step a copy
// of their observation: a non-End-turn action runs like apply_action with no game-over check; End
// turn (ignored while the active tribe can't end its turn) runs turn end, the game-over check, then
// turn start for the next tribe that hasn't lost, without advancing the tick. For reproducing
// Tribes' baselines only; our own search simulates the game loop. An action that isn't legal in s
// changes nothing (Tribes' copy can't execute it), so an environment action may be passed. Returns
// whether it executed: when it didn't, Tribes keeps the copy's previous legal-action list (stale;
// at a search root, the environment's), so the caller must keep its list rather than recompute.
bool agent_advance(GameState& s, const Action& a, Chance& chance);
// The same, with s's own legal set (exactly legal_actions(s)) supplied by the caller, which
// saves generating it again (a search usually has it). Unused for End turn. Checked in Debug.
bool agent_advance(GameState& s, const Action& a, Chance& chance,
                   std::span<const Action> legal_in_s);

// The active tribe's legal actions (04 §5), in Tribes' generation order: cities (city list order),
// then units (city units, then tribe-owned units), then tribe actions. A city that can level up
// makes its level-up variants the only actions. Empty once the game is over.
// [Tribes note] Tribes' flattened list (getAllAvailableActions) puts tribe actions first and walks
// cities and units in HashMap key order; this order is ours, not that one.
inline constexpr int kMaxActions = 4096;  // most seen in 140 oracle games: 829
using ActionList = InlineVector<Action, kMaxActions>;
void legal_actions(const GameState& s, ActionList& out);

// A city's production (03a §1): level + production bonus + capital bonus, or its population when
// negative. Raw: no occupation penalty, no clamp.
int city_production(const City& c);

// Researches a uniformly random researchable tech (enum order, from `chance`) at no cost and for no
// score; nothing when none is. The ruins' Research bonus (03b §5).
void research_random(TribeState& t, Chance& chance);

// Unit u's Heal-others targets (03b §7): its own tribe's units within its range, below max HP.
inline constexpr int kMaxHealTargets = 24;
using HealTargets = InlineVector<Slot, kMaxHealTargets>;
void heal_targets(const GameState& s, Slot u, HealTargets& out);

// Slot of the city / unit with the given Tribes actor id; aborts if there is none.
Slot city_by_id(const GameState& s, std::int32_t id);
Slot unit_by_id(const GameState& s, std::int32_t id);

}  // namespace poly
