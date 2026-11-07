# ADR-002: Rules source — facts-only reimplementation; Tribes as a black-box oracle

- **Status:** Accepted
- **Date:** 2026-11-05

## Context
The simulator must match the rules implemented by Tribes (GAIGResearch/Tribes, Java), so that results
are comparable with the AIIDE 2020 paper and Tribes can act as both test oracle and opponent.

**Licence check (2026-11-05):** the GitHub API reports `license: null` for GAIGResearch/Tribes, and the
README contains no licence, copyright or permission text. With no licence, all rights are reserved:
we may run Tribes locally, but we can't copy or translate its code into a published repository.
The last push was 2022-08-25.

Game rules as such (unit stats, costs, formulas, tech prerequisites) are facts and are not protected
by copyright. Their expression in code is.

## Decision
1. **Facts-only reimplementation.** The C++ simulator is written from scratch. Sources for rules:
   the Tribes paper (Perez-Liebana et al., *Tribes: A New Turn-Based Strategy Game for AI Research*,
   AIIDE 2020), general game knowledge, and the Tribes source **read only to extract rule facts**
   (numbers, formulas, ordering of effects). No Java code, class structure or comments are translated
   or copied. Our architecture is our own: a copyable value-type state, explicit seeded RNG, our own
   action generation.
2. **Tribes is a local oracle and opponent only.** It is cloned at a pinned commit
   (`270e7e564630e5ef3dcc04c96dd66f58ef7273f3`, master HEAD 2022-08-25) into a gitignored directory by
   `make setup`. It is never vendored, committed or redistributed. Our only additions to it are a local
   logging/bridge harness under `oracle/`, written by us.
3. **Stochastic outcomes are injected, not reproduced.** We do not imitate Java's RNG call order.
   For differential tests, Tribes logs every random outcome (map generation, ruin rewards, combat
   randomness if any); our simulator replays them through an outcome-injection interface. Deterministic
   rules must match bit-exactly; every remaining difference gets its own ADR.
4. **Ask the authors.** A permission request is drafted (`docs/tribes-permission-request.md`) for
   the user to send. If they grant a licence, this ADR is revisited; the architecture stays the same.

## Alternatives considered
- **Strict clean-room (never read the source).** Rejected: the exact rounding, thresholds and tie-break
  ordering needed for a bit-exact difftest are impractical to recover from black-box runs. The headline
  "verified simulator" claim would come with dozens of ADR'd differences.
- **Direct port, kept private until permission.** Rejected: fastest, but the portfolio couldn't be
  published unless the authors (inactive since 2022) reply. A single point of failure.

## Consequences
- The difftest compares state after each action, with random outcomes taken from the Tribes log.
  Seeded map generation in our sim is independent of Tribes' (same distributions, not the same maps).
- The README credits The Battle of Polytopia (Midjiwan) and Tribes (GAIG, QMUL), and states this
  licensing position.

## Amendment 2026-11-06: Tribes in CI
Approved by the user: CI may clone Tribes at the pinned commit (via `make setup`) on the CI runner and run a short
replay difftest there. This counts as running the oracle, like a local run; Tribes is still never vendored,
committed or redistributed, and logs stay build artifacts (not committed).
