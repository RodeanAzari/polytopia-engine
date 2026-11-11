# ADR-007: Evaluation protocol

- **Status:** Accepted (2026-11-09, user-approved); §6.2 amended 2026-11-10 (user-approved)
- **Date:** 2026-11-09

## Context
Every claim this project makes ("reproduces the paper", "beats RHEA", "the change helps") is a win rate.
docs/buildplan.md §9.2 and CLAUDE.md §10 fix the outline: round-robin vs. Tribes' agents on fixed map seeds with
seat swaps, Wilson 95% intervals with game counts, SPRT for engine changes, and the record fields. This ADR
pins the details those leave open, so that results from different sessions are comparable and nobody picks
the test after seeing the data.

Facts it rests on:
- Tribes always names one winner (04 §8.3). Its last tie-break is an unseeded RNG; ours is a stable sort, i.e.
  the lower tribe id (the first mover). Both only apply when score, techs, cities, production, wars declared
  and stars sent are all equal.
- Turn order follows the capitals' row-major order, not the seat (05 §1.2), so "seat" here means which agent
  plays which tribe on a given level; who moves first is a property of the level.
- Games are slow: Tribes' search-agent games took 270–670 s each (tribes-rr-20261108); ours run the
  forward model 5–6× faster. Game budgets have to be planned, not open-ended.
- The paper's Table 1 is 500 games per pair (25 levels × 20, 10 per tribe); see `docs/agents/00-paper-baselines.md`.

## Decision

### 1. Match unit: seat-swapped game pair
Every match is played as **pairs**: same level, same game seed, agent A as tribe 1 then as tribe 2. Counts are
always even per level, so tribe and first-move advantages cancel in expectation. Reported per game; SPRT
uses the pair (§5).

### 2. Seeds: fixed, recorded, split dev/test
- **Level seeds** come from fixed lists checked into `data/seeds/`: `paper-25.txt` (the 25 stock
  `tournament.json` seeds, for the paper replication only), `dev.txt` and `test.txt` (disjoint, generated once
  from a recorded master seed). **Tuning and SPRT use only dev; headline results use only test**, so no
  reported number comes from the seeds the engine was tuned on. Generated 2026-11-10 by
  `oracle/scripts/make_seed_lists.py` (master seed 20261110, 100 seeds each, disjoint from paper-25; `--check`
  verifies the files). Levels: `make tournament SEEDS=data/seeds/dev.txt` exports them via Tribes.
- **Game seed** = a fixed hash of (tournament id, level seed, repetition), shared by both games of a pair.
  Every RNG in the game and in our agents derives from it and the seat (CLAUDE.md §5), so any game on our
  simulator replays exactly from its CSV row. Tribes runs record the level seed but are only statistically
  reproducible (unseeded `GameState.copy()`; progress.md).

### 3. Win rate and intervals
- Result per game: win / loss / **draw**, where draw = the ranking reached the final (random or id) tie-break.
  The runner detects this from the ranking keys and records it; it never lets turn order decide a game.
- Win rate = (W + ½D) / N. 95% interval = **Wilson** (z = 1.96) on that rate with N games. Every table states
  N per cell. Draws are reported in a column of their own (expected ≈ 0).
- Per-agent aggregates pool all of that agent's games; per-pair cells are the primary numbers.
- Multiple comparisons: when a claim is "beats every agent in a set", each pairwise interval must clear 50%
  on its own, and the table shows all of them (no cherry-picking a subset).

### 4. Conditions
| Condition | Use | Settings |
|---|---|---|
| **Paper** | Phase 2 replication; Phase 3 headline vs. baselines | 2 players, 11×11, 1:1 land, Xin Xi vs. Imperius, Capitals, full obs, 50-turn cap, 2,000 FM calls per decision |
| **Fog** | Phase 3+ headline for our agents | as Paper, partial obs (our agents always see only their observation) |
| **Spread** | generalization check (CLAUDE.md §14) | sizes {11, 14, 16} × players {2, 4} × {Capitals, Score}, random tribes from the free set, partial obs |

Budget: baselines use 2,000 forward-model calls per decision call, as in the paper. Our agents are compared
at **equal FM calls** for the headline; wall-clock time per move is reported next to it, never instead of it.

### 5. SPRT for engine changes
- Statistic: **GSPRT on pentanomial game-pair outcomes** (score of a pair ∈ {0, ½, 1, 1½, 2}), the
  fishtest/Stockfish method; it uses the pair structure of §1 and is robust to seat imbalance.
- Hypotheses in logistic Elo: **H0: Δ = 0, H1: Δ = 20**, α = β = 0.05 (bounds ln(β/(1−α)) ≈ −2.94 and
  ln((1−β)/α) ≈ 2.94). Cap: 2,000 pairs; reaching the cap is reported as inconclusive, not as a pass.
- Opponent: the previous engine version (self-play A/B) on dev seeds, Fog condition. A change that passes
  SPRT is kept; its Elo estimate is reported with its interval.
- 20 Elo is a deliberate trade-off: fishtest uses 0–2 Elo bounds with ~10⁵ games; we can afford ~10³ pairs
  per test, which resolves ~20 Elo. Smaller gains are below what we can measure and are not claimed.

### 6. "Paper pattern reproduced" (Phase 2 DoD), decided before the run
On our simulator, Paper condition, 25 paper levels × 20 games (10 per seating) = 500 per pair:
1. every agent beats Random with the lower Wilson bound ≥ 95%;
2. **ordering as in Tribes' own round-robin at the pinned commit** (amended 2026-11-10, below): for each pair
   whose 50-game Tribes interval excludes 50%, our 500-game head-to-head must not exclude 50% in the opposite
   direction. Pairs Tribes leaves unresolved may go either way. On tribes-rr-20261108 the resolved pairs are:
   RB > RHEA, RB > MCTS, RB > OSLA, RB > MC, OSLA > MC, MCTS > MC, RHEA > MC, and every agent > Random;
   unresolved: RHEA–MCTS, RHEA–OSLA, MCTS–OSLA. The paper's ordering (RHEA > RB > MCTS > {MC, OSLA}) is
   compared and reported as a finding, not a pass criterion;
3. **cross-check vs. Tribes:** for each of the 15 pairs, our 500-game interval and the 50-game Tribes interval
   (tribes-rr-20261108) overlap. A pair that fails is investigated as a possible agent-reimplementation bug
   before any result is reported.

**Amendment 2026-11-10 (user-approved), §6.2.** Originally: "ordering by aggregate win rate: RHEA > RB > MCTS >
{MC, OSLA}; an inversion fails only if the head-to-head interval excludes 50% in the wrong direction". Tribes'
own agents fail that at commit 270e7e5 (tribes-rr-20261108: RB vs RHEA 74%, 60–84%; RB vs MCTS 70%, 56–81%),
so a faithful reimplementation could not pass it; the commit differs from the paper's (diplomacy and later
changes). The reference became Tribes' measured ordering. Decided before any of our search agents' tournament
results existed (only 4-game smoke runs). Rejected: keeping the paper ordering and reporting a known failure
(it would test the Tribes version gap, not our agents); dropping criterion 2 (criterion 3's 50-game intervals
are wide, so the direction check on resolved pairs still adds power).

### 7. Records
One CSV row per game in `data/results/<tournament-id>.csv`: game index, level seed, game seed, condition,
agents (name + params + git hash + checkpoint if any), tribes, turn order, result per seat incl. draw flag, scores,
turns played, FM calls used, seconds. The `.md` summary next to it holds the tables (W/D/L, rate, Wilson
interval, N), the hardware (CPU model, threads, RAM) and the exact command. Runs are resumable and append-only.

## Rejected alternatives
- **Plain standard errors (the paper's)**: symmetric, can cross 0/100% (paper's 0.00 (0.00) cells); Wilson is
  what CLAUDE.md §10 requires.
- **Elo/BayesElo ratings as the headline**: hide which matchup is weak; kept as a secondary column at most.
- **Fixed-N A/B tests for engine changes**: wastes games on clear results and invites stopping early when
  the number looks good; SPRT has pre-set error rates.
- **Trinomial (per-game) SPRT**: ignores the seat pairing and overstates variance where one tribe/seat is
  favoured; pentanomial is the current standard.
- **Letting the tie-break decide (Tribes RNG / our id order)**: in our sim it would systematically favour the
  first mover; counting as draw is neutral.
- **One seed set for tuning and reporting**: invites overfitting to the levels (CLAUDE.md §14).
- **Equal wall time as the headline budget**: depends on machine and implementation speed (ours is 5–6×
  faster than Tribes); equal FM calls compares the algorithms.

## Consequences
- `make tournament` needs: seed lists, the game-seed hash, draw detection, the condition presets, and the
  CSV/summary format above. The existing `tribes_tournament.py` already matches §1/§3 except draws and the
  extra columns; it stays as is for the in-flight cross-check run (Tribes never reports draws).
- An SPRT tool (`tools/sprt.py`) with its own tests against published fishtest examples.
- Phase 2's DoD is fixed by §6 before any of our agents' numbers exist.
