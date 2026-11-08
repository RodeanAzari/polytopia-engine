# ADR-006: Action encoding and factored policy head

- **Status:** Accepted (2026-11-08)
- **Date:** 2026-11-08

## Context
Search (Phase 3) needs a stable identity for each action at a decision: tree edges, priors, visit counts,
and statistics aggregated across determinized states (§7.3) must refer to "the same action" even when the hidden
part of the state differs. The network (Phase 4) needs a fixed-size policy output, the **factored policy head
(actor × action type × target)** of docs/buildplan.md §8, from which the logits of the legal actions are read in
one forward pass.

`poly::Action` (the simulator's internal form, ADR-005) names actors by Tribes actor id and stores raw
parameters. Ids mean nothing to a network, and the legal list's order is generation order (04 §5), which shifts
whenever an actor is added or removed. Neither is an encoding.

Scope: the encoding of one action of the active tribe, its index space, and how the policy head is laid out.
The network trunk, input planes and loss are Phase 4 ADRs; macro-actions have their own ADR (they will map onto
this encoding or extend it).

### Measured facts (2026-11-08)
Uniform-random games on the 1,200 `make bench` levels (2/4/8 players × both modes), legal sets taken after every
turn start and action; scratch tool, git `ee65ff9`:

| | full obs | partial obs |
|---|---|---|
| legal-action sets | 1,528,329 | 1,593,640 |
| actions per set: mean / median / p99 / max | 51.7 / 28 / 245 / 416 | 47.8 / 29 / 179 / 353 |
| sets where the key below collides | **0** | **0** |
| max Chebyshev offset actor → target: Move / Attack / Convert | 7 / 3 / 1 | 7 / 3 / 1 |
| max offset city → worked tile (Gather, Build, forest actions, Destroy) | 2 | 2 |

The most legal actions seen in the 140-game oracle corpus (Rule Based vs. Random) was 829 (rules.hpp).
Send stars accounts for 36.1M of the ~79M legal actions above (full obs): diplomacy dominates the raw branching,
which matters to the macro-action ADR, not to this one.

### Rule-derived bounds (so the window is a proof, not an observation)
- **Move:** every step costs ≥ 0.5 (the road discount is `max(0.5, s/2)`) and a step is accepted only if
  `floor(c + s) ≤ M`, i.e. `c + s < M + 1`, so a path has at most 2M + 1 steps (02 §2.3). Halved "rest" costs
  on forest/mountain roads are not multiples of 0.5, which lets c end strictly between M and M + 1 and still
  step on: the measured 7 for knights (M = 3) is this bound, reached. Max M is 3 (01), so **|Δ| ≤ 7**.
  (I first derived 6, assuming half-step costs; the measurement caught it.)
- **Attack:** distance ≤ range ≤ 3 (catapult; 02 §2). **Convert:** Mind Bender range 1.
- **City actions on tiles:** the tile is within the city's borders, radius ≤ 2 (03a); a tile has one owner.

## Decision

### 1. Canonical key: (location tile, channel), computed from the observation
Every action of the active tribe P maps to one **policy index** in a fixed space of size
`kPolicySize = 576 × 332 + 130 = 191,362`:

- **Tile frame:** tiles are indexed in a fixed 24 × 24 frame, `t = 24·x + y` (kMaxSide; ADR-005), so the index
  doesn't depend on map size. Unused tiles of smaller maps are never legal.
- **Spatial part:** index `t · 332 + channel`, where t is the action's **location** and the channel is:

| Group | Location | Channels | Count |
|---|---|---|---|
| Unit, no target: Recover, Examine, Capture, Disband, Upgrade, Make veteran, Heal others | unit's tile | one each | 7 |
| Move | unit's tile | offset (Δx, Δy) ∈ [−7, 7]², ≠ (0, 0) | 224 |
| Attack | unit's tile | target unit's offset ∈ [−3, 3]², ≠ (0, 0) | 48 |
| Convert | unit's tile | target unit's offset ∈ [−1, 1]², ≠ (0, 0) | 8 |
| City level-up | city's tile | bonus (LevelUpBonus enum) | 8 |
| City spawn | city's tile | unit type | 12 |
| Gather, Clear forest, Grow forest, Burn forest, Destroy, Build road | **the worked tile** | one each | 6 |
| Build | the worked tile | building type | 19 |

- **Global part** (index `576 · 332 + k`): End turn (1); Research, by tech (24); Declare war, by **relative**
  target r = (Q − P − 1) mod n (7); Send stars, by r and amount 1–14 (7 × 14 = 98). Total 130.
- **Why tile-located city actions:** Gather, Build etc. sit on the tile they change, which a tile's single
  owning city implies (03a), and the network sees that tile's features directly. Level-up and Spawn sit on the
  city. A unit on its own city tile uses different channels from the city, so they never collide.
- **Computed from the `Observation`**, never the true state: own units and cities are always visible, and an
  attack or convert target is a visible unit (02 §2), so `encode(obs, action)` needs nothing hidden.
  Because the key uses tiles, not actor ids, it means the same thing in every determinization of an
  observation: search statistics can be shared across samples.
- **Decode** is by matching: the environment hands the legal list (04 §7), each legal action is encoded, and
  an index maps back to the legal action with that key. No inverse function to keep in sync.

### 2. Policy head (Phase 4): planes over the tile frame plus a global vector
The head emits a **332-channel plane** over the 24 × 24 frame (e.g. a 1 × 1 convolution of the trunk) and a
**130-vector** from pooled features. Legal-action logits are gathered at the encoded indices; the softmax is over
legal actions only (masked). This is the buildplan's "actor × action type × target" factorization: the
location is the actor (or the worked tile), the channel fixes the type and the target (an offset or a parameter).
One forward pass prices every legal action at once, which is what PUCT priors need.

### 3. Guarantees, checked by tests (implementation step after acceptance)
- **Injective over every legal set** (the measurement above becomes a property test over random games in both
  observability modes); any collision or out-of-window offset aborts loudly, never truncates.
- `encode(observe(s, P), a) == encode(s, a)` for every legal a: computing it from the observation loses nothing.
- Round trip: `decode(encode(a)) == a` over the legal set; indices in `[0, kPolicySize)`.
- Hand-built cases per action group, with expected indices derived from this table (not from the code).

## Alternatives considered
- **The legal list's position (Tribes-style index).** Rejected: generation order shifts with every new or removed
  actor (04 §5), so "index 17" means different actions in sibling nodes and across determinizations.
- **Actor id × type × target id.** Rejected: ids are arbitrary to a network, and a determinized hidden state can
  invent units, so id-based edges don't line up across samples.
- **Absolute target tile (source tile × target tile).** Rejected: 576 × 576 per type, almost all impossible;
  offsets give the same information within the proven windows.
- **Autoregressive head (choose actor, then type, then target).** Rejected for now: it needs a forward pass per
  stage and per candidate, but PUCT wants priors for all legal actions at once. Revisit if the action space grows
  beyond what planes can hold (Phase 6, real game).
- **Per-action scoring (embed each legal action, score with attention / a pointer network).** A real option: no
  fixed size, any action type. Not chosen: a forward pass per decision scales with the number of legal actions
  (p99 245, max 829), and batching variable sets is harder for C++ inference. The encoding here doesn't preclude
  it: the key can also serve as that network's action features.
- **Absolute tribe ids for Declare war / Send stars.** Rejected: relative ids let one tribe's experience apply
  from every seat.

## Consequences
- 191,362 outputs, mostly Move channels (224 of 332). That's cheap as a 1 × 1 convolution; training targets are
  sparse (MCTS visits on legal actions only).
- The windows are tied to Tribes' rules (max movement 3, range 3). A rules change that raises them (Phase 6,
  real-game mode) needs a new encoding version, and the encoder fails loudly before then.
- Search can key edges by policy index from Phase 2 on (an int, cheap to hash and compare), and Phase 4 reuses
  the same index for the network.
- Tribe-relative diplomacy channels assume ≤ 8 tribes (kMaxTribes, ADR-005).

## Outcome (2026-11-08)
Implemented as `poly::encode_action` / `poly::decode_action` (`sim/include/poly/action_encoding.hpp`), exactly as
the table above. Tests (`tests/sim/test_action_encoding.cpp`): hand-computed indices for every action group and
both window corners; loud failure outside the window and for a target hidden by fog; a property test over 24 random
games on random maps (both observability modes, every action type exercised) checking range, uniqueness within each
legal set, observation = true state, and decode round trip. 7 injected encoder bugs (window centre, swapped axes,
relative-tribe sign, Gather located on the city, Build parameter dropped, reversed attack offset, window check
removed) were each caught. On the 1,200 Tribes-generated bench levels: 155,232,869 legal actions in 3,121,969
legal sets (both modes) encoded with 0 failures.
