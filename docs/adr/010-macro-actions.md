# ADR-010: Macro-actions: goal-directed unit moves and pruned never-played actions

- **Status:** Accepted (2026-11-11, user-approved; kept only if its SPRT passes)
- **Date:** 2026-11-11

## Context
docs/buildplan.md §7.2: collapse common sequences to cut the branching ("this unit attacks its best target",
"move toward the nearest enemy city", "build the best available building"); compare flat vs. macro (Phase 3
DoD, a plot in §13).

### Measured facts (2026-11-11, scratch tools, git de758e5 = 9c4170f code)
Rule Based vs. Rule Based, Fog, the 100 dev levels, every decision (98,617):

| | |
|---|---|
| decisions per turn | ~12.1 |
| legal actions per decision: mean / median / p90 / p99 / max | 56.7 / 35 / 141 / 285 / 510 |
| movable units per decision; destinations per such unit | 5.55; 6.4 |
| actions tied at Rule Based's top score, per decision | 23.2 |

| type | share of legal | share of PUCT prior (softmax T=1) | share of RB's choices |
|---|---|---|---|
| Move | 62.3% | 65.8% | 57.3% |
| Destroy | 9.9% | 2.4% | 0.0% |
| Send stars | 6.9% | 3.1% | 2.7% |
| Disband | 4.8% | 0.1% | 0.0% |
| Grow forest | 4.1% | 2.3% | 1.3% |
| everything else | 12.0% | 26.3% | 38.7% |

Default PUCT (K=0, own vision), probed every 40th decision of 20 of those games (462 positions): root
children 62.7, top root child 68.2% of root visits, max tree depth 7.8, principal variation (edges ≥ 10
visits) median 4 / p90 10 edges, **2.9 of them in our own turn**, crossing End Turn in 30.3% of positions.

So a 2,000-call tree sees about a quarter of our own turn; the leaf Diff mostly scores half-played turns. Moves
are most of the branching, and Rule Based's move scores (0/3/4/5) are too coarse to separate destinations.

## Decision
A **macro mode** of PUCT (`PUCT:macro=1`), changing only which edges a node gets; every edge stays one
primitive legal action (one forward-model call), so ADR-006 keys, ADR-008 selection/backup and ADR-009's
summed root visits are unchanged.

1. **Goal-directed moves.** Each unit's Move edges are replaced by at most one move per goal, the legal
   destination closest to it in Chebyshev distance, kept only if closer than the unit is now (ties: the
   destination nearest the unit, then legal order; a move's cost isn't in the action), computed from
   the mover's observation of the node state (as the priors are):
   - **Attack:** the nearest visible enemy unit;
   - **Siege:** the nearest visible enemy city;
   - **Capture:** the nearest visible village or unowned city;
   - **Explore:** the move revealing the most fogged tiles (Fog only);
   - **Defend:** the own city nearest to a visible enemy unit (one target for all units);
   - **Rule Based's pick:** the unit's best-scored move (so the macro set contains the baseline's move).
   Duplicates merge; goals with no target are skipped. Expected ≤ 6, typically 2–4 per unit (vs. 6.4).
2. **Pruned actions:** Destroy and Disband (Rule Based scores −2 and never plays them; 14.7% of legal edges).
   Send stars collapses to one amount per target (the largest legal). End Turn is never pruned.
3. **Unchanged:** all other action types, including every Attack (0.48 per decision; already few).
4. Priors: softmax of Rule Based scores over the reduced set (ADR-008 §3).
5. **Root:** the same reduction (the agent can't play a pruned action). Safety: if the reduced set is
   empty except End Turn while the legal set has other actions, fall back to the flat set.
6. **Evaluation:** SPRT `macro=1` vs. the current default (dev, Fog). The flat vs. macro plot for §13
   comes from the headline run (both variants vs. every baseline on test seeds).

## Rejected alternatives
- **Multi-action macros** (e.g. "move toward X, then attack best"): one edge = several forward-model calls and
  a sequence the opponent can't interrupt anyway; the single-move goals already collapse the branching where it
  is (Move). Revisit if the macro tree still doesn't reach End Turn.
- **Canonical actor order** (only the next unit/city acts at a node, plus "skip"): removes transpositions
  (n! orders of independent moves) and cuts branching further, but order does matter in Polytopia (clearing a
  tile, a kill freeing a path), and it adds skip nodes. Candidate for a later SPRT, not a macro.
- **Finishing our turn with Rule Based at each leaf:** ~11 forward-model calls per leaf cuts the tree to ~170
  nodes; it is a rollout, which buildplan §7.1 steers away from.
- **Pruning low-prior actions by threshold:** depends on T and on RB's coarse scores; goal moves are explainable
  and map to buildplan §7.2's examples.

## Consequences
- `search/macro.{hpp,cpp}`: `MacroFilter::select(obs, legal, rb_scores, keep)` (indices of a subset of
  `legal`; reusable buffers, no allocation per node), tests in `tests/search/test_macro.cpp`: each goal on
  hand-built positions, tie-breaks, pruning, fallback, hidden units don't attract, invariants on real legal
  sets of Rule Based games (both modes), and the PUCT tree (root and every inner node = the macro subset of
  that node's own legal set from the mover's view, priors over the kept scores). 16 injected bugs caught; one
  more (dropping the `seen` guard on enemy units) is equivalent: observations already omit hidden units.
- PUCT: `macro=0|1` in `parse_puct_params`. Below the root a node also keeps its full legal set
  (`agent_advance` needs the state's own set); the root's subset is chosen once from the agent's observation
  so all K trees share it.
- Measured (same 462 positions as above, 20 dev levels, Fog, K=0): root children 62.7 → 22.1, max depth
  7.8 → 10.5, tree reaches the opponent's turn 55.4% → 84.2%, own-turn principal-variation steps 2.91 →
  3.37, crossing End Turn 30.3% → 39.0%; same wall time.
