# ADR-009: Hidden information: determinization of tribe-level state

- **Status:** Accepted (2026-11-10, user-approved: what to sample first; K and the estimators are tuned by SPRT)
- **Date:** 2026-11-10

## Context
Agents only ever see their observation (CLAUDE.md: no peeking through the fog). The observation (rule facts
04 §7, `sim/include/poly/observation.hpp`) hides from other tribes, **in both observability modes**: stars,
kills, techs, monuments and lists; their cities' population, production bonus and unit list (level and
position stay visible); their units' home cities and kills. Under fog it also hides unseen tiles and
everything on them. Searched as-is, an opponent has 0 stars and no techs: it can't build, train or research,
so the search underestimates every threat. Tribes' MC patches only techs (⌊tick/2⌋ random researches,
agents sheet 03); its MCTS and RHEA patch nothing. docs/buildplan.md §7.3: start with determinization and
record strategy fusion.

## Decision
1. **Determinize tribe-level hidden state only.** Each determinization is a GameState built from the
   Observation alone (the sampler takes `const Observation&`, never a GameState, so the type system blocks
   a leak; ADR-005 §2) plus the agent's RNG:
   - **Techs:** the techs implied by visible evidence (unit types, buildings and improvements on their
     territory, with prerequisites), then random researches (as `research_random`) up to an estimated count;
     the first estimator is Tribes' ⌊tick/2⌋.
   - **Cities:** population and production consistent with each visible city's level and buildings (rules as
     in the simulator).
   - **Stars:** an estimate from their visible income (sum of their cities' production) times a sampled
     number of turns of saving, capped by what the rules allow.
   Fogged tiles stay fog, exactly as in the agent's copy; terrain and units under fog are not sampled.
2. **Aggregation:** K determinizations per decision, one PUCT tree each (ADR-008) with 2,000 / K
   forward-model calls; the move is the root action with the most visits summed over trees (the root legal set
   is the environment's, identical in every tree). Start K = 4.
3. **Leak tests:** property test that the determinized state equals the observation on every field the
   observation shows, and that its output is identical for two true states with the same observation (a
   leak would make it differ).

## Rejected alternatives
- **No determinization** (as Tribes' MCTS/RHEA): opponents modelled as broke and techless; known-wrong.
- **Also sampling fogged terrain, resources and units:** more faithful under Fog, but much more work and a larger
  leak-test surface; a later step if Fog results call for it.
- **Information-set MCTS (SO-ISMCTS, one tree, re-determinized per iteration):** avoids strategy fusion but needs
  availability counts for opponent actions that vary by determinization; revisit if fusion shows up.
- **Searching the true state:** a fog leak; invalidates every result.

## Known weakness
Strategy fusion: each tree assumes its determinization is the truth, so the aggregate can favour moves that
are only good when the hidden state is known, and never values information-gathering. Recorded per
docs/buildplan.md §7.3; ISMCTS is the remedy if it matters.

## Consequences
- `search/determinize.{hpp,cpp}` with its own tests (evidence → techs, city stats from level, star estimate
  bounds, the two leak properties); PUCT gains K trees.
- The flat PUCT agent comes first without determinization (K = 1, the observation itself) so the two are
  compared by SPRT rather than assumed.
