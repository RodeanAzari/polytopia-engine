# Progress

## Current: Phase 1 — Simulator (IN PROGRESS)

### Done
- 2026-11-05: Licence check. Tribes has no licence → facts-only reimplementation, Tribes as a local
  oracle only ([ADR-002](docs/adr/002-rules-source.md)). Language: C++20 ([ADR-001](docs/adr/001-language-cpp20.md)).
- 2026-11-05: Repo scaffold: CMake + GoogleTest, Release + Debug/ASan/UBSan, clang-tidy, CI workflow.
  Seeded RNG (`sim/include/poly/rng.hpp`), 5/5 tests pass in both builds.
- 2026-11-05: Tribes cloned at `270e7e5` into `oracle/tribes/` (gitignored) via `make setup`.

- 2026-11-05: Java 17 + Ninja installed. Tribes compiles; our headless bridge `oracle/bridge/OracleRun.java`
  runs a game with all seeds fixed; `make oracle-smoke` confirms byte-identical output across runs.

- 2026-11-05: Rule-facts sheet section 1 (`docs/rules/01-constants.md`): all static tables + 3 Tribes quirks.
- 2026-11-05: Section 2 (`docs/rules/02-combat-movement.md`): turn status, attack, movement, pushes.
- 2026-11-05: Section 3a (`docs/rules/03a-cities-economy.md`): city growth, buildings, city actions, monuments, trade network.
- 2026-11-05: Section 3b (`docs/rules/03b-capture-ruins-roads.md`): capture, ruins, explorer, borders, roads,
  other unit actions. Two 03a errors fixed (city-tile radius, network scope). Tribes builds with no fog →
  difftest both obs modes ([ADR-004](docs/adr/004-difftest-observability.md)).
- 2026-11-05: Section 4 (`docs/rules/04-turns-diplomacy-end.md`): turn flow, action generation order, diplomacy,
  agent observation, game end + ranking. All earlier `[TBD]`s resolved; tick count and send-stars bound probed on the oracle.
- 2026-11-05: Real-game Phase 6 planned ([ADR-003](docs/adr/003-real-game-phase.md)): after Phase 5; real-game interface method open, fog-filtered.

### Next
1. Rule-facts sheet, last section: 05 map generation (LevelGenerator).
2. Design the state value type and the outcome-injection interface (ADR: state encoding). Include the
   move-generation decision: replicate Tribes' order-dependent Dijkstra + Java-like heap (§02.3).
3. Oracle bridge: log Tribes actions, per-step state and random outcomes. Two builds: stock (full obs) and
   `PLAY_WITH_FULL_OBS=false` (partial obs) — [ADR-004](docs/adr/004-difftest-observability.md).

### Known oracle caveats
- Search agents (MC/MCTS/RHEA) are not reproducible run-to-run: `GameState.copy()` uses an unseeded
  `new Random()`. Doesn't affect difftests (we replay logged actions), but tournament runs vs. them are
  only statistically reproducible.
- Ranking tie-break in `TribeResult` is unseeded (only matters on exact ties).
- This Tribes commit includes **diplomacy** (wars, sending stars), a post-paper extension → include in the rule-facts sheet.

### Open items
- Permission email to the Tribes authors drafted (`docs/tribes-permission-request.md`); not yet sent.
