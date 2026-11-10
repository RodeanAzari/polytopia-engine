# Progress

## Phase 1 — Simulator: DONE (closed by the user 2026-11-08)
Rule-facts sheet 01–05; C++ simulator bit-exact vs. Tribes (1,000/1,000 replayed games per obs mode, + Mind
Bender scenario); agent observation = Tribes' agent copy; `make bench` 5.3–6.3× Tribes; replay viewer; action
encoding (ADR-006). Detail: README, `docs/notebook/2026-11-0[5-8].md`, ADR-001…006.

## Current: Phase 2 — Search baselines (IN PROGRESS, started 2026-11-08)
DoD (docs/buildplan.md §11): MCTS and RHEA reimplemented; ADR-007 §6 criteria (§6.2 amended 2026-11-10: Tribes' own ordering, RB first); media =
baseline round-robin table.

### Reference
- Tribes round-robin `tribes-rr-20261108` done (750 games): RB 83.6%, RHEA 64.0%, MCTS 63.2%, OSLA 56.0%, MC 32.4%,
  Random 0.8%; pairwise table in `data/results/tribes-rr-20261108.md` (gitignored; copy in notebook 2026-11-10).

### Done (2026-11-09/10; detail in `docs/notebook/2026-11-09.md`, `2026-11-10.md`)
- ADR-007, agent facts sheets 01–05, rule facts 04 §9 + `agent_advance`; final probe difftest 520 games, 0 failures.
- `search/`: tournament runner; Random, Rule Based (agrees with every Tribes RB decision), Diff heuristic,
  OSLA, MC (+ root groups), MCTS. `make tournament AGENTS=Random,RuleBased,OSLA,MC,MCTS`.

### Next
1. RHEA (sheet 04), on `agent_advance` (stale legal list on a no-op).
2. Validate OSLA/MC/MCTS/RHEA statistically vs. Tribes (ADR-007 §6.3), then the 500/pair paper replication.
3. Profile MC/MCTS (up to ~100 s/game): `agent_advance`'s per-step legal-set check + our own legal_actions = 2 generations
   per step (Rule 12: measure first).
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
