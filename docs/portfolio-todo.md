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
| Hero GIF: our agent beating RHEA, visit counts on the map. `docs/media/2026-11-11-hero-puct-vs-rhea.gif` + `.md`, README hero (PUCT as Xin-xi beats RHEA 2894–1471 by conquest in 16 ticks, test level 1716822222933, seed 178902269550811470, Paper; 122 frames, 7.8 MB) | 3 | confirmed by the user 2026-11-11 |
| Win-rate table + chart vs. every baseline, Paper + Fog (200 games/cell, Wilson CIs): `docs/media/2026-11-11-p3-headline.{png,md}`, README Results + At a glance | 3 | confirmed by the user 2026-11-11 |
| Macro-action vs. flat search comparison: orange rows of the headline chart + SPRT 3 (Elo +47.2, +19.8 to +75.2) in the sidecar | 3 | confirmed by the user 2026-11-11 |
| Tuned Phase 3 headline (c 1.25, T 0.125; 16 cells × 200 games, same seeds as the T = 1 run, paired comparison): `docs/media/2026-11-13-p3-headline-tuned.{png,md}`, README At a glance + Results updated to it (T = 1 version kept as history) | 3 | confirmed by the user 2026-11-13 |
| **Moment: first learned net beating search-only.** Pilot SPRT (dev levels, Fog, 100 seat-swapped pairs): `PUCT:net=boot-vw1-s3000` vs. Phase 3 PUCT 127/200 = 63.5% (56.6–69.9%), +96.2 Elo (+54.7 to +140.5), H1 (notebook 2026-11-14). Proposed captures: sidecar `docs/media/2026-11-14-p4-pilot-sprt.md`; test-seed headline of the net agent vs. PUCT and RHEA, Paper + Fog [TBD, needs the GPU] | 4 | listed, awaiting the user |
| Win rate vs. training time | 4 | — |
| Demo video (60–120 s) | 5 | — |
