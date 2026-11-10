# Agent facts 05: Rule Based (and Random)

Facts-only sheet (ADR-002 amendment). No forward model, no budget. All state reads are from the agent's
observation (04 §7); "my" = the active tribe; "stars", "production" are mine unless stated.
- **production** = raw sum of my cities' production (as in 01 §4.1).
- **needed(c)** = c.level + 1 − c.production (for a city c).
- **visible(t)** = my vision grid at tile t (all-true under full obs).
- **foreign unit** = a unit whose tribe ≠ mine (any tribe, whatever the diplomatic state).
- **allegiance(a, b)** = entry [a][b] of the allegiance matrix (04 §6); negative = hostile.
- ATK, DEF, RANGE are the unit type's base values (01); HP is current HP.
- **dist** = Chebyshev distance. **Ring(p, r)** = tiles within dist r of p, **excluding p**, on the map,
  enumerated x-major then y ascending. Several rules below return on the **first** match in that order.

## 1. Choice
Score every legal action (§2, integers). Take the highest score present, down to −1; choose uniformly among
the actions with that score. Actions scoring below −1 (Disband, Destroy) are never chosen. End Turn scores −1,
so it is chosen only when nothing scores ≥ 0. One RNG draw per call.

## 2. Scores by action type
Fixed: Capture 5 · Examine 5 · Make veteran 5 · Disband −2 · Destroy −2 · End Turn −1 · any type not listed
here (e.g. Declare war) 0.

**Research tech**: Organization, Climbing, Mining, Fishing, Hunting, Whaling → 5; Farming, Aquatism, Riding,
Forestry, Archery, Sailing, Shields, Chivalry → 4; every other tech → 3.

**Level up**: Border growth, Workshop, Resources, Super unit → 5; Explorer, City wall, Pop growth, Park → 0.

**Resource gathering** (city c): clamp(5 − needed(c), 0, 5).

**Build** (city c, building b): if stars < 8 → 0. Otherwise base = **1** for Port, Mine, Forge, Farm,
Windmill, Customs house, Lumber hut, Sawmill, all monuments, Temple, Water temple, Mountain temple
(quirk: the intended tiers 4/3/5/1 fall through to 1); base = **0** for Forest temple. Score =
clamp(base + 5 − needed(c), 0, 5).

**Spawn** (city c, unit type u): E = number of visible tiles of c's territory holding a foreign unit.
Rider, Archer, Catapult: E > 0 → 0, else 3. Swordsman, Knight: E > 0 → 4, else 3. Mind Bender: E > 0 → 0,
else 2. Warrior: 3. Defender: E > 0 → 5, else 3.

**Upgrade** (boat or ship): if production > 3 and stars > 6 → 3, else 0. (Quirk: a stricter branch meant for
ships tests whether the unit *is* a battleship, which a unit being upgraded never is, so it never applies.)

**Clear forest**: stars = 0 → 3, else 0.
**Burn forest** (city c): if stars > 5: 1 + (number of Farms among c's buildings if that number < 2, else 0),
capped at 5; else 0.
**Grow forest** (city c): count forest tiles and Lumber huts in c's territory; both < 2 → 4, else 2.

**Build road** (tile p): for each n in Ring(p, 1), first match returns:
- n in **my** city's borders: if n is that city's centre: 3 if production > 5 and stars > 4, otherwise no
  match (continue); if n is not the centre: 3 if n has a road, otherwise continue;
- n not in my borders (foreign or none) and n has a road: 4 if production > 5 and stars > 4, else 3.
No match → 0.

**Send stars** (to tribe T, n stars): s = my stars, a = allegiance(me, T).
s > 60 → 4. Else s > 30 → 3 if a < 0 and a + n > 0, else 2. Else 4 if −5 ≤ a < 0 and n ≤ 5. Else −1.

**Recover** (unit u): scan visible tiles in x-major order; for the first foreign unit e with dist(e, u) ≤
e.RANGE: return 3 if e.HP < u.HP else 1. None → 4.

**Heal others** (unit u): over u's heal targets t with t.HP < t.maxHP: for each tile in Ring(u, **t.RANGE**)
(quirk: the target's range, around the healer) holding a unit of a tribe ≠ t's tribe with HP > t.HP, count 1.
Score = min(count, 5).

**Attack** (attacker A, defender D, a = allegiance(A's tribe, D's tribe)); h(x, y, z) = x if a < 0, y if
0 ≤ a < 15, z otherwise:
- A not ranged: if A.HP ≥ D.HP: A.ATK > D.DEF → h(5, 2, 0), else h(1, 0, 0). If A.HP < D.HP: A.ATK > D.DEF →
  h(1, 0, 0), else 0.
- A ranged: in-their-range = dist ≤ D.RANGE; in-my-range = dist ≤ A.RANGE.
  in-their-range and D.ATK > A.DEF → 0; else in-my-range and not in-their-range → h(5, 2, 0); else
  D.DEF < A.ATK and A.HP ≥ D.HP → h(4, 1, 0); else 0.

**Move** (unit u from q to destination m), three stages, first return wins:
1. Visible tiles x-major; for each foreign unit e: in-range := dist(e, u) ≤ e.RANGE.
   If e.DEF < u.ATK and u.HP ≥ e.HP: return 3 if dist(m, e) < dist(q, e) (else keep scanning).
   Otherwise: return 4 if dist(m, e) > dist(q, e) and in-range (else keep scanning).
2. For each visible n in Ring(q, u.RANGE): if n is in a foreign city's borders and dist(m, centre) <
   dist(q, centre) → 4; else if n's terrain is Village and dist(m, n) < dist(q, n) → 5.
   (Both checks per tile, city first.)
3. If any tile of Ring(m, 1) is visible → 3 (always, under full obs). Else 0.

**Convert** (Mind Bender u, target unit v): intended h-style scores by v's type (Battleship, Super unit,
Swordsman, Knight, Catapult: 5 / 2; Mind Bender, Boat, Ship, Warrior: 4 / 1; else 0). **Tribes reads the
allegiance matrix with unit ids instead of tribe ids**, which is out of bounds (it would throw) whenever an id
≥ number of tribes. Unreachable in practice: Rule Based only gets Mind Benders by spawning one, and a Warrior
spawn (3) always outranks a Mind Bender spawn (≤ 2) when both are legal; 0 crashes in 424 tournament games so
far. **Our version uses the tribe ids** (the evident intent) and counts how often it is reached; any nonzero
count is reported as a deviation.

## 3. Random agent
Uniform over all legal actions (End Turn, Destroy, Disband included), one draw per call.

## 4. Notes for our implementation
- Per-type scorers on the observation; no forward model. Ring enumeration order must be x-major / y ascending.
- Unit tests per scorer from hand-built states, including the quirks: build fall-through, upgrade never
  stricter, road "centre without production continues", heal ring radius = target's range, move stage order.
