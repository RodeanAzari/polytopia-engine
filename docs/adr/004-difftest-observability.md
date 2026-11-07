# ADR-004: Difftest observability — test both full and partial observability

- **Status:** Accepted
- **Date:** 2026-11-05

## Context
At the pinned Tribes commit, the compile-time constant `Constants.PLAY_WITH_FULL_OBS` is `true`: every tribe's
vision grid starts all-visible. The stock oracle therefore has no fog, no reveal score, no tech gained on
meeting a tribe and no Eye of God unlock (rule facts 03b §0). Our agent must play under fog (CLAUDE.md:
"no peeking through the fog"; ADR-003 real-game play is fog-filtered), so the fog rules need an oracle check too.

## Decision
1. The simulator implements both modes, selected per game (a setup parameter, not a compile-time switch).
2. Differential tests run in **both** modes:
   - **full obs** against stock Tribes as pinned;
   - **partial obs** against a second local Tribes build in which only that one constant is set to `false`.
     The flip is applied by our `oracle/` build scripts to a gitignored build copy; nothing of Tribes is
     committed. This amends ADR-002 §2 ("our only additions are a local bridge harness") by exactly this one
     constant.
3. Search and training results are produced in partial-obs mode only.

## Rejected alternatives
- **Partial obs only:** less work, but the stock configuration (the one the Tribes paper's agents ran in)
  would go unchecked, and full-obs-only rule paths would be untested.
- **Full obs only, fog implemented without an oracle:** fog bugs (reveal score, meeting tech RNG, Eye of God,
  reveal-triggered network updates) would go unnoticed, and they affect every result we care about.

## Risks
The partial-obs path in Tribes is less exercised than the default. A divergence there may be a Tribes bug;
we still match Tribes and record the quirk in the rule-facts sheet.

## Outcome (2026-11-07)
Implemented as decided: `make setup` builds `build/tribes-po` from a copy of the pinned sources with only
`PLAY_WITH_FULL_OBS` flipped (the copy step fails if that line is not found exactly once); `OBS=partial`
selects it for `difftest-init`, `difftest` and `difftest-mindbender`. Results: map load 700/700, whole games
140/140 (800 meeting tech draws), Mind Bender scenario 100/100. The partial-obs path found one Tribes quirk
the full-obs build cannot show (one-directional port links, 03a §6) and one in meeting techs (03b §1).
