# ADR-012: Self-play, inference backend and training loop

- **Status:** Proposed (2026-11-13)
- **Date:** 2026-11-13

## Context
ADR-011 defines the network. This ADR covers how games are generated with it, how C++ search calls it, how
training consumes the games, and when a new network replaces the old one. Budget: `docs/compute-plan.md`.

Measured facts (notebook 2026-11-13):
- **Phase 3 search, CPU:**
  - 794 decisions per 2-player 11 × 11 game; ~51 µs per expansion on one thread, Rule Based scoring and Diff
    included.
  - ~400 games/h at 8 jobs.
- **GPU, A2000 Laptop 4 GB, fp16:**
  - a 64 × 6 ResNet does ~80k positions/s at 11 × 11, batch 256–1024;
  - small batches waste most of it: one position at a time is bound by launch latency, not arithmetic.
- **At 2,000 evaluations per decision:**
  - a game is ~1.6M evaluations, ~20 s of GPU time;
  - so self-play would be GPU-bound at ≤ ~180 games/h.
- **Evaluation (ADR-007 §4):** equal forward-model calls (2,000 per decision) against Phase 3.
- **Toolchain:** LibTorch 2.14.0+cu130 exists (~2 GB, same version as the trainer's PyTorch). No CUDA toolkit
  (nvcc) is installed; the driver is.

## Decision

### 1. Inference: LibTorch in-process, batched across many games
- **Backend:** LibTorch 2.14.0+cu130, pinned to the trainer's PyTorch version, linked into the self-play actor
  and the tournament runner behind one interface:

  ```
  Evaluator::evaluate(span<const float> planes, int n, int side) -> (logits, values)
  ```

  It has a CPU fallback for tests and for CI, which has no GPU.
- **Model format:** exported from PyTorch. The two candidates are `torch.export` + AOTInductor and TorchScript
  `trace`. The first prototype step picks one by parity (C++ vs. PyTorch outputs within fp16 tolerance) and by
  measured throughput, and records the result here.
- **Batching:**
  - Each actor thread runs **many games at once** (start 64). A search pauses at its leaf and waits for the
    batch; this is a resumable search state, not one OS thread per game.
  - One batcher collects the waiting leaves of all actor threads (≥ 256 per batch), runs the network once and
    resumes every search.
  - No virtual loss inside a tree to start with: one leaf per game per round keeps each tree's search exactly
    the sequential PUCT of ADR-008.
- Everything goes through the `Observation` → planes path of ADR-011 §1. The evaluator never sees a `GameState`.

### 2. Self-play settings (start; each change by measurement)
- **Settings:** 2-player, 11 × 11, Capitals, Tribes' level generator with *training* seeds, disjoint from dev and
  test (ADR-007). Paper and Fog alternate by game. The tribe pair is as evaluated (Xin Xi vs. Imperius) at first.
- **Search:** the Phase 3 PUCT with the network's priors and value (ADR-011 §2): K = 4, macro edges, c = 1.25.
- **Expansions per self-play decision: 400 to start**, versus 2,000 in evaluation, so self-play isn't GPU-bound
  at ~180 games/h. The pilot measures games/h at 400 and, if time allows, at 800.
- **Exploration:** Dirichlet noise at the root (AlphaZero form). Moves are sampled from visits for the first
  turns of the game, then the most-visited move is played. The parameters are recorded in the code and here once
  set.
- **Records:** one file per batch of games:
  - per decision: planes (fp16), legal policy indices, visit counts, the side to move;
  - per game: the result, level seed, game seed, network id and planes version.
  - Written by C++, read by Python (ADR-011 §5: Python never re-derives planes).

### 3. Training loop
- **Learner:** a Python process that trains on a sliding window of the newest records and writes a checkpoint
  every N steps.
- **Bootstrap:** the window starts with the Phase 3 records (ADR-011 §4).
- **Promotion (buildplan §8):** a new checkpoint plays the current best on dev levels. It is promoted by SPRT
  (ADR-007: pentanomial, H0 0 / H1 20 Elo), or by a fixed-length match if the SPRT runs too long for the cadence;
  the protocol is recorded with every promotion. Actors pick up the best network between batches.
- **Phase 4 done (buildplan §11):** the network-guided agent beats Phase 3's search-only agent by SPRT on dev
  levels. Then a headline on test levels, as in Phase 3 (both conditions, Wilson CIs), with a win rate vs.
  training time chart (§13).

### 4. Workers on other machines (later)
Records are files and networks are files, so a worker on another machine (the user's desktop, compute plan) only
needs both copied each way. No change to this design, only a sync job. Not built before the pilot says it's
worth it.

## Alternatives considered
- **ONNX Runtime (CUDA or TensorRT execution provider).** Smaller runtime and possibly faster through TensorRT,
  but its CUDA 13 support would need checking, a second numerics path to keep at parity with PyTorch, and
  TensorRT engines need building per GPU. Revisit if LibTorch's measured throughput falls short.
- **Python-driven search** (the engine as a pybind11 module; Python batches the leaves and calls PyTorch).
  - For: no LibTorch, no export step.
  - Against: the C++ tournament runner and Phase 5 would need a second, Python-side agent harness.
  - A fallback if LibTorch linking proves costly.
- **A separate inference server** (C++ actors → shared memory → Python process). Rejected: an IPC protocol to
  keep correct, for no measured gain over in-process.
- **Virtual loss / parallel leaves per tree.** Changes the search compared with Phase 3. Parallelism across games
  already fills the batch.
- **The same 2,000 expansions in self-play as in evaluation.** GPU-bound at ≤ ~180 games/h (measured
  arithmetic). Fewer per decision buys more games, AlphaZero-style; decided by the pilot's numbers.
- **Training directly by self-play from a random network.** Rejected: hours of GPU on near-random games, when
  Phase 3's agent can produce strong supervised targets on the CPU.

## Consequences
- A large pinned dependency (LibTorch, ~2 GB), downloaded by `make setup-nn` and never committed. CI builds
  without it (the CPU-fallback evaluator is a stub there) and runs the C++ tests that don't need it.
- The PUCT search becomes resumable (state kept between leaf and evaluation). Phase 3 behaviour must stay
  bit-identical with the Rule Based / Diff evaluator: a regression test plays recorded games and compares every
  chosen action.
- Throughput numbers (games/h, positions/s, GPU utilisation) are recorded with hardware, per the compute plan.
