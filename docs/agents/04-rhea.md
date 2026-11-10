# Agent facts 04: RHEA (rolling horizon evolution)

Facts-only sheet (ADR-002 amendment). Framework and Diff heuristic: sheet 01.
Paper configuration: **population 1** (a random-mutation hill climber), plan length 20, mutation rate 0.1,
Diff heuristic, budget 2,000. No root prioritization. Parameters that have no effect at population 1
(tournament size 3, "mutate best" 9, elitism, crossover) are listed in §5 but not implemented by us.

## 1. Terms
- **Plan**: a list of concrete actions (actor, type, parameters), not indices. It may contain End Turn; the
  actions after it are then played **for the opponent** in the simulation (the plan is one sequence across
  turns, whoever is active).
- **Random action** in a state: uniform over all legal actions **except Destroy and Disband**, End Turn
  included.
- **Feasible** action in a state: the game is not over; for a unit or city action the actor exists, is of that
  kind and belongs to the active tribe; and the action is legal there. (We test legality as membership in the
  legal-action set; Tribes calls the action's own feasibility check, which agrees with legal-set membership as
  far as our difftests show.)
- **Evaluate** a sequence from observation S: copy S, apply the actions in order (one budget unit each), value
  = Diff(S, final state).

## 2. Per call, on observation S
The agent keeps one plan with a value between calls.
1. Budget counter := 0.
2. **New turn** = S's tick differs from the tick of the previous call (first call of the game counts as new).
   - New turn: **init** (§3.1).
   - Same turn: **shift** (§3.2).
3. Loop: **mutate** the plan (§3.3); if the mutant's value ≥ the plan's value, the mutant replaces the plan
   (ties replace). Stop when the counter ≥ 2,000, checked after each mutation; at least one mutation always
   runs.
4. Return the plan's first action.

Note: there is no single-legal-action shortcut and no End Turn fallback; with only End Turn legal, every
feasible plan starts with End Turn (the rest are opponent actions), so End Turn is returned.

## 3. Operators
### 3.1 Init
From a copy of S, repeat until 20 actions or game over: draw a random action, apply it, append it. Value =
Diff(S, result).

### 3.2 Shift (quirk-exact)
1. Drop the plan's first action (it was played last call). The plan now has length L (19 normally; can be
   longer, see below).
2. From a copy of S, apply the plan's actions in order while each is feasible (no cap at 20); stop at the
   first infeasible one, at index j (j = L if all are feasible).
3. Then, while not game over and the number of applied actions < 20, draw a random action, apply it and
   **append it to the end of the plan** (after any not-applied tail).
4. Value = Diff(S, result of the applied actions).

**The infeasible tail is not removed.** With j < L, the stored plan is: applied prefix (j) + stale tail
(L − j, starting with the infeasible action) + new random actions (max(0, 20 − j)). Its value comes from a
different sequence (prefix + new random). Consequences: a plan grows by up to 19 per shift (one dropped, up to 20
appended), so within a turn it can exceed 20 by a lot, and later evaluations apply more
than 20 actions; the plan's first action can be infeasible (j = 0) and is returned if no mutant is accepted (§4).

### 3.3 Mutate
From a copy of S, for each index i of the plan while not game over:
- with probability 0.1: a random action;
- otherwise the plan's action i if feasible, else a random action;
apply it and append it to the mutant. Mutant length = plan length (shorter on game over). Value = Diff(S, result).

## 4. What the game does with an infeasible returned action
The game tries it, nothing changes, and the agent is asked again (tick unchanged → shift drops that action).
Our tournament runner must do the same: **an illegal action returned by an agent is a no-op and the agent
is called again** (never an assert). Only RHEA can trigger this.

## 5. Population > 1 (not used in the paper; not implemented)
Init draws POP_SIZE random plans; shift keeps the shifted best, 9 mutants of it, the rest new random plans;
each generation keeps the elite and fills with uniform crossover of two tournament-selected parents (size 3,
noisy values), mutation rate 0.1 per gene, falling back to the other parent's gene or skipping infeasible
genes, padded with random actions to length 20. Recorded so the sheet is complete; Phase 2 doesn't need it.

## 6. Notes for our implementation
- Store plans as simulator actions; feasibility = legal-set membership after the actor checks.
- Unit tests: shift keeps the stale tail and appends after it (from length 20: 39 − j for j < 20); ties accept the mutant;
  End Turn inside a plan makes later actions the opponent's; illegal returned action is a no-op in the runner.
