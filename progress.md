# Progress

## Phase 1 — Simulator: DONE (closed by the user 2026-11-08)
Rule-facts sheet 01–05; C++ simulator bit-exact vs. Tribes (1,000/1,000 replayed games per obs mode, + Mind
Bender scenario); agent observation = Tribes' agent copy; `make bench` 5.3–6.3× Tribes; replay viewer; action
encoding (ADR-006). Detail: README, `docs/notebook/2026-11-0[5-8].md`, ADR-001…006.

## Phase 2 — Search baselines: DONE (closed by the user 2026-11-10)
Baselines reimplemented; ADR-007 §6 all pass (ours-rr-20261110, 7,500 games); media
`docs/media/2026-11-10-baseline-round-robin.{png,md}`. Open: our RHEA tends stronger than Tribes' (sidecar).

## Current: Phase 3 — Action-level MCTS + heuristic priors (IN PROGRESS, started 2026-11-10)
DoD (docs/buildplan.md §11): beats every baseline incl. RHEA with significance (test seeds, Paper condition,
our reimplementations: ADR-007 amendment); macro vs. flat; media = win-rate table + replay GIF over RHEA.
Design: ADR-008 (PUCT, Rule Based softmax priors, Diff leaf, min/max-normalised Q, 2,000 FM calls),
ADR-009 (determinize opponents' techs/stars/city stats; K trees, summed visits). Tuning: SPRT on dev only.

### Plan
1. ~~Flat PUCT agent~~ done (a7227f9, "PUCT" in the runner; batch RB scoring fc89bf2, ~55→~26 s/game).
   Smoke on 10 dev levels: vs RB 13/20, vs RHEA 17/20. Open: Diff's active-tribe-vision quirk (fog only: past
   End Turn the leaf value jumps by unseen tiles) → first SPRT experiment (own-vision Diff).
2. ~~`tools/sprt.py`~~ done (a84e6c5): `sprt.py <csv> A B` (exit 0 H1, 1 H0, 2 continue, 3 cap). Agent variants
   for A/B runs still need names with parameters in the runner.
3. ~~Determinization~~ done (4cade7b, `PUCT:K=n`); SPRT K=4 vs K=0 after the vision SPRT.
   **Running:** SPRT `vision-own` (log `data/results/sprt-vision-own.log`; resume = rerun
   `tools/sprt_run.sh vision-own PUCT:vision=own PUCT`). Don't rebuild build/release while it runs.
4. Tune c, T, K, leaf form (SPRT, dev, Fog condition).
5. Macro-actions: ADR-010, flat vs. macro comparison.
6. Headline: vs. every baseline on test seeds (Paper + Fog); media (table, replay GIF with visit counts).

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
