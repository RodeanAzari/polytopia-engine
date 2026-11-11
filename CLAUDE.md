Project ID: P8
@~/fpga-coord/PROTOCOL.md

# CLAUDE.md — Project 8: Polytopia Engine

An AI that plays *The Battle of Polytopia* well enough to beat a competent human: a C++20 simulator (bit-exact vs. the Tribes Java forward model), action-level MCTS, and a policy/value net trained by self-play. Portfolio project: evidence (win rates with CIs, media, README) is half the deliverable. **A phase without its numbers and media is not done.**

Full spec, prior art, phase checklists, out-of-scope list and portfolio deliverables: **`docs/buildplan.md`** (not imported; same section numbers). Current work: `progress.md`.

## Current status

<!-- Claude: update as phases complete. Never mark a phase done until its checklist in
     docs/buildplan.md §11 passes in full, INCLUDING the portfolio items (§13). -->

| Phase | Status | Key result |
|---|---|---|
| 1 — Simulator | DONE (2026-11-08) | Rule-facts sheet 01–05; map-load difftest 700/700; all action types; replay difftest incl. legal-action sets, full + partial obs: 1,000/1,000 games each (+ 200 Mind Bender); agent observation matches Tribes' copy; 5–6× Tribes' speed; replay viewer; action encoding (ADR-006) |
| 2 — Search baselines | DONE (2026-11-10) | Baselines reimplemented; ADR-007 §6 all pass (7,500 games, every pair overlaps Tribes'); `docs/media/2026-11-10-baseline-round-robin.md` |
| 3 — Action-level MCTS + heuristic priors | IN PROGRESS | Design: ADR-008 (PUCT + RB priors), ADR-009 (determinization) |
| 4 — Learned policy/value network | NOT STARTED | — |
| 5 — Evaluation | NOT STARTED | — |
| 6 — Real game (ADR-003) | NOT STARTED | — |

## Rules for Claude

- **Follow the imported fpga-coord protocol** at the start and end of every session (this project has one gate: GPU access).
- **Never invent a number.** Win rates always come with game counts and confidence intervals; unmeasured = `[TBD]`.
- **The simulator is the golden model.** No search or training work on rules that haven't passed the differential test against Tribes.
- **No peeking through the fog.** The agent only ever sees its observation; any hidden-information leak in search or training invalidates results.
- **Record decisions** as ADRs, including rejected alternatives.
- **Portfolio moments:** simulator bit-exact, first win over RHEA, first learned net beating search-only, first human win: generate plots, list captures, add to `docs/portfolio-todo.md`, wait for confirmation. Media rules: `docs/buildplan.md` §13.2.
- **Lab notebook** every session (bugs as symptom → root cause → fix); **progress.md** at start.
- **Stay in scope** (`docs/buildplan.md` §12). Real-game play happens in Phase 6 (ADR-003).
- **Keep always-loaded docs lean.** CLAUDE.md ≤ 250 lines and progress.md ≤ 150 lines. Trim them at the end of every session that edits them, and at once if either is over. Keep only what a future session would get wrong without being told (current state, open work, rules, traps that cost real time). Cut finished-work narratives, dated history, reasons the code, tests, commits or reports already record, and duplicates of other docs. Move what must be kept but isn't needed every turn (the spec, append-only measurement tables, long verification detail) to files that aren't imported, such as `docs/buildplan.md` and `docs/measurements.md`, and leave a one-line pointer. Never edit the shared `~/fpga-coord/PROTOCOL.md`; propose trims to the user instead.

## 1. Dependencies and conduct (detail: `docs/buildplan.md` §1)

- GPU access is a **hard manual gate** for Phase 4; until then improve search/heuristics on CPU. **GPU-time budget is decided with the user before Phase 4.**
- Tribes has no licence → facts-only reimplementation, Tribes used as a local oracle/opponent only (ADR-002).
- No Midjiwan assets, art, paid-content names, decompiled code or extracted game data **in the public repo** (keep local, gitignored). **Never connect a bot to the real game's online multiplayer**; human opponents only with informed consent, in private games (ADR-003).

## 2. Hardware and compute

| Phase | Compute |
|---|---|
| 1–3 | CPU (any machine; WSL is fine) |
| 4 | GPU for training and batched inference |
| 5 | CPU for tournaments; GPU for network agents |

Record hardware with every throughput number.

## 3. Toolchain and build process

| Tool | Use |
|---|---|
| C++20, CMake, Ninja | Simulator, MCTS, self-play actors |
| GoogleTest, clang-tidy, clang-format, ASan/UBSan | Tests and static analysis |
| Java 8+ | Running Tribes as the oracle and opponent |
| Python 3.11+, PyTorch, NumPy, pytest | Training, analysis, plots |
| Network inference from C++ | LibTorch or ONNX Runtime (ADR) |

```
make setup            # Python venv, C++ deps, Tribes checkout (pinned commit)
make build test       # CMake Release + Debug/ASan; GoogleTest
make difftest N=<games>     # differential test vs. Tribes' forward model
make bench                  # simulator states/s, search nodes/s
make tournament A=<agent> B=<agent> N=<games>   # → data/results/ with confidence intervals
make selfplay train         # Phase 4
make plots ci clean
```

CI on every push: build, unit tests, a short differential test, clang-tidy, sanitizers, ruff.

## 4. Repository layout

```
├── CLAUDE.md  README.md  progress.md
├── sim/         # C++: state, rules, observation (fog), action generation, forward model
├── search/      # C++: MCTS (PUCT), macro-actions, determinization, RHEA/MCTS baselines
├── selfplay/    # C++ actors, game records, replay buffer writer
├── nn/          # Python: model, training loop, export for C++ inference
├── oracle/      # Tribes bridge: run Tribes games, export action logs/states for difftests
├── tools/       # replay viewer, tournament runner
├── tests/  data/  docs/  Makefile  .github/workflows/
```

## 5. Coding standards

### C++
- C++20, `-Wall -Wextra -Werror`, clang-format, clang-tidy clean.
- **Deterministic:** every random draw goes through one seeded RNG passed explicitly. Same seed → same game.
- Hot paths (rules, action generation, MCTS) avoid heap allocation per node/step; use pools.
- No global mutable state. The game state is a value type that can be copied cheaply for search.
- ASan/UBSan in Debug.

### Python
3.11+, type hints, ruff, pytest, seeded.

### Required ADRs
Language (C++ vs. Rust) · rules source (port vs. reimplementation, given the licence) · state encoding · action encoding and factored policy head · macro-action set · hidden-information handling (determinization vs. information sets) · MCTS variant (PUCT vs. Gumbel) · inference backend · curriculum · evaluation protocol.

## 6–9. Simulator, search, learning, testing/evaluation → `docs/buildplan.md`

## 10. Measurement discipline
Every result records: agent versions (git hash, network checkpoint), map seeds, mode, map size, number of players, game count, CI (95% Wilson), hardware. Engine changes are decided by SPRT.

## 11–13. Phase checklists, out of scope, portfolio deliverables → `docs/buildplan.md`

## 14. Known failure modes

- **Rules divergence from Tribes:** every later result is suspect. Difftest first
- **Hidden-information leakage** in search or training: the agent looks stronger than it is
- **Illegal or invalid actions** (RHEA's problem in the paper): property-test action generation
- **Overfitting to one opponent or map size:** evaluate across seeds and opponents
- **Too few games:** always report CIs; use SPRT for changes
- **Non-determinism:** unseeded randomness breaks reproducibility and difftests
- **Simulator too slow:** self-play stalls; profile before scaling up
- **Starting Phase 4 without a compute budget**

## 15. Working principles

- **The simulator is the golden model; difftest it first.**
- **No peeking through the fog.**
- **Win rates with confidence intervals, always.**
- **Respect the game's creators and other players.**
- **Evidence is part of done.**
