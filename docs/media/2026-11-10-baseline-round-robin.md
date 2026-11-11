# 2026-11-10-baseline-round-robin.png: search baselines, ours vs. Tribes

**Result:** our reimplementations of Tribes' five baselines (Random, Rule Based, OSLA, MC, MCTS, RHEA) pass all
three Phase 2 criteria of ADR-007 §6 on our simulator, paper condition, 500 games per pair (7,500 games):
1. every agent beats Random with a lower Wilson bound ≥ 98.3% (criterion: ≥ 95%);
2. none of the 12 pairs Tribes resolves is reversed (all 12 resolve the same way on ours);
3. all 15 per-pair intervals overlap Tribes' own 50-game intervals.

| pair (first agent's win rate) | ours (N; 95% CI) | Tribes (N; 95% CI) |
|---|---|---|
| RuleBased v RHEA | 64.6% (500; 60.3%–68.7%) | 74.0% (50; 60.4%–84.1%) |
| RuleBased v MCTS | 73.6% (500; 69.6%–77.3%) | 70.0% (50; 56.2%–80.9%) |
| RuleBased v OSLA | 83.6% (500; 80.1%–86.6%) | 84.0% (50; 71.5%–91.7%) |
| RuleBased v MC | 94.0% (500; 91.6%–95.8%) | 92.0% (50; 81.2%–96.8%) |
| RuleBased v Random | 99.8% (500; 98.9%–100.0%) | 98.0% (50; 89.5%–99.6%) |
| RHEA v MCTS | 61.4% (500; 57.1%–65.6%) | 54.0% (50; 40.4%–67.0%) |
| RHEA v OSLA | 62.4% (500; 58.1%–66.5%) | 58.0% (50; 44.2%–70.6%) |
| RHEA v MC | 92.2% (500; 89.5%–94.2%) | 84.0% (50; 71.5%–91.7%) |
| RHEA v Random | 100.0% (500; 99.2%–100.0%) | 98.0% (50; 89.5%–99.6%) |
| MCTS v OSLA | 51.2% (500; 46.8%–55.6%) | 58.0% (50; 44.2%–70.6%) |
| MCTS v MC | 87.0% (500; 83.8%–89.7%) | 82.0% (50; 69.2%–90.2%) |
| MCTS v Random | 100.0% (500; 99.2%–100.0%) | 100.0% (50; 92.9%–100.0%) |
| OSLA v MC | 82.4% (500; 78.8%–85.5%) | 80.0% (50; 67.0%–88.8%) |
| OSLA v Random | 100.0% (500; 99.2%–100.0%) | 100.0% (50; 92.9%–100.0%) |
| MC v Random | 99.4% (500; 98.3%–99.8%) | 100.0% (50; 92.9%–100.0%) |

| agent (pooled) | ours (N; 95% CI) | Tribes (N; 95% CI) |
|---|---|---|
| Rule Based | 83.1% (2,500; 81.6%–84.5%) | 83.6% (250; 78.5%–87.7%) |
| RHEA | 70.3% (2,500; 68.5%–72.0%) | 64.0% (250; 57.9%–69.7%) |
| MCTS | 60.6% (2,500; 58.7%–62.5%) | 63.2% (250; 57.1%–68.9%) |
| OSLA | 57.0% (2,500; 55.1%–59.0%) | 56.0% (250; 49.8%–62.0%) |
| MC | 28.8% (2,500; 27.0%–30.6%) | 32.4% (250; 26.9%–38.4%) |
| Random | 0.2% (2,500; 0.1%–0.4%) | 0.8% (250; 0.2%–2.9%) |

No draws in either run (ADR-007 §3).

## Provenance
- **Ours:** `ours-rr-20261110`, git `9bfae75`, `build/release/tournament --levels=build/levels/paper-25
  --agents=Random,RuleBased,OSLA,MC,MCTS,RHEA --reps=10 --obs=full --jobs=8`; per-game rows and summary in
  `data/results/ours-rr-20261110.{csv,md}` (gitignored). Every game replays exactly from its CSV row.
- **Tribes:** `tribes-rr-20261108`, Tribes commit `270e7e5`, the agents at stock `tournament.json` parameters,
  50 games per pair; `data/results/tribes-rr-20261108.{csv,md}` (gitignored; table copied in
  `docs/notebook/2026-11-10.md`). Statistically reproducible only (unseeded RNG in Tribes' state copy).
- **Condition (both):** ADR-007 Paper: 25 stock level seeds (`data/seeds/paper-25.txt`), 2 players, Xin Xi vs.
  Imperius, Capitals, full observability, both seatings per level, 2,000 forward-model calls per decision.
- **Plot and checks:** `tools/plot_round_robin.py <out.png> <ours.csv> <tribes.csv>` prints the table above and
  the §6 verdict (exits 1 on any failure; checked on a copy with one pair's results flipped, which fails).
- **Hardware:** Intel i9-11950H (16 threads), WSL2 on Linux 6.18, shared with other jobs; 110,070 s summed
  game time (≈ 4 h wall at 8 games in parallel).

## Interpretation and caveats
- **Ordering.** Ours: Rule Based > RHEA > MCTS ≈ OSLA > MC > Random. That is Tribes' ordering at this commit;
  the extra resolution from 500 games also separates RHEA > MCTS and RHEA > OSLA, which Tribes' 50 games leave
  open. The paper's ordering (RHEA > Rule Based > MCTS > {MC, OSLA}) does not hold on Tribes' own agents at
  `270e7e5` either (Rule Based beats RHEA 74%, 60.4–84.1%), which is why ADR-007 §6.2 was amended before
  these runs; the paper is compared, not matched.
- **Our RHEA may be slightly stronger than Tribes'.** It does better than Tribes' RHEA in all five of its
  pairs, and its pooled rate (70.3%) sits just above Tribes' interval (57.9–69.7%). Every per-pair interval
  overlaps, so no criterion fails; 5 of 5 in one direction is p ≈ 0.06 (two-sided sign test), so this is a
  tendency, not a finding. Resolving it would take more Tribes RHEA games (270–670 s each).
- **Illegal actions.** 14 returned actions were illegal (no-ops), in 12 of 7,500 games, all with RHEA in them.
  This is Tribes' own RHEA behaviour (agents sheet 04 §3.2/§4: a shifted plan can start with an infeasible
  action and is returned if no mutant beats it), and the runner treats it as Tribes does.
- Tribes' column has 50 games per pair, so its intervals are wide (±10–14 points); criterion 3 is a coarse
  check, which is why criterion 2 also checks direction.
