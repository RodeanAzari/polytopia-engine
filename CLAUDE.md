Project ID: P8
@~/fpga-coord/PROTOCOL.md

# CLAUDE.md — Project 8: Polytopia Engine

## What this is

An AI that plays *The Battle of Polytopia* (Midjiwan's turn-based 4X strategy game) well enough to **beat a competent human**, a bar the published research agents haven't cleared. A fast simulator, action-level Monte Carlo tree search, and a policy/value network trained by self-play.

```
FAST SIMULATOR (C++ port of the Tribes rules, bit-exact vs. Tribes' Java forward model)
      │ state encoding: tile planes (terrain, units, cities, fog) + global features (stars, techs, turn)
      ▼
POLICY/VALUE NETWORK ── priors over the next action, value of the position
      ▼
ACTION-LEVEL MCTS ── one action at a time within a turn; "end turn" is an action; macro-actions prune
      ▼
SELF-PLAY ─▶ replay buffer ─▶ training ─▶ new network ─▶ curriculum: small maps → full games
```

**This is a portfolio project and the plan's only pure-software one.** Working code is half the deliverable; the other half is evidence: measured win rates with confidence intervals, a replay viewer, training curves and a full README. §13 is mandatory. **A phase without its numbers and media is not done.**

**Languages:** **C++20** for the simulator and search (default; record the choice in an ADR. Rust is the alternative if you want a direct comparison with Polyfish). Python + PyTorch for training.

### Why it's hard (Tribes paper, AIIDE 2020)
- A turn is a **sequence** of actions: ~54 options per action on average, but a **branching factor per turn of ~10¹⁵**, growing as the game goes on.
- Multiplayer, **partially observable** (fog of war), **stochastic** (ruins, map generation), long-horizon economy.
- **Their best agent (RHEA) lost every game to a moderately skilled human.** MCTS did better *without* random rollouts.

### Prior art
| Project | What it is | Use it for |
|---|---|---|
| **Tribes** (QMUL, Java) | Open-source Polytopia reimplementation for AI research: forward model, Random/Rule-based/OSLA/MC/MCTS/RHEA agents, tournament runner, ~150 parameters | Rules reference, test oracle, baseline opponents |
| **Polyfish** (Rust) | AlphaZero-style MCTS + network, Gumbel self-play; README says training "needs monster compute" | Comparable engine; opponent if it runs |
| **polyterra-env** (Feb 2026) | RL environment on decompiled game logic | **Don't use**: it states the logic remains Midjiwan's property |

---

## Current status

<!-- Claude: update as phases complete. Never mark a phase done until its §11 checklist
     passes in full, INCLUDING the portfolio items. -->

| Phase | Status | Key result |
|---|---|---|
| 1 — Simulator | IN PROGRESS | Licence checked → facts-only (ADR-002); scaffold + RNG |
| 2 — Search baselines | NOT STARTED | — |
| 3 — Action-level MCTS + heuristic priors | NOT STARTED | — |
| 4 — Learned policy/value network | NOT STARTED | — |
| 5 — Evaluation | NOT STARTED | — |

---

## Rules for Claude

- **Follow the imported fpga-coord protocol** at the start and end of every session (this project has one gate: GPU access).
- **Never invent a number.** Win rates always come with game counts and confidence intervals; unmeasured = `[TBD]`.
- **The simulator is the golden model.** No search or training work on rules that haven't passed the differential test against Tribes.
- **No peeking through the fog.** The agent only ever sees its observation; any hidden-information leak in search or training invalidates results.
- **Record decisions** as ADRs, including rejected alternatives.
- **Portfolio moments:** simulator bit-exact, first win over RHEA, first learned net beating search-only, first human win: generate plots, list captures, add to `docs/portfolio-todo.md`, wait for confirmation.
- **Lab notebook** every session; **progress.md** at start.
- **Stay in scope (§12).**

---

## 1. Dependencies

| Needs | Gate | Until then |
|---|---|---|
| GPU access (local NVIDIA GPU via WSL2 CUDA, university compute, or rented cloud time) | **Hard** for Phase 4 (manual gate in fpga-coord) | Improve search and heuristics on CPU |

No FPGA, no shared blocks. **The budget for GPU time is decided with the user before Phase 4.**

### Licensing and conduct (check before Phase 1)
- **Check the Tribes repository's licence.** If it has none, its rules can't be assumed free to port and publish. Then treat Tribes as a **local test oracle and opponent only**, implement the rules from the paper and game knowledge, and ask the authors about porting.
- This is a non-commercial research clone: no Midjiwan assets, art, names of paid content, or decompiled code. Credit *The Battle of Polytopia* and Tribes in the README.
- **Never connect a bot to the real game's online multiplayer.** It's unfair to other players and likely against Midjiwan's terms.

---

## 2. Hardware and compute

| Phase | Compute |
|---|---|
| 1–3 | CPU (any machine; WSL is fine) |
| 4 | GPU for training and batched inference |
| 5 | CPU for tournaments; GPU for network agents |

Record hardware with every throughput number.

---

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

---

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

---

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

---

## 6. Simulator

- Implements the game the Tribes framework implements: tribes, terrain, cities and levels, stars, the tech tree, units and combat, ruins, fog of war, game modes (Capitals and Score).
- **Observation function:** what a player can see given fog. Search and training only ever call this, never the full state, except where determinization explicitly samples hidden information (§7.3).
- **Differential test harness (`oracle/`):** run Tribes games with seeds, export the action sequence and per-step state, replay in the port, compare state after every action. **Bit-exact, or every difference explained in an ADR.**
- **Speed:** report states/s and full random-game playouts/s vs. Tribes' Java forward model.

---

## 7. Search

### 7.1 Action-level MCTS
- One tree node per decision (single action), not per turn. "End turn" is an action.
- **PUCT** with priors (heuristic in Phase 3, learned in Phase 4); the Gumbel variant as an alternative for low simulation counts (ADR).
- Evaluation at leaves: a heuristic value in Phase 3, the network in Phase 4. Don't rely on random rollouts; Tribes found they hurt.

### 7.2 Macro-actions
Collapse common sequences to cut the branching: e.g. "this unit attacks its best target", "move toward the nearest enemy city", "build the best available building". Compare flat vs. macro.

### 7.3 Hidden information
Start with **determinization**: sample plausible hidden states consistent with the observation, search each, aggregate. Record the known weakness (strategy fusion) in the ADR.

### 7.4 Baselines
Reimplement MCTS and RHEA as in the paper, and **reproduce the paper's win rates** in Tribes' tournament setup before claiming improvements.

---

## 8. Learning (Phase 4)

- **Network:** convolutional or attention trunk over tile planes + global features; **factored policy head** (unit × action type × target) and a value head.
- **Self-play:** C++ actors generate games with MCTS + current network; Python learner trains; periodic promotion when a new network beats the old one.
- **Curriculum:** tiny maps, two players, Score mode → larger maps → full Capitals games.
- **Compute plan** written before starting; throughput (games/hour) measured.

---

## 9. Testing and evaluation

### 9.1 Tests
- Unit tests per rule (combat, movement, city growth, tech prerequisites).
- **Differential tests** vs. Tribes on thousands of seeded games.
- Property tests: action generation only returns legal actions; observation never leaks hidden tiles.

### 9.2 Evaluation protocol
- Round-robin tournaments vs. Tribes' agents (Random, Rule-based, OSLA, MC, MCTS, RHEA) on fixed map seeds, swapping seats.
- **Win rates with 95% confidence intervals** (Wilson), game counts stated.
- For engine changes: **SPRT** (sequential probability ratio test), as chess-engine developers do, to decide whether a change helps.
- vs. Polyfish if it can be run locally.
- **Humans:** offline games in your own simulator, opponent skill described, results labeled as such.

---

## 10. Measurement discipline
Every result records: agent versions (git hash, network checkpoint), map seeds, mode, map size, number of players, game count, CI, hardware.

---

## 11. Phases and definitions of done

Every phase's DoD includes **README updated, log entry, media captured, user confirming captures**.

### Phase 1 — Simulator
- [ ] Licence check done; rules-source ADR written
- [ ] Port passes the differential test on ≥ 1,000 seeded Tribes games (or every difference explained)
- [ ] Benchmark vs. Tribes' Java forward model
- [ ] Media: simulator speed chart; replay viewer screenshot

### Phase 2 — Search baselines
- [ ] MCTS and RHEA reimplemented; the paper's win-rate pattern reproduced
- [ ] Media: baseline round-robin table

### Phase 3 — Action-level MCTS + heuristic priors
- [ ] Beats every Tribes agent, including RHEA, with significance
- [ ] Macro vs. flat comparison
- [ ] Media: win-rate table with CIs; **replay GIF of a win over RHEA** with visit-count overlay

### Phase 4 — Learned policy/value network *(gate: GPU)*
- [ ] Self-play pipeline; curriculum; network-guided search beats Phase 3's search-only agent (SPRT)
- [ ] Media: win rate vs. training time; training throughput

### Phase 5 — Evaluation
- [ ] Final tournaments; Polyfish if possible; **games vs. human players**, labeled
- [ ] README complete; demo video
- [ ] Media: final results table; demo video

**Fallback that's still impressive:** Phases 1–3: a fast, verified open simulator plus a search agent that beats every published Tribes agent.

---

## 12. Out of scope — do not build

- Anything touching the real game: online play, game-memory reading, mods, decompiled logic
- Rules beyond what Tribes implements (paid tribes, newer content) unless separately specified
- Training at a scale that needs a cluster
- An FPGA version

---

## 13. Portfolio deliverables — mandatory

### 13.1 Captures
| Capture | Type | Phase |
|---|---|---|
| **Hero GIF:** replay of the agent beating RHEA, with the search's visit counts overlaid on the map | Screen recording | 3 |
| **Demo video (60–120 s):** a full game sped up, search visualization, results table, human-game clip | OBS | 5 |
| Simulator speed vs. Tribes | Plot | 1 |
| Win-rate table vs. every Tribes agent (CIs) | Table | 3 |
| Win rate vs. training time | Plot | 4 |
| Macro-action vs. flat search comparison | Plot | 3 |

### 13.2 Media rules
Dated names in `docs/media/`; plots via `make plots`; sidecar `.md` per result (hashes, seeds, counts, hardware, interpretation); GIFs ≤ 8 MB; videos linked.

### 13.3 Lab notebook
Every session; bugs as **symptom → root cause → fix** (rules divergences especially).

### 13.4 README standard
RISC-V README as the template for structure, depth and tone; every number linked.

**At a glance rows:** Game and rules source · Simulator speed vs. Tribes · Differential test result · Search method · Win rate vs. RHEA (CI, games) · Win rate vs. all Tribes agents · Network and training compute · Human results · Tests · CI.

**Sections:** claim + intro + hero GIF · at a glance · status · architecture (Mermaid) · testing (differential, unit, property) · deep sections (simulator, action-level MCTS + macros, hidden information, learning) · demos · results (every row linked) · **limitations** (#1: rules are the Tribes subset, not the full current game) · prior work · credits and licence notes · build and reproduce · layout · roadmap.

---

## 14. Known failure modes

- **Rules divergence from Tribes:** every later result is suspect. Difftest first
- **Hidden-information leakage** in search or training: the agent looks stronger than it is
- **Illegal or invalid actions** (RHEA's problem in the paper): property-test action generation
- **Overfitting to one opponent or map size:** evaluate across seeds and opponents
- **Too few games:** always report CIs; use SPRT for changes
- **Non-determinism:** unseeded randomness breaks reproducibility and difftests
- **Simulator too slow:** self-play stalls; profile before scaling up
- **Starting Phase 4 without a compute budget**

---

## 15. Working principles

- **The simulator is the golden model; difftest it first.**
- **No peeking through the fog.**
- **Win rates with confidence intervals, always.**
- **Respect the game's creators and other players.**
- **Evidence is part of done.**
