# ADR-003: Real-game play as a final phase — real-rules mode, hand-relayed matches

- **Status:** Accepted
- **Date:** 2026-11-05

## Context
The engine is built against Tribes (ADR-002) because Tribes is a runnable oracle: every rule can be
differentially tested, automatically, over millions of steps. The commercial game cannot serve as an oracle
without decompiling it or reading its memory, which breaks Midjiwan's terms and this project's conduct rules.

But Tribes differs from the commercial game (every **[Tribes quirk]** in `docs/rules/`), and lacks newer content.
An agent that is strong at Tribes is only approximately strong at the real game. The owner wants a final test
that matters to people outside AI research: beating the real game's hardest built-in AI, and a real human
(a friend) in the real game.

## Decision
1. **Order of work is unchanged.** Phases 1–5 are built and evaluated on Tribes rules. Phase 6 starts only after
   Phase 5's results exist.
2. **Real-rules mode (Phase 6).** Each Tribes quirk becomes a rules switch. Default = Tribes behaviour (all difftests
   and tournaments keep running in Tribes mode). Real-rules values come only from **public facts**: community
   documentation and situations the owner sets up and observes in their own copy of the game (screenshots as
   evidence, encoded as scenario unit tests). Still no decompiled code, memory reading, mods or assets.
   Real-game features Tribes lacks are added only if a real match needs them, one at a time, each with its source.
   The game version played is recorded with every real-game result.
3. **Matches are hand-relayed.** The owner reads the board from the screen, enters it into our tools, and plays
   the engine's chosen moves by hand. No screen scraping, input automation or any program touching the game
   process. The engine only gets what the owner can see (fog is respected exactly as in simulation).
4. **Opponents.**
   - The game's hardest built-in AI difficulty, single-player, offline.
   - Human opponents **only with informed consent**: they know they are playing an AI. Private games only
     (e.g. the owner's friend). **Never** public, ranked or matchmade multiplayer.
5. **Reporting.** Real-game results are reported separately from simulator results, labelled
   (opponent, difficulty, game version, map size, tribes, mode), with game counts and Wilson 95% intervals.
   Expected sample size is small (~10–20 games); results are presented as a demonstration, not as the main
   statistical evidence, which stays with the simulator tournaments.

## Consequences
- Needs two new tools in Phase 6: a fast way to enter/update a board state from the screen (a state editor), and
  an "advisor" front end that shows the engine's moves for the current turn.
- Hand relay is slow (≥ 1 hour per game) and error-prone; relay mistakes are logged per game.
- Distribution shift: the network is trained on Tribes rules. Phase 6 may need fine-tuning in real-rules mode;
  that GPU time is part of the Phase 4 budget discussion.

## Rejected alternatives
- **Build for the real game from the start.** No oracle → no automated rule verification; every later result
  would rest on unverified rules. Rejected.
- **Automated play (screen reading + input automation).** Likely against the game's terms and unfair in any
  multiplayer setting. Rejected.
- **Bot in public online multiplayer.** Cheating against people who didn't agree to it. Rejected permanently.
- **Decompiled logic / existing envs built on it** (e.g. polyterra-env). Midjiwan's property. Rejected (ADR-002).
