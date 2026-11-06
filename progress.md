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
- 2026-11-06: Section 5 (`docs/rules/05-map-generation.md`): map load (bit-exact) + generator (distribution).
  Rule-facts sheet complete. Fixed 03b §11: Luxidoor's capital starts at level 3 with walls.
- 2026-11-06: [ADR-005](docs/adr/005-state-encoding.md) accepted (state value type, `Chance`, move-gen replication).
- 2026-11-06: Oracle logging bridge (full obs): `make oracle-log`, `make oracle-equiv` (100 games = stock Game.run, 0 failures).
- 2026-11-06: C++ `GameState`, map load, state dump: `make difftest-init N=700` → 700/700 initial states match (2–8 players).
- 2026-11-05: Real-game Phase 6 planned ([ADR-003](docs/adr/003-real-game-phase.md)): after Phase 5; real-game interface method open, fog-filtered.

### Next
1. Design the state value type and the outcome-injection interface (ADR: state encoding). Include the
   move-generation decision: replicate Tribes' order-dependent Dijkstra + Java-like heap (§02.3).
2. Oracle bridge, remaining: partial-obs build (`PLAY_WITH_FULL_OBS=false`, [ADR-004](docs/adr/004-difftest-observability.md));
   canonical action format (action-encoding ADR; logs carry Tribes' action strings for now); log compression.
3. C++ replay: canonical actions + `Chance`/`ReplayChance`, then apply logged actions one at a time and diff every
   STATE (start with end turn / turn start, then unit moves and city actions).

### Known oracle caveats
- Search agents (MC/MCTS/RHEA) are not reproducible run-to-run: `GameState.copy()` uses an unseeded
  `new Random()`. Doesn't affect difftests (we replay logged actions), but tournament runs vs. them are
  only statistically reproducible.
- Ranking tie-break in `TribeResult` is unseeded (only matters on exact ties).
- Tribe id (= turn order) follows the capitals' row-major order, not the requested order; seats bind by tribe
  type. Log the id→type map per game (05 §1.2).
- This Tribes commit includes **diplomacy** (wars, sending stars), a post-paper extension → include in the rule-facts sheet.

### Open items
- ruff isn't installed locally and CI has no Python step yet: `oracle/scripts/*.py` is unlinted. Add with `make setup` venv.
- Permission email to the Tribes authors drafted (`docs/tribes-permission-request.md`); not yet sent.
