# Agent facts 01: common framework and the Diff heuristic

Facts-only sheet (ADR-002 amendment 2026-11-08). Sources: the Tribes paper (§4) and the Tribes source at the
pinned commit 270e7e5, read only to extract the behaviour below. Our C++ agents are written from this sheet.
Rule references (01–05) are to `docs/rules/`.

## 1. How agents are called
- An agent is asked for **one action at a time**. The game calls it repeatedly during its tribe's turn until it
  returns End Turn or no action is available. There is no per-turn action cap, and the turn timer is off
  (`TURN_TIME_LIMITED = false`).
- Each call gets the **agent's observation** (04 §7), freshly made for the current state. Every search agent
  builds a new search from scratch on every call: no tree or plan is kept between calls, except RHEA's
  shifted buffer (sheet 04).
- **Budget per call: 2,000 forward-model calls** (`STOP_FMCALLS`, `num_fmcalls = 2000`) for OSLA, MC, MCTS and
  RHEA in the Paper condition (ADR-007 §4). The check is "stop once the count **exceeds** 2,000", made after a
  whole iteration, so a call can use slightly more. What counts as a call differs per agent (each sheet says).
- Seed: each agent gets its own seed and RNG. Tribes' search agents are still non-reproducible because the
  game-state copy draws from an unseeded RNG; ours take every draw from the seeded game RNG (ADR-007 §2).

## 2. Paper configuration (stock values set by the Tribes runner; our bridge sets the same)
| Setting | Value | Applies to |
|---|---|---|
| Heuristic | Diff (§4) | OSLA, MC, MCTS, RHEA |
| Search depth / rollout / genome length | 20 actions | MC, MCTS, RHEA |
| Forced End Turn | **off**: implemented as "force at depths divisible by length + 1 = 21", which never happens at depth < 20 | MC, MCTS |
| Root prioritization (§3) | on | MC, MCTS |
| MCTS rollouts | off | MCTS |
| RHEA population | 1 | RHEA |

## 3. Root prioritization (action groups)
When on, the agent first picks **one action group at random, once per call** (not once per iteration; the
paper's wording suggested otherwise, and `00-paper-baselines.md` is corrected). Only that group's actions are
candidates at the root; deeper steps use all legal actions.

Groups and their membership, decided on the current observation:
| Group | Available if | Root candidates |
|---|---|---|
| City | at least one city action other than Destroy | all city actions **except Destroy** |
| Unit | at least one unit action (Disband included in the test) | all unit actions, **Disband included** (quirk: the code filters Disband into a list it never uses) |
| Tribe | more than one tribe action (i.e. something besides End Turn) | all tribe actions, **End Turn included** |

- The group is drawn uniformly from the available groups.
- If **no** group is available, the agent returns End Turn without searching.
- Before any of this, MC and MCTS return the only legal action directly when exactly one exists (no search, no
  RNG draw).
- Group order for the draw is City, Unit, Tribe (only matters for RNG alignment, which we don't need).

## 4. Diff heuristic
Evaluates a later state S′ against a reference state S, both from the agent's (player P's) point of view.
In MC/MCTS/RHEA, S is the observation given to the current call (not the turn start); S′ is the state reached
by the search.

### 4.1 Per-tribe delta
For each tribe i (every player id in the game, including P), with Δx = x(S′) − x(S):

  d_i = 5·Δproduction + 4·Δtechs + 0.1·Δscore + 4·Δcities + 2·Δunits + 3·Δkills + 2·Δ(Σ city levels)

Term definitions (all read from the respective state as the agent's copy has them):
- **production**: sum of `production` over the tribe's city list, raw: no clamp at 0 and no occupation effect
  (same quantity as ranking key 5, 04 §8.3).
- **techs**: number of researched techs in the tribe's tech tree.
- **score**: the tribe's score.
- **cities**: length of the tribe's city list. **Σ city levels**: sum of `level` over that list.
- **units**: number of units **attached to the tribe's cities** (each city's unit list, summed). Units without a
  home city (tribe-owned: e.g. from conversions, ruins or Mind Bender) are **not** counted.
- **kills**: the tribe's kill counter.

### 4.2 Combination
  value = d_P − mean(d_i for i ≠ P) + Δ(P's connected cities) + Δ(visible tiles)

- mean over the other tribes: the plain arithmetic mean (one opponent in the Paper condition).
- **connected cities**: length of P's connected-cities list.
- **visible tiles**: number of `true` cells in the vision grid of **the tribe that is active in that state**
  (quirk: not necessarily P). Counted in S and in S′ separately.
- No normalisation here; MCTS's bounds normalisation is in sheet 02.

### 4.3 Consequences of the observation (not extra rules; they follow from 04 §7)
- Other tribes' kills, tech tree, city list and city-attached units are hidden (zero/empty) in the agent's copy;
  their visible cities show production 0. So opponents' d_i comes mostly from score, plus whatever changes
  inside the search (techs researched or cities captured by the opponent after End Turn in the tree).
- Full obs: every vision grid is all-true, so the visible-tiles term is always 0.
- Partial obs: opponents' grids are all-true in the copy. If S′ has an opponent active (after End Turn) and S
  has P active, the term jumps by (tiles on map − tiles P sees). Our implementation reproduces this, because
  the baseline must behave as Tribes' does; it is a known quirk, not something our own agents copy.

## 5. Verification plan (ADR-002 amendment: statistical)
- Unit tests on hand-built states for each term and each quirk above (non-city units excluded, active-tribe
  vision, raw production).
- Our agents vs. Tribes' agents through ADR-007 §6, criterion 3.
