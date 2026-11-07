# Rule facts 03b: capture, ruins, explorer, borders, roads, other unit actions

Source: Tribes at commit `270e7e5`, read for facts only (ADR-002). Wording, structure and naming here are ours.
Conventions as in sections 01–03a (**[Tribes quirk]**, **[Sim note]**, neighbourhood order x-major then y).
"Native" = the real game state; Tribes agents search on *non-native* copies, which behave differently in a
few places (marked). Our simulator is always native.

## 0. Observability mode of this commit (read first)

Tribes' compile-time constant `PLAY_WITH_FULL_OBS` is **true** at this commit: every tribe's vision grid
starts all-visible. Consequences in the oracle as built:
- fog clearing never reveals anything → no reveal score (+5/tile), no reveal-triggered network updates;
- tech gained on meeting a tribe and the Eye of God unlock are disabled (both gated on partial obs);
- "visible" checks (roads, convert/attack target visibility) always pass.

The rules below give the partial-obs behaviour too, so the simulator can run either mode.
The difftest checks both modes (ADR-004).

## 1. Clear view (vision update; used by moves, explorer, new cities)

Clear view for tribe P at centre X, radius r: tiles = neighbourhood(X, r), then X itself. For each tile, in order:
1. If not yet visible to P: mark visible; P's score += 5; if the tile is a road (network flag, not water,
   not city) or water, a network recomputation is requested (done by the caller, see below).
2. Meeting: if a unit stands on the tile, P "meets" the unit's tribe; if the tile is in a city's borders,
   P meets the city's tribe. (Visibility of the tile is not required for meeting.)

**Meet tribe Q** (by P) [Tribes quirks]:
- If P's met-list is non-empty: nothing happens if Q = P or Q is already in it. If the list is **empty**, Q is
  added even when Q = P, so a tribe's first clear view usually adds **itself** (it sees its own city).
- The code intends "Q also meets P" when P enters Q's vision, but calls the meet on P with P's own id, so it
  never makes Q meet P. Each tribe only meets others through its own clear views.
- Partial obs only: on a new meeting, P gains one random tech Q has and P lacks (uniform draw over techs in
  enum order; no draw if none).
- Partial obs only: afterwards, if Eye of God is *unavailable* and every tile is visible, it becomes *available*.

Callers: a unit move (section 02), the explorer (requests a network recomputation if step 1 asked for one),
a new city (radius 1; reveal-triggered recomputation not requested). [Sim note] The new-city call runs on a
board copy in Tribes; no observable difference.

## 2. City tiles and borders

- A city's **border radius** starts at 1. Border growth (level-up reward) adds 1 and claims tiles.
- **Claim tiles** (city C, radius r): neighbourhood(C, r) then C's own tile; each tile with **no** owner gets
  owner C, and C's tribe score and C's points-worth += 20. Already-owned tiles are never taken.
- **City tiles for action generation** (Build, Destroy, forest actions, Gather, and Build's unique-per-city
  check): tiles owned by C within Chebyshev radius **1 if level < 4, else 2** of C, scanned x-major then y.
  [Tribes quirk] This ignores the border radius: tiles claimed beyond that square are owned (score, network,
  enemy road blocking) but no city action can use them.

## 3. Capture

**Legal** (unit U of tribe P, Fresh, on tile X):
- X is a village → capture village (no other checks: diplomacy, enemies near etc. are ignored);
- X is a city centre whose owner ≠ P → capture city. (U necessarily stands on the city tile.)
  [Tribes quirk, found 2026-11-07] The city is looked up as X's **border owner**, and its centre must be X.
  A city founded on a village inside another city's borders keeps that tile owner (village step 3), so it
  **can never be captured** (e.g. a village inside your capital's borders, once captured). Covered by a unit
  test; the oracle corpus never reaches it.
Generated at most once per unit.

**Village capture:**
1. U → Finished.
2. New city C at X: level 1, population 0/2, not capital, no walls, border radius 1, new actor id. Added to P's
   city list (appended). Clear view radius 1 around X. X's network flag set (network recomputed).
3. Claim tiles (C, 1) (+20 each newly owned tile, including X if unowned). [Tribes quirk] If X was already inside
   another city's borders, X keeps that owner.
4. X's terrain → city.
5. **Unit re-assignment to C** (one unit, "move one"): if P owns its capital and the capital's unit list is
   non-empty, the **last** unit of the capital's list moves to C's list. Otherwise P's other cities (city list
   minus the capital, which includes C itself) are shuffled with the game RNG (Java `Collections.shuffle`), and
   the first city with ≥ 1 unit gives its last unit. [Tribes quirk] The moved unit is usually **not** U.
   No shuffle (no RNG draw) when the capital branch applies.
6. P's score += 100 (city centre; not added to C's points-worth).
7. X's network flag set again (recomputed again).

**City capture** (city C of tribe Q):
1. Q's score −= C's points-worth; P's score += it.
2. Diplomacy: capture repercussion (−30) applied to (P, Q), then consequences checked (04 §6.2).
3. U → Finished.
4. C's owner → P, C appended to P's city list. Then for each building of C (list order), the 03a building
   effects are re-applied with sign +1 in **matching-only** mode, with P as the tribe (below).
5. C removed from Q's list. Then for each Farm / Mine / Lumber hut / Port of C, effects re-applied with sign −1,
   matching-only, again with **P** as the tribe (C's owner is already P).
6. Q's units of C move out ("move all"): while Q owns its capital and the capital has room, the **last** unit
   of C's list moves to the capital. Then, if any remain: Q's other cities (minus capital) are shuffled (game RNG)
   and filled in that order while they have room. Any still left become Q's **tribe-owned** units (no city),
   appended in C's list order (front to back).
   No shuffle when the capital took them all.
7. "Move one" (village step 5) for P into C.
8. If Q now has no cities (native state only): Q **loses**: all of Q's units are removed from the board (city
   units and tribe-owned units). No score is subtracted for them. Win/loss handling: 04 §8.
9. X's network flag set (recomputed).

**Matching-only mode** (03a building effects, steps 4–5): the building's own population bonus (base
buildings, port, temples, monuments) is **not** applied; only the neighbour-pair bonuses are (with the same
loop quirks as 03a), plus the score parts: temples give +100 (step 4), monuments +400 (step 4).
[Tribes quirk] Step 4 re-adds every pair bonus once per building of the pair (so a Farm–Windmill pair inside C
is counted again from both ends); step 5 removes it only from the base/port end. Net effect: pair bonuses
grow on each capture. The simulator replicates the procedure, not an intended outcome.

Other capture facts:
- A captured capital keeps its **capital flag**: it still gives +1 production, now to P. Q's capital id
  still points to it, so Q no longer "controls its capital" (03a §6: Q loses all trade connections); P's
  capital is unchanged.
- C's tiles keep owner C (now P's), so its border score moves with points-worth.

## 4. Unit-to-city assignment when a city is full

A city's unit list silently **ignores** an add when full (level + 1 units). Cases:
- Spawn checks capacity first (03a), so it never overflows.
- Level-up Super unit (03a §2): added to the city's list; if the city is full, it is on the board but in
  **no** list (neither city nor tribe-owned) [Tribes quirk]. It still plays; it is invisible to "move all" on
  capture and is not removed on tribe loss.
- Upgrade (§7) removes the old unit first, so there is room.
- Re-assignment on capture (§3) only fills cities with room.

## 5. Ruins (Examine)

**Legal:** unit U Fresh, standing on a tile whose resource is ruins, and U's tribe P has ≥ 1 city.

**Execute:**
1. Handler city H = P's capital if P controls it, else the first city in P's list.
2. Bonus = uniform draw over the 5 outcomes in order (Super unit, Research, Population growth, Explorer,
   Resources) — one `nextInt(5)`. If P has the "all researched" flag, redraw while it is Research.
3. Apply:
   - **Super unit**: if X is water, a Battleship carrying a Warrior; else a Super unit. It is a **tribe-owned**
     unit (no city), kills 0, not veteran. First, U itself (standing on X) is **pushed** (section 02 §4;
     killed with score loss if it can't be). Then the new unit is placed on X (new actor id). X's ruins cleared.
     New unit's status: as created (Finished per section 02).
     [Tribes quirk, found by the replay difftest 2026-11-06] The water Battleship has **HP −1 / max HP −1**: naval
     units' constructors take max HP −1 (they normally copy HP from the land unit they replace), and this path
     only sets the carried type. Combat then reads HP ratio 1, and any hit kills it (−1 ≤ damage).
   - **Research**: one random researchable tech (uniform over techs in enum order that are researchable now)
     is marked researched. No stars, **no score** and no Tower of Wisdom unlock [Tribes quirk: those live in the
     Research action only]; the "all researched" flag is still re-checked if it is a leaf tech (section 01).
     The meeting tech (§1) behaves the same way.
   - **Population growth**: H gains +3 population (P's score +15).
   - **Explorer**: explorer walk (§6) from X.
   - **Resources**: P gains +10 stars.
4. X's ruins resource cleared (all bonuses). U → Finished (no effect if U was killed).
[Sim note] In difftests the bonus is injected from the Tribes log (ADR-002); the Research tech, explorer
steps and push RNG are injected the same way.

## 6. Explorer walk

From start tile S for tribe P (callers: ruin reward, city level-up Explorer from the city tile):
15 steps. Each step: up to 45 tries; each try picks a uniformly random neighbour of the current tile
(neighbourhood radius 1, board-clipped; one `nextInt(count)`), and if it is **traversable** for P the walk
moves there, clears view radius 1 (§1), recomputes the network if requested, and the step ends.
If all 45 tries fail, the step is skipped (position unchanged).
Traversable: mountain needs Climbing, shallow water Sailing, deep water Navigation; units, borders and
other terrain are ignored. No unit is created.

## 7. Other unit actions

| Action | Legal iff | Effect |
|---|---|---|
| **Recover** | U Fresh; 0 < HP < max HP | HP += 2, +2 more if U's tile is in borders of one of U's tribe's cities; capped at max HP; → Finished |
| **Heal others** | U is a Mind Bender and can attack (section 02 status); some unit of U's tribe within U's range (neighbourhood, U's tile excluded) has HP < max | each such unit +4 HP, capped; U → **Finished** (transition "attacked" for a Mind Bender, 02 §1) |
| **Convert** (target T) | U is a Mind Bender and can attack; T is another tribe's unit within range; T's tile visible to U's tribe | T leaves its city list (or Q's tribe-owned list), joins the **active** tribe as a tribe-owned unit (city id → none), → Finished; U → **Finished**. No score moves. Diplomacy: see quirk |
| **Make veteran** | U not a Super unit; kills ≥ 3; not veteran | veteran; max HP +5; HP = max HP. **Any** status, status unchanged |
| **Upgrade** | Boat + Sailing + stars ≥ 5, or Ship + Navigation + stars ≥ 15. **Any** status | −cost; replaced by a new Ship / Battleship (new actor id) with same HP, max HP, kills, veteran flag, carried land unit, city, and **status** |
| **Disband** | U Fresh; Free Spirit | +floor(U's cost / 2) stars; U removed; score −U's points |

- Convert's diplomacy repercussion (−5) is applied to (P, T's tribe) **after** T's tribe was changed to P, so it
  hits (P, P) [Tribes quirk], then consequences are checked for (P, P) (04 §6.2). Convert ignores
  diplomacy status (can target allies).
- Generation: Convert scans U's neighbourhood (radius = range) for other tribes' units; Heal others and the
  rest yield at most one action per unit.
- Verified 2026-11-07 against the oracle (Mind Bender scenario, `make difftest-mindbender`): this table said
  U → Attacked for Heal others and Convert; Tribes' transition makes a Mind Bender Finished. Corrected above.
- Disband's cost is the unit-table cost (section 01: Ship 5, Battleship 15, Super unit 10).

## 8. Roads

**Legal** (tribe P, tile X): P has Roads; stars ≥ 2; X visible to P; X's terrain is plain, forest or **village**;
X is unowned or owned by one of P's cities; X has no network flag; no unit of **another tribe** on X
(diplomacy ignored). Generated by scanning the whole board x-major then y.
**Execute:** −2 stars; X's network flag set; network recomputed (03a §6).

## 9. Ports on the board

Building a port sets its tile's network flag; destroying one clears it; each triggers a recomputation.
(A city centre's flag is set when the city is created and on capture.)

## 10. Network recomputation scope (correction to 03a §6)

Every flag change recomputes the network of **every tribe** in tribe-id order in a native state. Only in
non-native copies (Tribes agents' search) are tribes other than the active one skipped. The explorer's
reveal-triggered recomputation is for the exploring tribe only.

## 11. Luxidoor

No starting tech (score 0), Warrior, standard stars. **Corrected in 05 §1.3:** the capital starts at level 3
with walls and population 0 (4 stars/turn); this is in map load, which the earlier search missed.

## Resolved
- Section 01: Luxidoor's start (§11); `NUM_STEPS` use (explorer, §6).
- Section 02: push callers = level-up Super unit and ruin Super unit (the examining unit itself).
- Section 03a: city-tile definition for actions (§2) and network scope (§10) corrected.
