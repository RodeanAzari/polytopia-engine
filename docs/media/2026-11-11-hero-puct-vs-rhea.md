# 2026-11-11-hero-puct-vs-rhea.gif: PUCT beats RHEA, with the search's visit counts

**What it shows:** one game from the Phase 3 headline (Paper condition, test level `1716822222933`): our PUCT
agent as Xin Xi conquers RHEA's Imperius capital at tick 16 and wins 2894–1471. Each frame is one of PUCT's
decisions; arrows on the map are the search's top root actions, thickness by share of the summed visits of its
K=4 trees, with % labels at ≥ 2% and the ranked list in the side panel. On the capturing move 92% of the
visits went to the capture.

**Typical, not cherry-picked for drama:** a conquest win in 16 ticks; 16 is the median length of PUCT's
conquest wins over RHEA in that cell (120 of its 155 wins came before the tick cap).

## Provenance
- **Game:** row 150 of `data/results/p3-headline-full-PUCT_RHEA.csv` (gitignored): seat 0 PUCT, seat 1 RHEA,
  game seed `178902269550811470`, engine `d787779`, result WIN 2894–1471, 16 ticks, 186 decisions.
- **Re-recorded:** `make replay-game LEVEL=build/levels/test/level-2-Capitals-1716822222933.txt
  AGENTS=PUCT,RHEA SEED=178902269550811470` → replay (221 frames, sha256 prefix `84c6a364440faa76`); the
  re-recorded game matches its CSV row exactly (result, scores, ticks, decisions).
- **Rendered:** `tools/replay_gif.py <replay.json> <out.gif> --tribe 0 --decisions-only --ms 650
  --window 1040x720 --width 760`: 122 frames, 760×526, 7.8 MB (≤ 8 MB, buildplan §13.2); headless Chromium
  renders `tools/viewer/index.html`, so the GIF is exactly what the viewer shows.
- **View:** the true state (the agent itself saw full observability in this condition).
- **Agent version:** PUCT before c/T tuning (T = 1, engine `d787779`). The tuned agent (T = 0.125) wins 86.0%
  vs. RHEA in this condition ([2026-11-13-p3-headline-tuned.md](2026-11-13-p3-headline-tuned.md)); the user chose
  to keep this game as the hero (2026-11-13).
