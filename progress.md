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
- 2026-11-05: Real-game Phase 6 planned ([ADR-003](docs/adr/003-real-game-phase.md)): after Phase 5, hand-relayed only.

### Next
1. Rule-facts sheet, remaining sections: 03b capture/ruins/explorer/borders/roads, 04 turn flow,
   fog, diplomacy, win conditions, 05 map generation. Resolve the `[TBD]`s in section 01.
2. Design the state value type and the outcome-injection interface (ADR: state encoding). Include the
   move-generation decision: replicate Tribes' order-dependent Dijkstra + Java-like heap (§02.3).
3. Oracle bridge: log Tribes actions, per-step state and random outcomes.

### Known oracle caveats
- Search agents (MC/MCTS/RHEA) are not reproducible run-to-run: `GameState.copy()` uses an unseeded
  `new Random()`. Doesn't affect difftests (we replay logged actions), but tournament runs vs. them are
  only statistically reproducible.
- Ranking tie-break in `TribeResult` is unseeded (only matters on exact ties).
- This Tribes commit includes **diplomacy** (wars, sending stars), a post-paper extension → include in the rule-facts sheet.

### Open items
- Permission email to the Tribes authors drafted (`docs/tribes-permission-request.md`); not yet sent.
