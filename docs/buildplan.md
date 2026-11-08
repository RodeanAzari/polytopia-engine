# Build plan — Project 8: Polytopia Engine

The full project spec, moved out of `CLAUDE.md` to keep the always-loaded context small.
**Not imported**: read the section you need. Section numbers match the references in `CLAUDE.md`
(§1–5, §10, §14, §15 live in `CLAUDE.md`; this file holds the detail for §1 and all of §6–9, §11–13).

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
| **polyterra-env** (Feb 2026) | RL environment on decompiled game logic | Reference only; it states the logic remains Midjiwan's property, so not vendored into our repo |

---

## 1. Dependencies

| Needs | Gate | Until then |
|---|---|---|
| GPU access (local NVIDIA GPU via WSL2 CUDA, university compute, or rented cloud time) | **Hard** for Phase 4 (manual gate in fpga-coord) | Improve search and heuristics on CPU |

No FPGA, no shared blocks. **The budget for GPU time is decided with the user before Phase 4.**

### Licensing and conduct (check before Phase 1)
- **Check the Tribes repository's licence.** If it has none, its rules can't be assumed free to port and publish. Then treat Tribes as a **local test oracle and opponent only**, implement the rules from the paper and game knowledge, and ask the authors about porting.
- This is a non-commercial research clone: no Midjiwan assets, art, names of paid content, decompiled code or extracted game data in the public repo (ADR-003). Credit *The Battle of Polytopia* and Tribes in the README.
- **Never connect a bot to the real game's online multiplayer.** It's unfair to other players.

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
- **Real game (Phase 6):** games vs. the hardest built-in AI and a consenting friend; reported
  separately from simulator results (ADR-003).

---
## 11. Phases and definitions of done

Every phase's DoD includes **README updated, log entry, media captured, user confirming captures**.

### Phase 1 — Simulator
- [x] Licence check done; rules-source ADR written (ADR-002)
- [x] Port passes the differential test on ≥ 1,000 seeded Tribes games (or every difference explained)
- [x] Benchmark vs. Tribes' Java forward model
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

### Phase 6 — Real game *(after Phase 5; [ADR-003](adr/003-real-game-phase.md))*
- [ ] Real-rules mode: each Tribes quirk is a switch (default Tribes); real values from any source,
      each recorded (docs, observed scenarios → scenario tests, game inspection); game version recorded
- [ ] Tools: board-state editor or reader + advisor front end or move executor
- [ ] Real-game interface (hand relay or automated; ADR per method), filtered to the agent's observation and tested.
- [ ] Matches: vs. the hardest built-in AI, and vs. a
      consenting human (private game). Never public/ranked multiplayer
- [ ] Media: real-game results table (labelled, Wilson CIs, small N stated), game clips, relay-error log if hand-relayed

**Fallback that's still impressive:** Phases 1–3: a fast, verified open simulator plus a search agent that beats every published Tribes agent.

---

## 12. Out of scope — do not build

- Bots in public, ranked or matchmade online play; games vs. humans who don't know they face an AI (ADR-003)
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
