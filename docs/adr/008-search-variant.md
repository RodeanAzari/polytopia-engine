# ADR-008: Phase 3 search: action-level PUCT with Rule Based priors

- **Status:** Accepted (2026-11-10, user-approved: starting design; its constants are tuned by SPRT, ADR-007 §5)
- **Date:** 2026-11-10

## Context
Phase 3 must beat every Tribes baseline, RHEA included, with significance (docs/buildplan.md §11), at equal
forward-model calls (ADR-007 §4: 2,000 per decision). Facts from Phase 2:
- Branching is large: hundreds of legal actions per decision, most of them unit moves; a turn is tens of
  decisions. Tribes' MCTS (UCT, uniform expansion, random rollouts) loses to its Rule Based agent 30%
  (50 games); ours to ours 26.4% (500; 22.7–30.4%).
- Tribes' Rule Based agent is the strongest baseline (83.1% pooled, 2,500 games; 81.6–84.5%) and scores every
  legal action from the observation alone, with no forward-model calls (`rule_based_score`, agents sheet 05):
  integers, about −2…5.
- docs/buildplan.md §7.1: one node per decision, End Turn an action; PUCT with heuristic priors; heuristic leaf
  value; no reliance on random rollouts (Tribes found they hurt).

## Decision
1. **Tree.** One node per decision; edges are single actions, End Turn included, so the tree continues into the
   opponent's turn. Two players: values are from the searching player's view; nodes where the opponent is to
   move pick the child that minimises it (more players, Spread condition: paranoid, decided when needed).
2. **Selection: PUCT.** a* = argmax Q̂(s,a) + c·P(s,a)·√N(s)/(1+N(s,a)) (min for opponent nodes, with Q̂ negated),
   Q̂ = Q normalised by the min/max value seen in this tree (MuZero-style, no scale constant to tune), unvisited
   children Q̂ = the parent's value. Start c = 1.25.
3. **Priors.** P(s,·) = softmax(score/T) over the node's legal actions, score = `rule_based_score` from the
   player-to-move's observation of the node's state. Start T = 1. Actions Rule Based never plays (Disband,
   Destroy, −2) keep their small prior; End Turn (−1) is never pruned.
4. **Leaf value.** No rollouts: the Diff heuristic (agents sheet 01) of the leaf vs. the root state, from the
   searching player's view; terminal states are ±(max seen + 1) by winner. Each expansion = one `agent_advance`
   = one forward-model call; scoring and evaluation are not forward-model calls (they read the state).
5. **Budget and choice.** Stop at 2,000 forward-model calls per decision (ADR-007 §4). Play the root child with
   the most visits (ties: higher Q). Nodes come from a fixed pool (CLAUDE.md §5); no tree reuse between
   decisions at first.
6. **Legal sets.** At the root, the environment's legal set (as for the baselines); below it, the copy's own
   set after each executed action.
7. **Tuning:** c, T, the leaf value's form, tree reuse and Gumbel root selection are SPRT experiments on dev
   seeds (ADR-007 §5), each kept only if it passes.
8. **Tuning record** (dev seeds, Fog, 2,000 FM calls per decision; pentanomial GSPRT H0 0 / H1 20 logistic Elo,
   α = β = 0.05; logs in `data/results/sprt-<name>.log`):
   - SPRT 1 `vision-own` (2026-11-11, git f8b92fa): Diff's visible-tiles term from the searching player's vision
     instead of the active tribe's (Tribes' quirk rewards ending the turn by the number of tiles we can't
     see). **H1 accepted** after 300 pairs, pentanomial [36, 0, 189, 0, 75], LLR 4.79 (bound 2.94), Elo
     +45.4 (95% +21.8 to +69.4). Adopted: `vision=own` is the default.
   - SPRT 2 `det-k4` (2026-11-11, git 9c4170f): K = 4 determinized trees of 500 calls each (ADR-009) vs. one
     tree on the observation. **H1 accepted** after 500 pairs, pentanomial [69, 0, 325, 0, 106], LLR 3.74
     (bound 2.94), Elo +25.8 (95% +7.8 to +43.8). Adopted: `K=4` is the default.
   - SPRT 3 `macro` (2026-11-11, git dd50860, both K = 4): macro mode (ADR-010) vs. flat. **H1 accepted** after
     200 pairs, pentanomial [20, 0, 133, 0, 47], LLR 3.72 (bound 2.94), Elo +47.2 (95% +19.8 to +75.2).
     Adopted: `macro=1` is the default; `PUCT:macro=0` is the flat variant.
   - SPRT 4 `c2.5` (2026-11-12, git fd12bd3; resumed at ea70775 after a memory-pressure kill, engine
     unchanged): c = 2.5 vs. 1.25. **H0 accepted** after 1,200 pairs, pentanomial [165, 0, 856, 0, 179], LLR
     −4.09 (bound −2.94), Elo +4.1 (95% −6.5 to +14.6). Kept c = 1.25. c = 0.6 not tried: the estimate's sign
     points the other way and the H1 bar is +20.
   - SPRT 5 `T2` (2026-11-12, git 693ec0d): prior temperature T = 2 vs. 1. **H0 accepted** after 300 pairs,
     pentanomial [71, 0, 192, 0, 37], LLR −6.68, Elo −39.5 (95% −63.2 to −16.2): flatter priors are worse.
     Kept T = 1; SPRT 6 tries T = 0.5.
   - SPRT 6 `T0.5` (2026-11-12, git d6e1073): T = 0.5 vs. 1. **H1 accepted** after 400 pairs, pentanomial
     [52, 0, 260, 0, 88], LLR 4.06, Elo +31.4 (95% +11.4 to +51.6). Adopted: T = 0.5 is the default. The
     Phase 3 headline (engine d787779) was measured at T = 1.

## Rejected alternatives
- **Gumbel (sequential halving at the root) from the start:** designed for 16–200 simulations; at 2,000 the gain
  is uncertain and it is harder to debug first. Kept as an SPRT experiment.
- **Plain UCT without priors (Tribes' MCTS minus rollouts):** with hundreds of actions per node, 2,000
  expansions barely visit each root child once; the measured Tribes MCTS already sits below Rule Based.
- **Random rollouts:** buildplan §7.1 and Tribes' experience; they also spend forward-model calls the budget
  counts.
- **Priors from a hand-written policy of our own:** Rule Based scores already exist, are validated against
  Tribes decision by decision, and are the strongest baseline; a new heuristic would be untested.
- **Unbounded or tanh-squashed Diff values:** need a scale constant; min/max normalisation needs none.

## Consequences
- `search/puct.{hpp,cpp}` (agent "PUCT"), tests from this ADR (selection formula, opponent minimisation, prior
  softmax, budget counting, terminal values), bug injection as for the baselines.
- Hidden information is ADR-009; macro-actions get their own ADR when the flat agent works.
