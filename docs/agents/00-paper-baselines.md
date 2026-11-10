# Paper baselines: setup and results (Perez-Liebana et al., AIIDE 2020)

Source: D. Perez-Liebana, Y.-J. Hsu, S. Emmanouilidis, B. D. A. Khaleque, R. D. Gaina, "Tribes: A New
Turn-Based Strategy Game for AI Research", AIIDE-20, pp. 252–258. Local copy `docs/refs/tribes-aiide2020.pdf`
(gitignored). Read 2026-11-08. Numbers below are the paper's, transcribed; nothing here is our measurement.

## Experimental setup (§4.1)

| Item | Paper | Our Tribes run (`tribes-rr-20261108`) |
|---|---|---|
| Agents | RHEA, Rule Based, MCTS, MC, OSLA, Random | same 6 |
| Players / tribes | 2: Xin Xi vs. Imperius | same |
| Levels | 25 procedurally generated, 11×11, 1:1 water/land | stock `tournament.json` 25 seeds; generator `init(11, 3, 4, 0.5)` → 11×11, 0.5 land: matches |
| Games per pair | 500 = 25 levels × 20 reps, 10 with each tribe | **50** = 25 levels × 2 seatings (1 per tribe) |
| Mode / obs | Capitals, full observability; capped at 50 turns, most points wins | Capitals, full obs, `MAX_TURNS_CAPITALS = 50` |
| Budget | 2,000 forward-model `next` calls per decision (OSLA, MC, MCTS, RHEA) | `STOP_FMCALLS`, `num_fmcalls = 2000` |
| Heuristic | Diff: Φ = Δ(production, techs, score, cities, units, kills, Σ city levels), W = {5, 4, 0.1, 4, 2, 3, 2}; own Δ minus mean of opponents' Δ | `DIFF_HEURISTIC` (code adds connected cities and visible tiles: see progress.md) |
| MCTS | UCB, C = √2; no rollouts (evaluate expanded node); root prioritization; most-visited child | `K = √2`, `ROLOUTS_ENABLED = false`, `PRIORITIZE_ROOT = true` |
| Iteration length | 20 for MC, MCTS, RHEA (5, 10 worse) | `MAX_LENGTH = 20` |
| RHEA | population 1 (random-mutation hill climber), shift buffer | `POP_SIZE = 1` |
| MC | root prioritization | `PRIORITIZE_ROOT = true` |
| Forced End Turn | tried, discarded | `FORCE_TURN_END = false` |
| Disband / Destroy | removed from SFP agents' and RB's choices | per agent code (to check in the agent facts sheets) |

Root prioritization (MC, MCTS): the first action is drawn only from one action group (Unit, City or Tribe),
picked at random **once per decision call** (code; the paper's wording read as per iteration). Deeper steps use
all actions. Details and quirks: `01-common-and-diff-heuristic.md` §3.

Known differences from the paper's code: our Tribes commit (270e7e5) is later and includes diplomacy; the
paper's results are therefore a pattern to reproduce, not numbers to match exactly.

## Table 1 (left): row agent's win rate vs. column agent, 500 games per cell (standard error)

| | RHEA | RB | MCTS | MC | OSLA | RND |
|---|---|---|---|---|---|---|
| RHEA | — | 58.60 (2.20) | 63.00 (2.16) | 77.80 (1.86) | 74.80 (1.94) | 100.00 (0.00) |
| RB | 41.40 (2.20) | — | 56.20 (2.22) | 62.40 (2.17) | 70.20 (2.05) | 98.80 (0.49) |
| MCTS | 37.00 (2.16) | 43.80 (2.22) | — | 62.00 (2.17) | 60.80 (2.18) | 98.60 (0.53) |
| MC | 22.20 (1.86) | 37.60 (2.17) | 38.00 (2.17) | — | 54.80 (2.23) | 99.00 (0.44) |
| OSLA | 25.20 (1.94) | 29.80 (2.05) | 39.20 (2.18) | 45.20 (2.23) | — | 99.40 (0.35) |
| RND | 0.00 (0.00) | 1.20 (0.49) | 1.40 (0.53) | 1.00 (0.44) | 0.60 (0.35) | — |

## Table 1 (right): aggregates over each agent's 2,000 games (standard error)

| Agent | Wins % | Rank | Score | Techs % | Cities | Prod. |
|---|---|---|---|---|---|---|
| RHEA | 74.84 (1.50) | 1.25 (0.03) | 11610.56 (134.34) | 88.95 (1.78) | 2.68 (0.05) | 20.68 (0.41) |
| RB | 65.80 (1.32) | 1.34 (0.03) | 8076.32 (81.58) | 71.14 (1.42) | 2.98 (0.06) | 20.29 (0.41) |
| MCTS | 60.44 (1.21) | 1.40 (0.03) | 9966.55 (106.19) | 85.27 (1.71) | 2.23 (0.04) | 16.96 (0.34) |
| MC | 50.32 (1.01) | 1.50 (0.03) | 8065.96 (71.22) | 82.35 (1.65) | 2.23 (0.04) | 14.73 (0.29) |
| OSLA | 47.76 (0.96) | 1.52 (0.03) | 8927.05 (88.84) | 82.31 (1.65) | 2.05 (0.04) | 15.17 (0.30) |
| RND | 0.84 (0.02) | 1.99 (0.04) | 3708.76 (12.51) | 58.54 (1.17) | 0.39 (0.01) | 0.01 (0.00) |

The caption says "averaged across 2000 game ends", but 5 opponents × 500 games = 2,500 per agent. The
discrepancy is in the paper; we rely on the left table only.

**Paper pattern:** RHEA > RB > MCTS > MC ≈ OSLA ≫ Random; every agent beats Random ≥ 98.6%. Not the Phase 2 pass
criterion since 2026-11-10: Tribes at the pinned commit orders RB first (ADR-007 §6.2 amendment); we report
the comparison with the paper as a finding.

## Other facts from the paper

- Human check: RHEA vs. a "moderately good" human, 25 games (one per level): human won 25/25.
- Action-space analysis (400 RHEA-vs-RB games, 50 turns): 9.86 moves per turn (6.47 turns 1–25, 13.11 turns
  26–50); 54.47 legal actions per move (21.86 / 85.83); turn branching factor ~10^15 (10^7 / 10^23).
