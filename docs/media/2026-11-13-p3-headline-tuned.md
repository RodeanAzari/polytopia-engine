# 2026-11-13-p3-headline-tuned.png: tuned PUCT vs. every baseline (Phase 3 final)

**Result:** the final Phase 3 agent (ADR-008 + ADR-009 + ADR-010 with the SPRT-tuned constants: c = 1.25,
prior temperature T = 0.125, own-vision Diff leaf, K = 4 determinizations, macro mode) beats every baseline
with significance in both the Paper (full observability) and Fog (partial observability) conditions: every
Wilson 95% lower bound is above 50%. The baselines are our reimplementations of Tribes' agents, which reproduce
Tribes' own results pair by pair ([2026-11-10-baseline-round-robin.md](2026-11-10-baseline-round-robin.md);
ADR-007 amendment). The same run at T = 1, before tuning, is
[2026-11-11-p3-headline.md](2026-11-11-p3-headline.md).

PUCT's win rate (W + D/2; no draws occurred), 200 games per cell, Wilson 95% interval:

| opponent | PUCT · Paper | flat PUCT · Paper | PUCT · Fog | flat PUCT · Fog |
|---|---|---|---|---|
| RHEA | 86.0% (80.5–90.1) | 77.0% (70.7–82.3) | 91.0% (86.2–94.2) | 88.0% (82.8–91.8) |
| Rule Based | 79.5% (73.4–84.5) | 56.5% (49.6–63.2) | 80.5% (74.5–85.4) | 74.0% (67.5–79.6) |
| MCTS | 89.0% (83.9–92.6) | — | 95.0% (91.0–97.3) | — |
| OSLA | 94.5% (90.4–96.9) | — | 98.0% (95.0–99.2) | — |
| MC | 98.5% (95.7–99.5) | — | 100.0% (98.1–100.0) | — |
| Random | 100.0% (98.1–100.0) | — | 100.0% (98.1–100.0) | — |

"Flat PUCT" is `PUCT:macro=0` (every legal action a child; ADR-010) with the same tuned constants, played
against the two strongest baselines.

## Provenance
- **Run:** `tools/headline.sh 8 -tuned` at git `2743c2a` (engine `32c133c`), 2026-11-13 00:42–11:58 (stopped
  once by Claude Code under system memory pressure after 11 cells, resumed; finished games are kept, so no game
  was replayed or dropped). Tournament ids `p3-headline-full` / `p3-headline-partial`, as in the T = 1 run, so
  every game uses the same game seed as its T = 1 counterpart (checked). Per-game rows:
  `data/results/p3-headline-tuned-*.csv`, log `data/results/p3-headline-tuned.log` (gitignored). Every game
  replays exactly from its CSV row (`make replay-game`).
- **Condition:** 100 test levels (`data/seeds/test.txt`; all tuning used dev seeds only), 2 players, Xin Xi
  vs. Imperius, Capitals, 50-tick cap, both seatings per level, 2,000 forward-model calls per decision for
  every search agent (ADR-007 §4).
- **Tuning that produced this agent:** SPRTs 4–9 on dev levels, ADR-008 §8 (T 1 → 0.5 → 0.25 → 0.125 each
  accepted at +31.4, +34.9, +21.7 Elo; c = 2.5 rejected at T = 1 and at T = 0.125).
- **Plot and table:** `make plots-headline TAG=-tuned`; prints the table and the "beats every baseline" check.
- **Hardware:** Intel i9-11950H (16 threads), WSL2 on Linux 6.18, 8 games in parallel.

## Paired comparison with the T = 1 run
Same game seed per level and seating, so each game has a T = 1 counterpart. Games that went from loss to win
/ win to loss, two-sided sign test:

| cell | T = 1 → tuned | improved / worsened | p |
|---|---|---|---|
| Paper vs. Rule Based | 65.5% → 79.5% | 42 / 14 | 0.0002 |
| Paper vs. MCTS | 75.0% → 89.0% | 38 / 10 | 0.00006 |
| Paper vs. RHEA | 77.5% → 86.0% | 28 / 11 | 0.01 |
| Paper vs. OSLA | 86.5% → 94.5% | 22 / 6 | 0.004 |
| Fog vs. Rule Based | 67.5% → 80.5% | 39 / 13 | 0.0004 |
| Fog vs. OSLA | 92.5% → 98.0% | 13 / 2 | 0.007 |
| Fog vs. RHEA | 91.0% → 91.0% | 11 / 11 | 1 |
| Fog vs. MCTS | 94.0% → 95.0% | 9 / 7 | 0.8 |

## Interpretation and caveats
- **Macro vs. flat** now separates in the headline itself: vs. Rule Based in Paper, 79.5% vs. 56.5% (disjoint
  intervals); the other three shared cells overlap. Head-to-head evidence remains SPRT 3 (Elo +47.2, +19.8 to
  +75.2, at T = 1). The flat variant gained less from sharper priors (only Fog vs. Rule Based moved: 40 / 19,
  p 0.009), consistent with macro mode's pruning and sharp priors both concentrating the search.
- **Where tuning helped.** Against RHEA and MCTS in Paper, the T = 1 agent won fights but split games decided
  on score at the tick cap (35/39 and 39/39). Tuned: more conquests (RHEA 120 → 142, MCTS 111 → 146 wins) and
  the cap games won (30/22, 32/14). Against Rule Based, conquest losses halved (40 → 19).
- **Fog vs. RHEA / MCTS did not move** (already 91–95%): the remaining losses there are not addressed by
  prior sharpness.
- **Rule Based is still the hardest opponent** (~80%): with T = 0.125, PUCT plays close to Rule Based's own
  choice unless search finds better, so their games are the closest to a mirror match.
- **Illegal actions:** 8 returned actions were illegal (no-ops), all in games with RHEA in them (Tribes' RHEA
  behaviour, as in Phase 2). No draws.
