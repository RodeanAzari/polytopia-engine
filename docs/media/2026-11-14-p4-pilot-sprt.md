# 2026-11-14-p4-pilot-sprt: first learned net beating search-only

**Result:** PUCT guided by a network trained on 2,000 self-play games of the Phase 3 agent beats the Phase 3
agent itself (same search, heuristic Rule Based priors and Diff leaf value) in an SPRT on dev levels with fog:
H1 accepted after the first batch.

| A (vs. Phase 3 PUCT) | pairs | pentanomial | games won by A (95% Wilson) | LLR | Elo (95%) | verdict |
|---|---|---|---|---|---|---|
| `PUCT:net=boot-vw1-s3000` (learned priors + learned value) | 100 | [7, 0, 59, 0, 34] | 127/200 = 63.5% (56.6–69.9%) | 3.476 | +96.2 (+54.7 to +140.5) | **H1 accepted** |
| `PUCT:leaf=diff:net=boot-vw025-s17000` (learned priors only) | 100 | [11, 0, 71, 0, 18] | 107/200 = 53.5% (46.6–60.3%) | 0.826 | +24.4 (−12.0 to +61.3) | no verdict (stopped at batch 2, 47/200; resumable) |

- By seat, the winning net: 65/100 as first player, 62/100 as second. No draws; median game 35 ticks.
- The learned value head is what wins: priors alone are not yet shown better than the hand-written ones.

**Scope (read before quoting):** dev levels, fog only, one opponent (the agent the net was trained from).
This is a pilot, not the Phase 4 headline. Test-seed result vs. PUCT and RHEA, Paper + Fog: [TBD], planned
after gen1 for both the bootstrap and the gen1 net (the first two points of "win rate vs. training time").

## Provenance
- **SPRT:** `tools/sprt_run.sh`, sprt.py defaults (H0 0 Elo, H1 +20 logistic Elo, α = β = 0.05), seat-swapped
  pairs, batch 1 of 200 games. Binary `build/runs/0b723cb-torch/tournament` (git `0b723cb`; the run's exit abort
  fixed later in `8628966` did not affect any game). Per-game rows `data/results/sprt-p4-netprior-netvalue-b1.csv`,
  log `.log` (gitignored). 2026-11-13 22:25 → 2026-11-14 06:38; detail in `docs/notebook/2026-11-14.md`.
- **Condition:** 100 dev levels (`data/seeds/dev.txt`; disjoint from train and test seeds), 2 players, Xin Xi
  vs. Imperius, Capitals, partial observation (fog), 50-tick cap, 2,000 forward-model calls per decision for
  both agents (ADR-007 §4).
- **Network:** ADR-011 (64 channels × 6 blocks), trained on the 2,000-game bootstrap (Phase 3 PUCT vs. itself,
  train seeds, 1.39 M decisions; 1,900 train / 100 validation games), value weight 1.0, checkpoint at step
  3,000 of 20,000 (lowest validation value MSE, 0.477). `data/nets/boot-vw1-s3000.ts` (gitignored).
- **Hardware:** Intel i9-11950H (16 threads), NVIDIA RTX A2000 Laptop GPU, WSL2 on Linux 6.18; the two pilot
  SPRTs shared the GPU for part of the run (throughput only, not results).
