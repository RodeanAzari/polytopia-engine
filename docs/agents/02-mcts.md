# Agent facts 02: MCTS

Facts-only sheet (ADR-002 amendment). Shared framework, root prioritization and the Diff heuristic: sheet 01.
Paper configuration: C = √2, max depth 20, rollouts off, root prioritization on, 2,000-call budget, Diff.

## 1. Per call
1. If exactly one legal action exists: return it (no search).
2. Root candidates = the prioritized group (01 §3); none → return End Turn.
3. Build a fresh tree whose root holds the observation S and one child slot per root candidate.
4. Iterate (§2) until the budget counter exceeds 2,000 after an iteration.
5. Return the root candidate chosen by §5.

## 2. Iteration: selection, expansion, evaluation, backup
**Selection.** From the root, while the node's state is not game-over and node depth < 20:
- if any child slot is empty → **expand** that node (below) and stop;
- else move to the child chosen by the UCT rule (§3).
If the loop stops without expanding (depth 20 or game over), that node is evaluated again.

**Expansion.** Pick an empty slot uniformly at random (one uniform draw per slot, the largest draw among empty
slots wins). Copy the node's state, apply that slot's action with the forward model, compute the legal
actions of the result, and create the child with one slot per legal action.
- At depth 0 the slots are the root candidates; at depth ≥ 1, **all** legal actions of the node's state
  (Destroy and Disband included), in the simulator's generation order.
- The tree continues through End Turn: nodes after it belong to the opponent's turn (and back again), up to
  total depth 20.
- Forced End Turn exists in the code but never fires in the Paper configuration (01 §2).

**Evaluation (rollouts off).** value = Diff(S, state of the new node) (01 §4). No rollout. (With rollouts
on, a random playout to depth 20 would be evaluated instead; not used, not implemented by us.)

**Backup.** Every node on the path back to the root, including the new node: visits += 1, total value += value,
and the node's running min and max of all values backed up through it are updated. No sign flip for the
opponent (min-max is handled in selection).

## 3. UCT selection at a fully expanded node N
For each child c:
- mean = total(c) / (visits(c) + ε), ε = 1e-6;
- normalised mean: (mean − min_N) / (max_N − min_N) using **N's** min/max of backed-up values; if min_N ≥ max_N
  (not set yet, or all equal), the mean is used unnormalised;
- u = normalised mean + C · √( ln(visits(N) + 1) / (visits(c) + ε) ), C = √2;
- random tie-break: u′ = (u + ε) · (1 + ε · (r − 0.5)), r uniform in [0, 1), one draw per child.

Choose the **largest u′ if N's state has P (the searching tribe) active, otherwise the smallest u′**
(opponent nodes minimise the same quantity, exploration term included: a quirk). Ties are broken by the
first index (essentially impossible after the noise).

## 4. Budget counter
One counter per call, on the root. **+1 per forward-model action applied in expansion, and +1 per UCT
selection step** (a descent through a fully expanded node counts even though no action is applied). So an
iteration that expands at depth d costs d units. Stop when the counter > 2,000, checked after each iteration.

## 5. Final choice at the root
- Among expanded root children, take the most visits (with the same multiplicative noise tie-break, one draw
  per child).
- **If all expanded root children have equal visit counts**, take instead the highest mean value
  (total / (visits + ε), noise tie-break).
- If no child is expanded, take candidate 0.
- Return that candidate action.

## 6. Notes for our implementation
- Same observation (04 §7) and the same forward model, so tree states match Tribes' up to RNG.
- Node pool, no per-node heap allocation (CLAUDE.md §5); actions stored by policy index (ADR-006) plus the
  simulator action for applying.
- Unit tests: the counter arithmetic (§4), equal-visits fallback (§5), min/max normalisation off until two
  distinct values (§3), opponent-node minimisation, slot order at depth 0 vs. ≥ 1.
