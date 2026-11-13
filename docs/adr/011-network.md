# ADR-011: Network inputs, architecture and training targets

- **Status:** Proposed (2026-11-13)
- **Date:** 2026-11-13

## Context
Phase 4 replaces two pieces of the Phase 3 PUCT (ADR-008): the **priors** (softmax of Rule Based scores) and the
**leaf value** (Diff heuristic). Both are replaced by one network, `f(observation) → (policy logits, value)`.
Everything else stays: the tree, selection, K = 4 determinizations (ADR-009), macro edges (ADR-010), and the
2,000 forward-model calls per decision of ADR-007 §4.

The output layout is already fixed by ADR-006: a 332-channel plane over the tile frame plus a 130-vector, with
legal-action logits gathered at their policy index. This ADR fixes the inputs, the trunk, the value head and
the training targets. Self-play, the inference backend and the training loop are in ADR-012.

Facts this depends on:
- **Phase 3's evaluation setting:**
  - Levels: 2-player Capitals, side 11 (dev and test levels, Tribes' generator).
  - Conditions: Paper (full obs) and Fog (partial obs).
  - Phase 4's goal is to beat that agent in that setting (buildplan §11).
- **Observation masking (04 §7, `observation.hpp`):**
  - Hidden tiles are `Terrain::Fog`.
  - Hidden from other tribes: their stars, techs, kills and monuments, and their cities' population and units.
  - Other tribes' units on hidden tiles are absent.
- **Throughput (notebook 2026-11-13, A2000 Laptop, fp16):**
  - A 64-channel × 6-block ResNet runs ~80k positions/s at 11×11 and ~17k at 24×24.
  - So running at the map's own side, not the 24 × 24 frame, matters (4.8×).
  - Trunk size is the main throughput lever.

## Decision

### 1. Inputs: planes built from the observation of the player to move, never from a state
`encode_planes(const Observation&) → float[C][side][side]` takes an `Observation`. ADR-005's type rule
then makes "no peeking" a compile-time property.
- At a tree node, the input is `observe(node_state, to_move)`, the same rule the Phase 3 priors use (ADR-010 §1).
- In training data, the input is the observation the player had at that decision.

**Perspective:** everything is relative to the player to move P. A tribe Q is encoded as r = (Q − P) mod n
(r = 0 is P), as ADR-006 does for diplomacy, so one network plays every seat. Channels for r = 0..7 are kept, so
the encoding allows 8 tribes; the curriculum (ADR-012) starts with 2 tribes.

**Tile planes** (per tile; 0 off the board):

| Group | Planes |
|---|---|
| On board; visible to P | 2 |
| Terrain one-hot (Fog included) | 8 |
| Resource one-hot (as observed: masked by tech) | 7 |
| Building one-hot | 19 |
| Territory owner r one-hot, plus "fogged owner" | 9 |
| Trade-network flag | 1 |
| City: present, owner r one-hot, level/10, capital, walls, population/need (own cities only), border radius/3 | 14 |
| Unit: type one-hot, owner r one-hot, hp/max_hp, veteran, status one-hot, carried type (naval) | 12 + 8 + 2 + 6 + 12 = 40 |

**Global features**, broadcast as constant planes:
- P's state: stars/20, tech bits (24), tribe type one-hot (12), score/1000;
- the opponents' visible scores (r one-hot order);
- tick/50;
- obs mode, game mode;
- `can_end_turn` (P), `turn_must_end`;
- P's monument statuses (7 × 4 one-hot).

About 80 planes in all. The exact list is fixed by the implementation and its tests, and recorded here.

**Scaling** constants are fixed and stated in code, not learned. Out-of-range values are clipped, and the
clipping is counted in tests.

### 2. Trunk and heads
- **Trunk:** a residual CNN at the map's own side (fully convolutional, so one set of weights runs on any side):
  stem 3 × 3 conv, then B blocks of two 3 × 3 convs with batch norm and ReLU, C channels.
  **Start: C = 64, B = 6** (~80k positions/s at 11 × 11, measured). Changing the size is decided by SPRT and
  throughput, not by taste.
- **Policy head:** a 1 × 1 conv → 332 channels (ADR-006 spatial part), plus a linear layer on
  global-average-pooled features → 130 (global part). The index gather maps ADR-006's 24-frame tile index
  `24x + y` to the map-side tensor's `x·side + y`. Logits of legal actions only; softmax over the node's edges
  (the macro subset where macro mode is on, ADR-010).
- **Value head:** pooled features → linear → tanh, v ∈ [−1, 1] = expected outcome for P
  (win +1, loss −1, draw 0, ADR-007's scoring). One scalar fits 2 players. More than 2 players would need a
  per-tribe value vector, decided when the curriculum gets there.
- **Search use:** the prior p_i is the softmax of the legal logits (temperature 1; T is again an SPRT parameter).
  The leaf value is mapped to the searching player's view by the node's `to_move`. ADR-008 §2's min/max
  normalisation of Q stays.

### 3. Training targets and loss (AlphaZero form)
- **Policy target:** the root's summed visit distribution over its edges (ADR-009 §2: summed over the K trees),
  keyed by ADR-006 index.
- **Value target:** the final result from P's view (+1 / −1 / 0).
- **Loss:** cross-entropy (policy) + mean squared error (value) + L2 weight decay.
- **Augmentation:** none at first. Polytopia boards aren't symmetric for a player's units under every flip, and
  ADR-006 offsets would need remapping. Revisit with measurements.

### 4. Bootstrap from Phase 3 (before any RL)
Train the first network **supervised** on records of the Phase 3 agent playing itself: visit targets and results
from the CPU-only search, ~400 games/h at 8 jobs (measured). That gives a starting network that already
encodes Rule Based priors plus search. The 24 h pilot's check (compute plan) is that this network's priors plus
value at least match Phase 3's priors plus Diff, by SPRT. Then self-play (ADR-012).

### 5. Tests (before any training run)
- **No peeking:**
  - `encode_planes(observe(s, P))` is unchanged when only hidden parts of s change (property test over
    random Fog games: re-sample hidden tiles, units and opponents' private values);
  - planes never depend on anything but the `Observation`.
- **Perspective:** swapping the two seats of a position and encoding from each side gives the same planes up to
  the r relabelling (hand-built positions).
- **Hand-built positions:** one per plane group, with the expected values derived from this table.
- **Policy gather:** the 24-frame → side mapping, checked against `encode_action` on every legal action of
  random games at sides 11 and 16.
- **C++ / Python parity:**
  - the same planes in both languages: C++ writes the tensor; Python reads it, never re-derives it;
  - the same network output in the C++ backend and in PyTorch, within fp16 tolerance.
- **Bug injection** on the encoder: a dropped perspective relabel, swapped x/y, an unmasked hidden field, the
  wrong side in the gather.

## Alternatives considered
- **Absolute tribe ids / a "seat" plane.** Rejected: relative encoding lets experience from either seat train
  one network (same reason as ADR-006).
- **Always run at the 24 × 24 frame.** Rejected: 4.8× slower at 11 × 11 (measured), for zeros.
- **Transformer over tiles or entities.** Not chosen to start: more costly per position at these sizes, and
  CNNs suit the grid locality of moves and attacks. Revisit if the CNN plateaus.
- **Keep Rule Based priors and learn only the value.** A fallback if the policy head underperforms. It would
  skip the factored head that ADR-006 laid out for this phase.
- **Score-difference value, or auxiliary heads** (final score, ownership). Not at first. KataGo-style auxiliary
  targets are a later SPRT candidate.
- **Inputs that include history (previous observations).** Not at first. Fog memory is what history would carry, and
  Tribes' vision is cumulative: a tile once seen stays visible, with its current contents (the simulator only
  ever sets a tribe's `visible` bits).

## Consequences
- New code: `nn/` (PyTorch model, dataset reader, trainer) and a C++ plane encoder next to `action_encoding`.
- The encoder is a second "golden" interface. Any change to it invalidates trained networks, so the planes get
  a version number stored with every record and every checkpoint.
- The 4 GB GPU limits the batch × trunk size. The measured 64 × 6 at batch 1024 fits.
