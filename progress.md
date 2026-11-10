# Progress

## Phase 1 — Simulator: DONE (closed by the user 2026-11-08)
Rule-facts sheet 01–05; C++ simulator bit-exact vs. Tribes (1,000/1,000 replayed games per obs mode, + Mind
Bender scenario); agent observation = Tribes' agent copy; `make bench` 5.3–6.3× Tribes; replay viewer; action
encoding (ADR-006). Detail: README, `docs/notebook/2026-11-0[5-8].md`, ADR-001…006.

## Current: Phase 2 — Search baselines (IN PROGRESS, started 2026-11-08)
DoD (docs/buildplan.md §11): MCTS and RHEA reimplemented; the paper's win-rate pattern reproduced; media =
baseline round-robin table.

### In flight (left running at end of session 2026-11-09)
- **Tribes reference round-robin** `tribes-rr-20261108` (~620/750 at session end). Resumable: rerun
  `.venv/bin/python oracle/scripts/tribes_tournament.py --jobs=6 --xmx=1g --out=data/results/tribes-rr-20261108.csv`;
  summary → `.md` next to it. 50 games/pair (paper: 500); it is the cross-check for our agents (user, 2026-11-08).
- **Final forward-model probe difftest** on commit `4918240` (binary frozen; sha in `build/fm-final.sha`), 4 configs
  sequentially: `build/fm-full.out` (200 games), `fm-po.out` (200, partial), `fm-mb.out` / `fm-mb-po.out` (60 each).
  `build/fm-final.status` lists exit codes and the binary checksum. At session end: full obs 152/152 so far.
  Record the totals in the notebook + 04 §9; any failure = a non-native rule still diverging.

### Done this session (2026-11-09; detail in `docs/notebook/2026-11-09.md`)
- ADR-007 evaluation protocol accepted. Agent facts sheets `docs/agents/01`–`05`.
- Rule facts 04 §9 (Tribes' agent-side forward model) + `poly::agent_advance` + probe difftest (`make
  difftest-fm`, in CI); 7 non-native rule divergences found and fixed (GameState::native, actor-map order,
  fog owner, ghost meetings, naval fog cost, stale legal list after a no-op, absent monuments).
- `search/` + `make tournament` (ADR-007 runner), Random and Rule Based. RB agrees with every Tribes RB decision
  (44,764 full + 22,744 partial obs). RB vs Random on our sim: 500-0-0 (99.2–100%); Tribes 49/50.

### Next
1. Record the final probe-difftest totals and the Tribes round-robin summary (`tribes-rr-20261108.md`).
2. Diff heuristic (sheet 01 §4, unit tests per term/quirk) on the observation, then OSLA, MC (sheet 03), MCTS
   (sheet 02), RHEA (sheet 04), all simulating with `agent_advance` (keep the stale legal list on a no-op).
   Validate each statistically vs. its Tribes counterpart (ADR-007 §6.3), then the 500/pair paper replication.
3. Measure `agent_advance`'s per-step legal-set membership check once a search agent exists (Rule 12).
4. dev/test seed lists (ADR-007 §2) before any Phase 3 tuning.

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
