# ADR-005: Simulator state encoding, chance interface and move generation

- **Status:** Accepted (2026-11-06)
- **Date:** 2026-11-06

## Context
The simulator state is copied on every search simulation and stepped millions of times in self-play
(CLAUDE.md §5: value type, cheap copy, no per-step heap allocation). Difftests replay Tribes games action by
action, inject Tribes' random outcomes (ADR-002 §3) and compare state after every action, in both observability
modes (ADR-004). Search may see only an observation (CLAUDE.md "no peeking").

Scope: the C++ representation of the game state, the observation type, the interface for random outcomes, and the
move-generation algorithm. The network's input planes are a separate Phase 4 ADR. The action encoding has its own ADR.

Facts this depends on (rule-facts sheet 01–05):
- Order matters in many lists: a tribe's city list, a city's unit list ("last unit" moves), building lists, and
  tribe-owned units. Removal keeps the order of the remaining elements.
- Actor ids come from one counter, incremented before use (first id 1). No rule iterates actors in id order (the
  only id-keyed iteration in Tribes is its board copy). Ids identify actors in logs only.
- Sizes: 2–8 players; map side 11–24 (≤ 576 tiles); 12 tribe types. Villages + capitals per generated map
  (1000 seeds each): max 8 (2 players) … 28 (7 players), 33 (8 players, n = 24).
- Random draws in the rules (all others are deterministic): ruin bonus (with redraw), random tech (ruin Research;
  meeting tech under partial obs), explorer-walk neighbour picks, city shuffles in unit re-assignment (village
  capture "move one", city capture "move all"), map generation, and the ranking's last tie-break (unseeded in Tribes).

## Decision

### 1. `GameState`: one fixed-capacity value type
- **Trivially copyable**, no pointers or heap: copying is one `memcpy`. Compile-time capacities:
  `kMaxSide = 24`, `kMaxTribes = 8`, `kMaxCities = 64`, `kMaxUnits = 512`, plus per-list limits.
  Every append checks capacity and **aborts loudly** on overflow (no silent drop). The difftest and long random
  self-play runs show whether a limit is ever hit. Capacities are revisited for Phase 6 (real-game maps up to 30×30).
- **Tiles, structure of arrays** over `side × side` (row-major, x = row): terrain, resource, building (1 byte each),
  unit slot (0 = none), owning city slot (none = sentinel), trade-network flag, and a per-tribe visibility bitset.
- **Actor pools:** units and cities live in fixed arrays of slots; a new actor takes the first free slot
  (simpler than a free list and equally deterministic; amended 2026-11-06 at implementation). Each actor stores its **Tribes
  actor id**, assigned by the same counter rule, so logged actions map to our actors without a translation table and
  states compare field by field. The board stores slot indices (fast); ids are for logs and difftests.
- **Ordered lists** (tribe → cities, city → units, city → buildings, tribe → tribe-owned units) are small inline
  fixed-capacity vectors with Java `ArrayList` semantics: append at the end, order-preserving removal.
- **Per tribe:** type, tech bitset, stars, score, capital slot, result, monument and diplomacy counters (01, 03a, 04).
  Diplomacy: an 8 × 8 allegiance matrix. Global: tick, active tribe, game mode, observability mode (ADR-004).
- **Size:** estimated ~22 KB; **measured 32.7 KB** at implementation (Unit 20 B, City 200 B, TribeState 680 B),
  ~1.1 µs per copy (one-off microbenchmark, i9-11950H, WSL2, g++ 15 -O3). Most of it is unit and city capacity that
  small games never use. `make bench` reports `sizeof(GameState)` and copies/s next to states/s.
  If copying turns out to dominate search, the first fallback is to shrink the capacities to the game's actual
  side (templated capacity). That decision will be made from measurements, not in advance.

### 2. `Observation`: a distinct type wrapping a masked `GameState`
- `observe(state, tribe)` builds a `GameState` with hidden information masked exactly as in 04 (fog terrain value,
  opponents' stars, tech and city internals hidden, resources masked by tech), wrapped in a separate
  `Observation` type.
- Search and agents take `const Observation&` and **cannot** receive a `GameState`. Only the determinizer
  (Phase 3, ADR on hidden information) can turn an observation into a sampled `GameState`. The type system
  enforces "no peeking". The property test "observation never leaks hidden tiles" tests `observe`.
- The full-obs mode is the same code with nothing hidden in the fog layer (04: tech/star masking still applies).
- *Outcome (2026-11-07):* implemented as `poly::Observation` (only `observe` constructs it; `view()` gives the
  masked `GameState`). It mirrors Tribes' agent copy exactly, quirks included (04 §7: e.g. points-worth 0), and
  is difftested against that copy before every decision. A foreign city on a hidden tile stays as a ghost slot
  (id, tribe −1) because capital ids and visible tiles' owners refer to it. Legal actions are not derivable from
  an observation (04 §7), so agents receive them from the environment, as in Tribes.

### 3. `Chance`: semantic random outcomes, passed explicitly
Every rule that needs randomness calls an explicitly passed `Chance&` with a **semantic** request, never a raw
RNG draw:

| Request | Returns |
|---|---|
| ruin bonus (given whether Research is excluded) | one of the 5 outcomes |
| random tech (given the candidate set, enum order) | a tech |
| explorer neighbour (given the clipped neighbour count) | an index |
| city order (given the list to shuffle) | a permutation |
| ranking final tie-break (given the tied tribes) | an order |

Two implementations:
- **`SampledChance`** wraps `poly::Rng`: same distributions as Tribes (uniform), our own draw order. Used in
  play, search and self-play.
- **`ReplayChance`** reads outcomes from a Tribes log in order. On each request it checks that the kind and the
  domain (candidate set, count, list) match the logged one, and **fails the difftest** on any mismatch.
  So a rules divergence that changes *when* or *over what* randomness is drawn is caught where it happens,
  not several actions later.
- Map generation is outside `Chance`: in difftests the level is loaded from the log (05 §1). Our generator
  (05 §2–§3) takes a `poly::Rng` and is checked statistically (05 §4).
- Ranking tie-break: the Tribes draw is unseeded, so the oracle logs the order it got and replay uses it.

### 4. Move generation: replicate Tribes' search exactly
Generate a unit's move set with the same Dijkstra-like search as Tribes (02 §2.3): a binary heap that behaves like
`java.util.PriorityQueue` (sift-up on insert; on pop the last element goes to the root and sifts down), cost
overwritten in place without re-heapifying, no re-expansion, neighbours pushed in neighbourhood order. The heap is a
fixed-capacity array (≤ 576 entries), so there is no allocation. Implemented from the behaviour described in 02, not
by translating Tribes' code (ADR-002).

## Alternatives considered
- **Dynamic containers (`std::vector`, maps keyed by id).** Rejected: heap allocation on every state copy, which
  violates the hot-path rule and costs time in search.
- **Templated capacity per map size from the start.** Deferred: more code and template bloat for an unmeasured gain.
  It stays available as the first optimisation if copy cost shows up in profiles.
- **Our own id scheme with a translation table for difftests.** Rejected: an extra mapping in every comparison and
  every log line, and it saves nothing, since Tribes' counter rule is trivial to follow.
- **Same type for state and observation (a flag).** Rejected: a fog leak would then be a runtime bug instead of a
  compile error.
- **Raw-draw injection (replay Java `Random` values).** Rejected in ADR-002: it ties our code to Java's call order
  and catches divergences late.
- **Explicit chance nodes in the action space** (the agent "chooses" the ruin bonus). Not now: the `Chance`
  interface already lets MCTS sample outcomes at a node. Explicit chance nodes can sit on top of it later if
  search needs them (MCTS ADR).
- **Correct Dijkstra for movement.** Rejected: its reachable set can differ from Tribes' (02 §2.3), so logged
  moves would be illegal for us or vice versa, and every difftest would need an exception. Replicating the quirk
  is cheap. If the real game (Phase 6, real-rules mode) differs, that mode gets its own variant and ADR.

## Consequences
- Difftest comparison is a field-by-field compare of two `GameState`s, with no id mapping.
- Capacity overflows abort: a wrong limit fails loudly in tests, never silently in results.
- Performance claims for this layout wait for `make bench` (`[TBD]` until measured, with hardware recorded).
- The oracle bridge must log, per random request: kind, domain and outcome (next step in progress.md).
