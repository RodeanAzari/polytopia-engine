# Progress

## Phase 1 — Simulator: DONE (closed by the user 2026-11-08)
Rule-facts sheet 01–05; C++ simulator bit-exact vs. Tribes (1,000/1,000 replayed games per obs mode, + Mind
Bender scenario); agent observation = Tribes' agent copy; `make bench` 5.3–6.3× Tribes; replay viewer; action
encoding (ADR-006). Detail: README, `docs/notebook/2026-11-0[5-8].md`, ADR-001…006.

## Phase 2 — Search baselines: DONE (closed by the user 2026-11-10)
Baselines reimplemented; ADR-007 §6 all pass (ours-rr-20261110, 7,500 games); media
`docs/media/2026-11-10-baseline-round-robin.{png,md}`. Open: our RHEA tends stronger than Tribes' (sidecar).

## Phase 3 — Action-level MCTS + heuristic priors: DONE (closed by the user 2026-11-13)
PUCT + Rule Based priors (ADR-008), K=4 determinization (ADR-009), macro mode (ADR-010); 9 SPRTs (ADR-008 §8)
→ defaults c 1.25, T 0.125, own-vision Diff, K 4, macro. Tuned headline (test seeds, 200 games/cell): every
95% lower bound > 50% vs. all 6 baselines, Paper + Fog; vs. RHEA 86.0% (80.5–90.1) Paper. Media
`docs/media/2026-11-13-p3-headline-tuned.{png,md}`, hero GIF `2026-11-11-hero-puct-vs-rhea` (T=1 agent, kept
by the user). Tools: `tools/sprt_run.sh`, `tools/headline.sh [jobs] [tag]`, `make plots-headline TAG=`,
`make replay-game`, `tools/replay_gif.py`.

## Phase 4 — Learned policy/value network: IN PROGRESS (started 2026-11-13)
GPU gate approved by the user 2026-11-13. Budget: local laptop only, 24 h pilot → ≤ 7 days → 14-day cap, no
cloud (`docs/compute-plan.md`; the desktop RTX 3060 as a second worker is undecided). `make setup-nn` = CUDA torch.
Done: ADR-011 (network; amended: compact features expanded in-model) + ADR-012 (self-play, LibTorch);
`encode_features` + `network_index`; records (`selfplay/`, `tools/selfplay`, `nn/records.py`); `nn/model.py`,
`pack.py`, `train.py`; `make selfplay RUN= GAMES=` / `make train RUN= STEPS=`; train seeds `data/seeds/train.txt`.
Running: bootstrap records, `make selfplay GAMES=2000 JOBS=8` (started 2026-11-13 16:23, ~300 games/h;
resumable: rerun the same command). Next: train on it (`make train RUN=bootstrap`), compare with RB priors;
then LibTorch in C++ (ADR-012 §1) and games/h with the net in the loop.
Open from Phase 3: Fog vs. RHEA/MCTS losses unchanged by tuning. Untried: leaf value form, tree reuse, Gumbel.
Trap: Claude Code kills idle background runs under system memory pressure (4× on 2026-11-12/13; other
sessions); all runners resume, so just rerun (or start Claude Code with CLAUDE_CODE_DISABLE_BG_SHELL_PRESSURE_REAP=1).

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
