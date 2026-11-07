# Rule facts 02: combat, unit turn status, movement

Source: Tribes at commit `270e7e5`, read for facts only (ADR-002). Wording, structure and naming here are ours.
Conventions as in section 01. **[Tribes quirk]** = differs from the commercial game; we match Tribes.
**[Sim note]** = an implementation consequence for our simulator.

Coordinates are (x, y) with y growing downward. "Distance" is always Chebyshev: max(|dx|, |dy|).
"Neighbourhood of radius r" = the (2r+1)² square minus the centre, clipped to the board, enumerated
**x-major, then y, both ascending**. Enumeration order matters wherever it decides ties (pathfinding, pushes).

## 1. Unit turn status

Statuses: Fresh, Moved, Attacked, Moved-and-attacked, Pushed, Finished.

- At the start of a tribe's turn, every unit of that tribe becomes **Fresh**, except a unit that is
  **Pushed**, which becomes **Moved** (a pushed unit starts its next turn as if it had already moved).
- Newly created units (spawned, embarked, disembarked) start **Finished**; an upgraded unit keeps the old
  unit's status. [Sim note] Embark, disembark and upgrade create a *new* unit with a *new* actor id; the old one
  is removed.
- "Can move" / "can attack" = the transition below is allowed. Finished allows nothing.
- Any status may go to Finished. Recover, Examine, Capture set it; a converted unit becomes Finished.

| Unit class | Allowed | Result |
|---|---|---|
| Mind Bender, Catapult, Defender | move or attack, from Fresh only | Finished |
| Super unit | move from Fresh; attack from Fresh or Moved | Finished after either → in effect **one action only** [Tribes quirk] |
| Archer, Warrior, Swordsman, Boat, Ship, Battleship ("dash") | move from Fresh → Moved; attack from Fresh or Moved | attack → Finished |
| Rider ("escape") | move from Fresh, Attacked, Moved-and-attacked; attack from Fresh or Moved | see below |
| Knight ("persist") | move from Fresh → Moved; attack from Fresh, Moved, Attacked | attack → Finished; a kill → Attacked |

Rider transitions: Fresh –move→ Moved –attack→ Moved-and-attacked –move→ Finished;
Fresh –attack→ Attacked –move→ **Finished** [Tribes quirk: `transitionToStatus` is a chain of `if`s without
`else`, so Attacked → Moved-and-attacked is immediately followed by Moved-and-attacked → Finished; the
intended Attacked –move→ Moved-and-attacked never sticks. Corrected 2026-11-06 after a replay difftest
mismatch; this sheet previously said a Rider may attack then move twice]. So a Rider moves at most once after
attacking. It cannot attack from Attacked or Moved-and-attacked.

Knight persist: the attack itself first sets the Knight to Finished; scoring a kill then *forces* the status
to Attacked, from which it may attack again (not move). Chains continue as long as each attack kills.
The forced Attacked status is also applied when a Knight kills by **retaliation** on the enemy's turn
(harmless: statuses reset at its own turn start).

## 2. Attack

**Legality** (attacker A, target T):
- T exists and belongs to a different tribe. **No diplomacy check** — peace or alliance does not block an attack.
- A can attack (section 1) and is not a Mind Bender.
- T's tile is visible to A's tribe, and distance(A, T) ≤ A.range.
- Action generation scans the neighbourhood of radius A.range around A.

**Damage** (all arithmetic in double precision):

```
attackForce  = A.atk × A.hp / A.maxHp
defenceForce = T.def × T.hp / T.maxHp × bonus
total        = attackForce + defenceForce
attackResult  = round(attackForce  / total × A.atk × 4.5)
defenceResult = round(defenceForce / total × T.def × 4.5)
```

- `round` is Java `Math.round` = floor(x + 0.5). [Sim note] Use that, not `std::round`, and keep
  the exact operation order above for bit-exactness. Base atk/def are used in the last step (bonus only in the force).
- Veteran status does **not** change atk/def (only max HP, section 01).
- `bonus` (first match):
  1. T stands on a **city tile** whose city T's tribe owns: walls → ×4.0; else if T's type can fortify → ×1.5;
     else ×1. (A unit in a city it doesn't own gets no bonus, and no terrain bonus either.)
  2. Otherwise ×1.5 if: mountain + T's tribe has Meditation; or water + Aquatism; or forest + **Archery**.
  3. Otherwise ×1.
  - [Tribes quirk] Forest defence comes from Archery and mountain defence from Meditation;
    no other tech or terrain grants a bonus.

**Resolution, in order:**
1. A's status transitions as for "attack" (section 1). A's tribe pacifist counter resets.
2. Diplomacy repercussion for attacking, applied to (A's tribe, T's tribe). Details: section 04 §6.2.
3. If T.hp ≤ attackResult — **kill**:
   - The attack repercussion is applied **a second time** [Tribes quirk].
   - A and A's tribe each get +1 kill (Knight: persist, section 1). T is removed: off the board, off its
     city's unit list, and T's tribe loses T's score points.
   - **Advance:** if A is a Defender, Swordsman, Rider, Warrior, Knight or Super unit, A tries to move into T's
     tile using the push-target rule (section 4). Archers, Catapults, Mind Benders and naval units never advance.
     An advance does not change A's status.
4. Otherwise: T.hp −= attackResult. If distance(A, T) ≤ **T.range**, T retaliates: A.hp −= defenceResult;
   if A.hp ≤ 0, A dies and T and T's tribe get +1 kill. (Ranged targets retaliate at range; melee targets
   only when adjacent. Retaliation uses the *pre-attack* forces computed in the damage step.)

## 3. Movement

**Legality** of a move to destination D: the unit can move, D holds no unit, and a path exists under the
step rules below (Tribes checks this with a separate A* search; see the pathfinding note).

### Step rules (from tile F, with cost-so-far c, unit movement M)
- If c == M exactly, F has no outgoing steps.
- "On road at F": F is a road tile or a city tile, **and** F is outside every city's borders or inside a city
  of the mover's tribe. (Road tile = tile in the road layer that is not water and not a city.)
- For each neighbour N of F (radius 1, 8 directions), skip N if:
  - N holds a unit of another tribe (any diplomatic state);
  - N is not visible to the mover's tribe;
  - N is not traversable: mountain without Climbing; shallow water without **Sailing**; deep water without
    **Navigation**. This applies to every unit, naval included;
  - mover is a Mind Bender and N is a city tile not owned by its tribe.
- **Zone of control:** N is in ZoC if any tile adjacent to N holds a unit of another tribe.
- Step cost s ("rest" = M − c if c < M, else M):

| Mover | Terrain of N | s |
|---|---|---|
| Naval | city, plain, forest, village, mountain | rest (lands; disembarks) |
| Naval | shallow, deep water | 1 |
| Ground | shallow/deep water **with a port** | rest (embarks) |
| Ground | water without a port | not enterable |
| Ground | plain, city, village | 1 |
| Ground | forest, mountain | rest |

- **Road discount** (ground units only): if on road at F and N is a road or city tile, s = max(0.5, s / 2).
  This applies even when N is an enemy city or a road inside enemy borders — only F's ownership is checked.
  It also halves "rest" on a forest/mountain tile that has a road.
- **Accept** N:
  - in ZoC: s = rest, accepted if c + s ≤ M (entering ZoC ends movement);
  - otherwise accepted if **floor(c + s) ≤ M** [Tribes quirk: the floor lets a road half-step be followed by a
    full step on a 1-move unit, e.g. 0.5 + 1 = 1.5 → 1].
- Leaving a ZoC tile is not restricted; only entering one is.

### Executing a move
1. The unit is placed on D; its tribe clears fog around D with radius 1, or 2 if D is a mountain or the unit is
   a Battleship (clear view and tribe meeting: §03b.1; RNG only under partial obs).
2. Naval unit landing on a non-water tile → **disembark**: replaced by a new land unit of its carried type with
   the same HP, max HP, kills and veteran flag (new unit starts Finished). Ground unit ending on a **port** →
   **embark**: replaced by a new Boat carrying it, same HP/max HP/kills/veteran (starts Finished).
   [Tribes quirk] Move legality does not check port ownership; only Sailing (via traversability).
3. Otherwise the unit's status transitions as for "move".
   [Sim note] After embark/disembark, Tribes applies the transition to the *removed* unit, so the new unit stays Finished.

### Pathfinding note — move generation is order-dependent
Tribes generates the move set with a Dijkstra-like search over the step rules, using a binary-heap priority
queue keyed on cost-so-far. When a cheaper cost to an already-queued tile is found, the cost is overwritten
**in place without re-heapifying**, and a tile already expanded is never re-expanded. Because the step rules
depend on cost-so-far, the reachable set can depend on expansion order, and so on heap tie-breaking.

[Sim note] To match the move set bit-exactly we replicate the search as is, with a binary heap that behaves like
`java.util.PriorityQueue` (sift-up on insert; on removal move the last element to the root and sift down;
ties not reordered), neighbours inserted in neighbourhood order. Legality of a logged move is checked against
our generated set; any disagreement with Tribes' A* check shows up in the difftest. Decision recorded in the
state/action ADR.

## 4. Pushes and advance targets

**Push** (used when a city levels up or a ruin spawns a unit onto an occupied city tile; callers: level-up Super unit, ruin Super unit — the examining unit itself, §03b.5):
try target tiles in this order of (dx, dy): (0,+1), (−1,0), (0,−1), (+1,0), (−1,+1), (−1,−1), (+1,−1), (+1,+1),
skipping off-board tiles, until one succeeds. Whatever happens, the unit's status becomes **Pushed**.
If no target succeeds, the unit is **killed** (removed, score points lost).

**Target rule** (shared by push and attack advance; unit U of tribe P moving to tile X):
1. X holds a unit → fail.
2. X is a mountain: succeed (move) only if P has Climbing, else fail.
3. X is water:
   - U is naval → **reports success without moving** [Tribes quirk; a pushed naval unit stays put];
   - X has a port inside a city of P → U embarks there (succeed);
   - otherwise fail.
4. Otherwise move U to X (fog cleared as in a normal move) and succeed.
   Shallow/deep water traversal tech is **not** checked here.

## Resolved from section 01
- `NUM_STEPS` = 15 is the **explorer** step count (ruin reward), not a push parameter → §03b.6.
- Damage formula, ranged retaliation, fortify eligibility, turn-status transitions: above.
