# Portfolio captures

Media rules: docs/buildplan.md §13.2 (dated names in `docs/media/`, sidecar `.md` per result). A capture is done
when the user has confirmed it.

## Moment 1: simulator bit-exact (Phase 1, closed 2026-11-08)
| Capture | File | Status |
|---|---|---|
| Simulator speed vs. Tribes (plot) | `docs/media/2026-11-08-sim-speed.png` + `.md` | confirmed by the user 2026-11-08 |
| Replay viewer, true state | `docs/media/2026-11-08-replay-viewer.png` + `.md` | confirmed by the user 2026-11-08 |
| Replay viewer, fog view | `docs/media/2026-11-08-replay-viewer-fog.png` | confirmed by the user 2026-11-08 |
| Difftest headline (README "At a glance": 2,200 games, 4.5M states, 4.2M legal sets, 0 mismatches) | README | in README since 2026-11-07 |

## Upcoming (docs/buildplan.md §13.1)
| Capture | Phase | Status |
|---|---|---|
| Baseline round-robin table (Tribes agents and our reimplementations, CIs): `docs/media/2026-11-10-baseline-round-robin.png` + `.md`, README Results | 2 | confirmed by the user 2026-11-10 |
| Hero GIF: our agent beating RHEA, visit counts on the map. Candidate: `build/replays/hero-paper-candidate.gif` (local; PUCT as Xin-xi beats RHEA 2894–1471 by conquest in 16 ticks, test level 1716822222933, seed 178902269550811470, Paper; 122 frames, 7.8 MB) → `docs/media/2026-11-11-hero-puct-vs-rhea.gif` + `.md` | 3 | **awaiting user confirmation** |
| Win-rate table + chart vs. every baseline, Paper + Fog (200 games/cell, Wilson CIs): candidate `data/results/p3-headline.png` (local) → `docs/media/2026-11-11-p3-headline.{png,md}`, README Results | 3 | **awaiting user confirmation** |
| Macro-action vs. flat search comparison: orange rows of the headline chart + SPRT 3 (Elo +47.2, +19.8 to +75.2) in the sidecar | 3 | **awaiting user confirmation** |
| Win rate vs. training time | 4 | — |
| Demo video (60–120 s) | 5 | — |
