# ADR-003: Real-game play as a final phase — real-rules mode and a real-game interface

- **Status:** Accepted (revised 2026-11-05: real-game interface methods opened up)
- **Date:** 2026-11-05

## Context
The engine is built against Tribes (ADR-002) because Tribes is a runnable oracle: every rule can be
differentially tested, automatically, over millions of steps.

But Tribes differs from the commercial game (every **[Tribes quirk]** in `docs/rules/`), and lacks newer content.
An agent that is strong at Tribes is only approximately strong at the real game. The owner wants a final test
that matters to people outside AI research: beating the real game's hardest built-in AI, and a real human
(a friend) in the real game. This is a non-commercial hobby/learning project.

## Decision
1. **Order of work is unchanged.** Phases 1–5 are built and evaluated on Tribes rules. Phase 6 starts only after
   Phase 5's results exist.
2. **Real-rules mode (Phase 6).** Each Tribes quirk becomes a rules switch. Default = Tribes behaviour (all difftests
   and tournaments keep running in Tribes mode). Real-rules values may come from any source the owner chooses:
   community documentation, owner-observed scenarios, or inspection of the owner's own copy of the game
   (including decompilation or memory inspection). Each value records its source. Real-game features Tribes lacks
   are added only if a real match needs them. The game version is recorded with every real-game result.
3. **Real-game interface.** Moves may be relayed by hand, or the interface may be automated (screen reading,
   memory reading, input automation, mods), chosen per need in Phase 6 with its own ADR. Whatever the method:
   - **the agent receives only its observation** (what the player could see). A memory reader can see through
     the fog; the interface must filter to the visible state, and this is tested. Otherwise results are invalid
     ("No peeking through the fog").
4. **Opponents.**
   - The game's hardest built-in AI difficulty, single-player.
   - Human opponents **only with informed consent**: they know they are playing an AI. Private games only
     (e.g. the owner's friend). **Never** public, ranked or matchmade multiplayer.
5. **Reporting.** Real-game results are reported separately from simulator results, labelled
   (opponent, difficulty, game version, map size, tribes, mode, interface method), with game counts and Wilson
   95% intervals. Expected sample size is small (~10–20 games) unless the interface is automated.

## Consequences
- Phase 6 needs tools: a board-state editor or reader, and an advisor front end or automated move executor.
- Hand relay is slow (≥ 1 hour per game) and error-prone; relay mistakes are logged per game. Automation allows
  many more games vs. the built-in AI, and so real confidence intervals.
- Distribution shift: the network is trained on Tribes rules. Phase 6 may need fine-tuning in real-rules mode;
  that GPU time is part of the Phase 4 budget discussion.
- **Publishing:** this repo is a public portfolio. Midjiwan's terms of use could not be located when this was
  written (2026-11-05), and code or data extracted from the game is Midjiwan's copyright whatever the purpose.
  Keep decompiled code, extracted game data and assets out of the public repo (local, gitignored), as with
  Tribes; rule facts written in our own words are fine. Check the game's terms before Phase 6 starts.

## Rejected alternatives
- **Build for the real game from the start.** No automated oracle as convenient as Tribes → every later result
  would rest on rules verified more weakly. Rejected for Phases 1–5.
- **Bot in public online multiplayer.** Cheating against people who didn't agree to it. Rejected permanently.
- **Hand relay only** (first version of this ADR). Too restrictive and too slow for meaningful game counts;
  replaced by decision 3.
