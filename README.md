# Polytopia Engine: a Bit-Exact Strategy-Game Simulator, Built to Train an AI

An AI for *The Battle of Polytopia*-style turn-based 4X strategy, aiming to **beat a competent human**, a bar
the published research agents haven't cleared: in the Tribes paper (AIIDE 2020) the best agent, RHEA, lost
every game to a moderately skilled player. The plan is a fast C++20 simulator, action-level Monte Carlo tree
search, and a policy/value network trained by self-play.

**Phase 1, the simulator, is under way, and its rules are verified against the reference.** Every rule is
written from a facts-only rule sheet, then checked by replaying games logged from
[Tribes](https://github.com/GAIGResearch/Tribes), the research framework's Java forward model, action by
action. In **240 whole games, every one of 375,201 game states and every one of 337,585 legal-action sets
matches the oracle exactly**, covering every action type Tribes has. Search and learning come next; until they
exist, every row below that needs them is a dash.

Every number here links to the lab notebook or decision record where it was measured, with the command that
reproduces it. Dashes mean not measured yet.

*Hero GIF (the agent beating RHEA, search visit counts on the map): Phase 3.*

## At a glance

| | |
|---|---|
| **Game and rules source** | The Polytopia rules as implemented by Tribes (commit `270e7e5`): 12 tribes, 24 techs, 12 unit types, 19 buildings, ruins, diplomacy, Capitals and Score modes. Reimplemented from a written [rule-facts sheet](docs/rules/); Tribes has no licence, so none of its code is used ([ADR-002](docs/adr/002-rules-source.md)) |
| **Differential test** | **Map load: 700/700** initial states (2–8 players, all 12 tribes, both modes). **Whole games: 140/140** normal games, 303,641 states, 278,332 legal-action sets; **100/100** Mind Bender scenario games, 71,560 states, 59,253 legal sets. Zero mismatches ([2026-11-06](docs/notebook/2026-11-06.md), [2026-11-07](docs/notebook/2026-11-07.md)) |
| **Simulator speed vs. Tribes** | — (`make bench`, Phase 1) |
| **State** | One trivially copyable value type, **32.7 KB**, ~1.1 µs per copy (i9-11950H, WSL2, g++ 15 -O3; [ADR-005](docs/adr/005-state-encoding.md)) |
| **Search method** | — (action-level MCTS with macro-actions, Phase 3) |
| **Win rate vs. RHEA** | — |
| **Win rate vs. all Tribes agents** | — |
| **Network and training compute** | — (Phase 4) |
| **Human results** | — (Phase 5) |
| **Tests** | 30 GoogleTest unit tests, Release and Debug with ASan/UBSan; every difftest also run under the sanitizers on a subset |
| **CI** | Build, unit tests, clang-tidy and sanitizers; a short replay difftest against Tribes on every push |

## Status

| Phase | State |
|---|---|
| 1 — Simulator | **In progress**: rule-facts sheet done; map load, every action type and legal-action generation bit-exact under full observability. Remaining: partial observability (fog), throughput benchmark |
| 2 — Search baselines | Not started |
| 3 — Action-level MCTS + heuristic priors | Not started |
| 4 — Learned policy/value network | Not started (needs GPU time) |
| 5 — Evaluation | Not started |
| 6 — Real game, private matches with consenting humans ([ADR-003](docs/adr/003-real-game-phase.md)) | Not started |

Full scope and each phase's definition of done: [docs/buildplan.md](docs/buildplan.md). Current work:
[progress.md](progress.md).

## Architecture

```mermaid
flowchart LR
    subgraph oracle["Oracle (local or CI only, never vendored)"]
        T["Tribes @ 270e7e5<br/>Java forward model"] --> B["Our logging bridge<br/>oracle/bridge"]
    end
    B -- "log: level, RNG draws,<br/>legal sets, actions, states" --> D["oracle_diff<br/>tools/"]
    subgraph sim["Simulator (C++20, sim/)"]
        S["GameState<br/>value type"] --> R["rules:<br/>legal_actions, apply_action"]
        R --> S
        C["Chance<br/>semantic random outcomes"] --> R
    end
    D -- "replays every action,<br/>compares every state + legal set" --> sim
    sim -.-> M["action-level MCTS<br/>(Phase 3)"] -.-> N["policy/value net,<br/>self-play (Phase 4)"]
```

Dashed: not built yet.

## Testing

**The simulator is the golden model, so it is tested against Tribes before anything is built on it.**

- **Rule-facts sheet first.** Tribes has no licence, so its code can't be ported. Instead every rule was read
  out of Tribes into our own words, with its quirks marked, in [docs/rules/](docs/rules/) (constants, combat and
  movement, cities and economy, capture/ruins/roads, turns/diplomacy/game end, map generation). The C++ is written
  from the sheet.
- **Differential replay.** A small Java bridge (ours) drives Tribes with every seed fixed and logs everything:
  the level, each draw on the game RNG and who drew it, the legal-action set before each decision, the action
  taken, and the full state after it. `oracle_diff` loads the level, replays each action, feeds Tribes' logged
  random outcomes back in through the `Chance` interface, and compares **every state field and every legal-action
  set**. A random draw requested for a different reason or range fails the game at that point.
- **Targeted scenarios** for rules normal games never reach. Heal others and Convert never occurred in 140
  normal games (one Mind Bender was spawned in all of them), so `make difftest-mindbender` starts every tribe with Philosophy and seats a small agent of ours
  that seeks those actions: 479 Converts and 226 Heals in 100 games, all matching.
- **Unit tests** for what the oracle corpus can't show: under full observability every tile is visible, so
  visibility checks are tested on hand-made states, as are rare paths (overlapping borders, uncapturable cities).
- **Tests are shown to fail.** Each change is checked by injecting realistic bugs (off-by-one ranges, wrong
  status transitions, skipped checks) and confirming a test catches each; the uncaught ones became new unit tests.
  The record is in the [lab notebook](docs/notebook/).

### Tribes quirks the difftest caught

Matching Tribes exactly means matching its bugs; each is recorded in the sheet as a **[Tribes quirk]**:

- A Rider that attacks and then moves ends **Finished**, not able to move again: the status transition is a chain
  of `if`s without `else`, so two transitions fire in one call ([02 §1](docs/rules/02-combat-movement.md)).
- A Battleship from ruins on water has **−1/−1 HP**, so any hit kills it
  ([03b §5](docs/rules/03b-capture-ruins-roads.md)).
- A city founded on a village inside another city's borders **can never be captured**: Capture looks up the
  tile's border owner ([03b §3](docs/rules/03b-capture-ruins-roads.md)).
- Philosophy gives an **80 %** research discount, not 20 % ([01](docs/rules/01-constants.md)).

## Simulator

- **State as a value** ([ADR-005](docs/adr/005-state-encoding.md)): fixed-capacity pools and inline vectors, no
  heap, trivially copyable, so search can copy it with `memcpy`. Actors keep Tribes' actor ids for logs.
- **Randomness as semantic outcomes.** Rules never draw raw numbers; they ask a `Chance` for "a ruin bonus",
  "a shuffle of these cities". Self-play samples them from one seeded RNG; the difftest replays Tribes' draws.
  Same seed, same game.
- **Legal actions in Tribes' generation order**, one shared legality predicate per action used by both
  generation and execution; an illegal action aborts loudly.
- **Move generation replicates Tribes' search exactly**, including its priority-queue tie-breaking, because the
  reachable set depends on expansion order ([02 §3](docs/rules/02-combat-movement.md)).

## Results

Phase 1 results so far are the difftest rows above. Win rates will be reported with game counts and 95 % Wilson
confidence intervals, agent versions, seeds and hardware; engine changes will be accepted by SPRT.

## Limitations

- **The rules are Tribes' subset, not the current commercial game**: Tribes models a 2020 version with
  simplifications and its own quirks (above), and some content the real game has is absent. Phase 6
  ([ADR-003](docs/adr/003-real-game-phase.md)) addresses the real game.
- **Full observability only, so far.** The pinned Tribes commit is built to play with full observability;
  the fog (partial observability) path needs a second oracle build and is the next difftest ([ADR-004](docs/adr/004-difftest-observability.md)).
- **Tribes' search agents are not reproducible run to run** (an unseeded RNG inside `GameState.copy()`), so games
  against them are reproducible only statistically. Difftests replay logged actions and are unaffected.
- **No speed number yet.** The simulator's throughput against Tribes has not been measured.

## Prior work

- [**Tribes**](https://github.com/GAIGResearch/Tribes) (Perez-Liebana et al., *Tribes: A New Turn-Based Strategy
  Game for AI Research*, AIIDE 2020): the open research reimplementation used here as the rules oracle and as
  baseline opponents (Random, Rule Based, OSLA, MC, MCTS, RHEA).
- **Polyfish** (Rust): an AlphaZero-style engine with Gumbel self-play; a comparable engine.
- **polyterra-env**: an RL environment on decompiled game logic; not used, since that logic remains Midjiwan's.

## Credits and licence notes

- *The Battle of Polytopia* is by **Midjiwan AB**. This is a non-commercial research project, not affiliated with
  or endorsed by Midjiwan. It contains no Midjiwan assets, art, decompiled code or extracted game data
  ([ADR-003](docs/adr/003-real-game-phase.md)). No bot is ever connected to the game's online multiplayer.
- **Tribes** (QMUL Game AI Research Group) has no licence. It is therefore never copied, vendored or
  redistributed here: `make setup` clones it at a pinned commit for local (and CI) use as an oracle only, and the
  simulator is written from our own rule sheet ([ADR-002](docs/adr/002-rules-source.md)).
- This repository's own code and documents are under the [MIT licence](LICENSE). The licence covers only what is
  in this repository; it grants nothing over Tribes or *The Battle of Polytopia*.

## Build and reproduce

Requires a C++20 compiler, CMake ≥ 3.24 and Ninja (GoogleTest is fetched by CMake), Java 17 for the oracle,
Python 3.11+, and clang-tidy for `make ci`.

| Target | Does |
|---|---|
| `make setup` | Clones Tribes at the pinned commit into `oracle/tribes/` (gitignored) and compiles it with our bridge |
| `make build test` | Release and Debug+ASan/UBSan builds; all unit tests in both |
| `make ci` | `test` plus clang-tidy (warnings are errors) |
| `make difftest-init N=700` | Map-load difftest: N initial states, 2–8 players, all tribes, both modes |
| `make difftest N=140` | Whole-game replay difftest: every state and legal-action set (~40 MB of log per game) |
| `make difftest-mindbender N=100` | The Mind Bender scenario (Heal others, Convert) |
| `make oracle-equiv N=25` | Checks our logging driver plays exactly like stock Tribes (`Game.run`) |
| `make oracle-log` | One full oracle log (`LEVEL`, `SEED`, `MODE`, `PLAYERS`, `TRIBES`) |
| `make bench`, `make tournament`, `make selfplay train`, `make plots` | Later phases; fail loudly until implemented |

## Repository layout

| Path | Contents |
|---|---|
| `sim/` | The simulator: state, map load, rules and action generation, movement, trade network, state dump |
| `tools/oracle_diff.cpp` | The replay difftest driver |
| `oracle/bridge/` | Our Java harness around Tribes: seeded runs, the logging driver, the Mind Bender scenario agent |
| `oracle/scripts/` | Corpus generation and the loop-equivalence check |
| `tests/sim/` | GoogleTest unit tests |
| `docs/rules/` | The rule-facts sheet (01–05) |
| `docs/adr/` | Decision records, rejected alternatives included |
| `docs/notebook/` | Lab notebook: every session, bugs as symptom → root cause → fix |
| `search/`, `selfplay/`, `nn/` | Phases 2–4 |

## Roadmap

- **Finish Phase 1:** the fog-of-war difftest, then simulator throughput against Tribes.
- **Phase 2–3:** search baselines, then action-level MCTS with heuristic priors and macro-actions, until it beats
  RHEA, with confidence intervals.
- **Phase 4:** a policy/value network trained by self-play (GPU time to be budgeted first).
- **Phase 5–6:** evaluation against every Tribes agent and against humans, then the real game, in private
  matches with consenting players only.
