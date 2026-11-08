# Progress

## Phase 1 — Simulator: DONE (closed by the user 2026-11-08)
Rule-facts sheet 01–05; C++ simulator bit-exact vs. Tribes (1,000/1,000 replayed games per obs mode, + Mind
Bender scenario); agent observation = Tribes' agent copy; `make bench` 5.3–6.3× Tribes; replay viewer; action
encoding (ADR-006). Detail: README, `docs/notebook/2026-11-0[5-8].md`, ADR-001…006.

## Current: Phase 2 — Search baselines (IN PROGRESS, started 2026-11-08)
DoD (docs/buildplan.md §11): MCTS and RHEA reimplemented; the paper's win-rate pattern reproduced; media =
baseline round-robin table.

### In flight (2026-11-08)
- **Tribes reference round-robin running** (started ~14:30): `oracle/scripts/tribes_tournament.py --jobs=6
  --xmx=1g --out=data/results/tribes-rr-20261108.csv` (Random, Rule Based, OSLA, MC, MCTS, RHEA; 15 pairs × 25 stock
  level seeds × 2 seatings = 750 games; est. 6–8 h). Resumable: rerun the same command, finished games are skipped.
  Summary → `data/results/tribes-rr-20261108.md` (Wilson CIs per agent and pair). Search games: 270–670 s each.
- **Paper:** the AIIDE 2020 Tribes paper is not readable by our tools (ojs.aaai.org bot wall, core.ac.uk 403;
  a curl with a browser UA was rightly denied). User to save it as `docs/refs/tribes-aiide2020.pdf` (gitignored).
  Secondary source (arXiv 2104.08641) ranks RHEA > Rule Based > MCTS; not yet checked against the paper.

### Next
1. Read the paper: setup + results tables verbatim; align the tournament setup (players, mode, budgets) with it.
2. ADR-007 evaluation protocol (round-robin, seat swaps, fixed seeds, Wilson CIs, SPRT for engine changes).
3. Agent facts sheet (`docs/agents/`): Tribes' Diff heuristic (read: 5·production + 4·techs + 0.1·score +
   4·cities + 2·units + 3·kills + 2·city levels, as deltas vs. the root state, own minus mean of others, plus own
   connected cities and visible tiles), MCTS, RHEA, Rule Based. Ask the user first whether ADR-002's facts-only
   rule extends to agents (amendment), then reimplement in C++ (`search/`) and `make tournament` on our sim.

### Known oracle caveats
- Search agents (MC/MCTS/RHEA) are not reproducible run-to-run: `GameState.copy()` uses an unseeded
  `new Random()`. Doesn't affect difftests (we replay logged actions), but tournament runs vs. them are
  only statistically reproducible.
- Ranking tie-break in `TribeResult` is unseeded (only matters on exact ties).
- Tribe id (= turn order) follows the capitals' row-major order, not the requested order; seats bind by tribe
  type. Log the id→type map per game (05 §1.2).
- This Tribes commit includes **diplomacy** (wars, sending stars), a post-paper extension → include in the rule-facts sheet.

### Open items
- CI difftest job: full + Mind Bender + partial obs, actions v5: green on GitHub (run 42408782685, 2026-11-07).
- README.md written 2026-11-07 (§13.4 structure; dashes for unmeasured rows). Repo licence: MIT (user, 2026-11-07).
- ruff now in `.venv` (`make setup`), not yet in CI; 6 pre-existing findings in the older `oracle/scripts/*.py`.
- Permission email to the Tribes authors drafted (`docs/tribes-permission-request.md`); not yet sent.
