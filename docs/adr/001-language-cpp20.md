# ADR-001: C++20 for the simulator and search

- **Status:** Accepted
- **Date:** 2026-11-05

## Context
The simulator and MCTS are the hot path: self-play throughput (Phase 4) is bounded by states/s and
nodes/s. Python is used for training only. The candidates for the engine were C++20 and Rust.

## Decision
C++20 (GCC 15 / Clang 21), CMake, GoogleTest. Network inference from C++ is decided separately
(LibTorch vs. ONNX Runtime ADR, Phase 4).

## Alternatives considered
- **Rust.** Rejected. It would allow a direct like-for-like comparison with Polyfish, and its memory
  safety is attractive. But LibTorch / ONNX Runtime integration, sanitizer tooling and profiling are
  more mature in C++, and the plan's toolchain (clang-tidy, ASan/UBSan, GoogleTest) is C++-native.
- **Java (extend Tribes).** Rejected. Tribes has no licence (see ADR-002), and a JVM forward model is
  the speed baseline we are trying to beat.

## Consequences
- Memory safety comes from ASan/UBSan in Debug builds and clang-tidy, not the language.
- A Polyfish comparison will be agent-vs-agent only, not engine-vs-engine at the code level.
