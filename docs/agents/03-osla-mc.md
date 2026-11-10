# Agent facts 03: OSLA and MC

Facts-only sheet (ADR-002 amendment). Framework, root groups and Diff heuristic: sheet 01.

## 1. OSLA (one-step look-ahead)
Paper configuration: Diff heuristic, budget 2,000, **no** root prioritization.

Per call, on observation S:
1. If exactly one legal action exists: return it.
2. Default choice = End Turn; best value = −∞.
3. Go through the legal actions **in generation order**. Skip End Turn, Destroy and Disband. For every other
   action a: copy S, apply a (one forward-model call; legal actions of the result are not needed), value =
   Diff(S, result) with the noise tie-break (u + ε)(1 + ε(r − 0.5)), ε = 1e-6, one draw per evaluated action.
   Keep a if its noisy value is **strictly** greater than the best so far.
4. Stop after the last action, or as soon as 2,000 actions have been applied (only reached when > 2,000 legal
   actions; then later actions in generation order are never considered).
5. Return the kept action.

Consequence: OSLA ends its turn only when End Turn, Destroy and Disband are the only legal actions (any
evaluated action beats −∞, whatever its value). It never declines a non-negative-looking move.

## 2. MC (flat Monte Carlo)
Paper configuration: Diff heuristic, budget 2,000, root prioritization on, rollout length 20, forced End Turn
off (countdown 21; see below).

### 2.1 Per call, on observation S
1. If exactly one legal action exists: return it.
2. Root candidates = prioritized group (01 §3); none → return End Turn.
3. Budget counter = 0; best = none; best value = −∞. One running mean per root candidate.
4. Repeat until the counter ≥ 2,000 (checked after each rollout):
   a. Draw a root candidate uniformly; **redraw while it is End Turn** (possible only in the Tribe group).
   b. Value = rollout (§2.2) starting with that candidate.
   c. Add the value to that candidate's mean. If its **updated mean is strictly greater than the best value**,
      best := this candidate and best value := its mean.
5. Return best.

Quirk in 4c: the best value is a running record, never lowered. If a candidate's mean later falls, it stays
"best" until some mean beats its old record; the result is the candidate whose mean set the highest record, not
the argmax of final means. No noise tie-break.

### 2.2 Rollout
1. Copy S, then **give every other tribe ⌊tick / 2⌋ random techs** in the copy, where tick is S's turn counter:
   each step picks uniformly among that tribe's currently researchable techs (enumeration order of the tech
   list) and researches it, with no star cost and no score; a step does nothing once nothing is researchable.
   In the agent's observation those trees start empty (04 §7), so this is a guess at the opponents' techs.
   (S itself is not changed: the Diff reference keeps 0 opponent techs, so the opponents' Δtechs includes these.)
2. Countdown := 21. Step := 0. Action := the root candidate.
3. Loop:
   - If countdown = 0 and End Turn is legal: apply End Turn, countdown := 21. (Never reached within 20 steps in
     the Paper configuration.) Otherwise apply the current action and countdown −= 1.
   - Step += 1. Stop if the game is over, step = 20, or the budget counter ≥ 2,000 (a rollout can be cut short
     by the budget; it is still evaluated).
   - Next action: if exactly one action is legal, take it (if it is End Turn, countdown := 22); otherwise draw
     uniformly from the legal actions, **redrawing while it is End Turn**.
4. Value = Diff(S, final copy) (not normalised).

Consequence: rollouts end the turn only when End Turn is the only legal action; after that the opponent's
turn is played by the same random policy (also never choosing End Turn voluntarily). Rollouts include Destroy
and Disband after the first step.

### 2.3 Budget counter
+1 per action applied in a rollout, End Turn included. The research in 2.2 step 1 is not counted.

## 3. Notes for our implementation
- Same observation and forward model as Tribes; the random-research step uses our existing ruins research
  routine (it is the same "research a random researchable tech" rule; 03b), drawing from the agent's RNG.
- Unit tests: OSLA's skip set and strict-greater rule; MC's End Turn redraws, the running-record "best" quirk,
  the ⌊tick/2⌋ research on opponents only, budget check ≥ (not >, unlike MCTS).
