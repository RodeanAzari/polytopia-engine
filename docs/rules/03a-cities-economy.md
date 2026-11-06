# Rule facts 03a: cities, buildings, economy, monuments, trade network

Source: Tribes at commit `270e7e5`, read for facts only (ADR-002). Wording, structure and naming here are ours.
Conventions as in sections 01–02 (**[Tribes quirk]**, **[Sim note]**, neighbourhood order x-major then y).
"City tiles" = tiles owned by that city **within radius 1 (level < 4) or 2 (level ≥ 4)** — not the full borders (03b §2). Constants are in section 01.
Still open, in 03b: capture, ruins (Examine), explorer walk, border growth, road building, unit-to-city
assignment when a city is full, port build/destroy board effects, Luxidoor's start.

## 1. City state and growth

A city has: level (starts 1), population (starts 0), population needed (starts 2), production bonus
(starts 0), capital flag, walls flag, border radius (starts 1), points worth, unit list, building list.

- **Add population v:** if population + v < −level, v is clamped so population becomes −level (the floor).
  The owning tribe's score and the city's points-worth change by 5 × v (clamped v).
  [Sim note] The tribe passed in is not always the current owner (section 6).
- **Can level up** iff population ≥ population needed.
- **Level up:** level += 1; population −= population needed (excess carries over); population needed = level + 1.
- **Production** (stars per turn): if population ≥ 0, level + production bonus + (1 if capital); else **the
  negative population itself** [Tribes quirk: a city below zero population yields negative income].
  When income is collected: 04 §3.
- Production bonus never goes below 0 (clamped on every change).
- Unit capacity: a city can hold level + 1 units.

## 2. Level-up action

Legal iff the city can level up and the chosen reward is valid for the city's **current** (pre-level-up) level:
level 1 → Workshop / Explorer; 2 → City wall / Resources; 3 → Population growth / Border growth;
≥ 4 → Park / Super unit (offered again at every later level).

Execution order:
1. If the reward is one of the level ≥ 4 pair, Park of Fortune becomes available (if unavailable).
2. Tribe score and city points-worth += reward points (section 01 table).
3. City levels up (section 1).
4. Apply the reward: Workshop → production bonus +1; Explorer → explorer walk from the city (03b; uses RNG);
   City wall → walls on; Resources → +5 stars; Population growth → +3 population (may enable another level-up);
   Border growth → border expansion (03b); Park → +250 score and points-worth; Super unit → if a unit stands on
   the city tile it is pushed (section 02 §4; killed if it can't be), then a new Super unit is created on the city
   tile (starts Finished). No score is added for that Super unit.

## 3. Buildings

### Legality of Build (city C, type B, tile X)
Generated for every tile of C that has no building, for every building type. Then all of:
- stars ≥ cost (when cost > 0); B's tech gate researched (Gate of Power and Park of Fortune have none);
- X's terrain is in B's terrain set (section 01);
- Farm needs the crops resource on X, Mine needs ore (section 01 resource constraint);
- Windmill / Forge / Sawmill / Customs house need an adjacent Farm / Mine / Lumber hut / Port (radius 1,
  **any owner**);
- Windmill, Forge, Sawmill, Customs house are **unique per city** (no other of that type on C's tiles);
- Monuments: the tribe's status for B is *available*.
- Units on X are **not** checked (own or enemy). [Tribes quirk]

### Executing Build
1. Stars −= cost. X's building = B; X's resource cleared.
2. The building is added to C and its effects applied (below).
3. Port: board port bookkeeping (03b). Monument: tribe status → *built*.
4. Lumber hut: X's terrain becomes **plain** [Tribes quirk] (after effects were applied).

### Building effects (applied with sign +1 on build, −1 on destroy)
- **Base** (Farm, Mine, Lumber hut): C gains +bonus population. Then for each neighbour of X (radius 1) holding the
  paired multiplier building (Windmill / Forge / Sawmill): that multiplier's bonus is added as population to
  the multiplier's city.
- **Multiplier** (Windmill, Forge, Sawmill): for each neighbour holding the paired base building: the
  multiplier's own bonus is added as population to the *base building's* city.
- **Port**: C gains +bonus population; then for each adjacent Customs house, the port's bonus is added as
  **production bonus** to the customs house's city.
- **Customs house**: for each adjacent Port, the customs house's bonus is added as production bonus to the port's city.
- Neighbour handling, both bonus kinds [Tribes quirks]:
  - the receiving city is the neighbour tile's owning city; if that city belongs to another tribe, **the whole loop
    stops** (remaining neighbours are skipped);
  - the "receiving city" variable is not reset between neighbours: after a neighbour in another own city, later
    neighbours in C itself still credit that other city;
  - a neighbour tile whose owner has no building record at that tile is skipped.
- **Temples**: C gains +bonus population. Score: on build +100 (level-1 temple points); on destroy the
  temple's accumulated points are **added**, not subtracted [Tribes quirk].
  Temple levelling: a temple has level 1..5 and a 3-turn counter; each tick below level 5 the counter drops and
  at 0 the temple levels up, scores that level's points and the counter resets. When ticks happen: at the start of the owner's turn (04 §3).
- **Monuments**: C gains +3 population; tribe score ±400.

### Destroy (city C, tile X)
Legal iff X has a building, C has a building record at X, X is C's tile, and the tribe has Construction.
Any building type, including temples and monuments. Clears X's building, port bookkeeping if a port, removes
the effects (sign −1). No refund; resource not restored.

## 4. Other city actions (all on C's own tiles; generated by scanning C's tiles)

| Action | Legal iff | Effect |
|---|---|---|
| Clear forest | forest; Forestry | terrain → plain; +stars (section 01). Resource kept. |
| Burn forest | forest; stars ≥ cost; Chivalry | terrain → plain, resource → crops; −stars |
| Grow forest | plain; stars ≥ cost; Spiritualism | terrain → forest, resource cleared; −stars |
| Gather resource R | X has resource R; stars ≥ R's cost; Animal → Hunting, Fish → Fishing, Whales → Whaling, Fruit → Organization | resource cleared; −cost; Fish/Animal/Fruit: +bonus population to C; Whales: +bonus stars |
| Spawn unit U | U spawnable; stars ≥ cost; C below capacity; no unit on C's tile; U's tech researched | new U on C's tile (Finished), in C's unit list; −cost; score + U's points |

- None of these check units on X. Gather's legality check itself does not check that X is C's tile; only
  generation does (logged actions are always generated ones).
- Crops and ore have no gather action (they are consumed by Farm/Mine).

## 5. Monument availability

Each monument goes *unavailable → available* once; Build moves it to *built*.

| Monument | Becomes available when |
|---|---|
| Altar of Peace | pacifist counter reaches **exactly** 5 (it only counts up while the tribe has Meditation; any attack resets it). Counter tick: at the start of the tribe's turn (04 §3). [Tribes quirk] This sets *available* even if already *built*, so it can be rebuilt after another 5 peaceful turns. |
| Emperor's Tomb | after gaining stars, the tribe holds ≥ 100 (checked only when stars are added) |
| Eye of God | after the tribe clears fog, **every** tile is revealed. Never in full-observability games. |
| Gate of Power | tribe kill count reaches ≥ 10 |
| Grand Bazaar | ≥ 5 cities connected to the capital (section 6) |
| Park of Fortune | any city picks a level ≥ 4 reward (section 2) |
| Tower of Wisdom | "all researched" flag set (section 01 quirk) |

The tech gate (section 01) applies at build time, not at unlock time.

## 6. Trade network (roads, ports, connection to capital)

The board keeps a "network" flag per tile (set by roads, city centres, ports; 03b). Recomputation happens on
network changes and when fog clearing reveals a road or water tile; in the real game it is done for **every
tribe** (only Tribes' search copies skip non-active tribes; corrected in 03b §10). A tribe without its capital loses all connections.

For tribe P owning its capital:
1. **Connected tiles**: own city centres and own ports (inside own borders) with the network flag; plus road
   tiles (network flag, not water, not city) that are outside all borders or inside P's borders.
2. **Navigable water**: water tiles visible to P and outside other tribes' borders.
3. **Port links**: for every pair of P's ports, if a water path exists over navigable tiles with at most
   **4 steps** (8-directional, each step 1, the destination port counts as a step), the two ports are linked
   both ways (a link is a single step).
4. For every non-capital city of P: connected iff a path exists from the capital over connected tiles and
   port links (8-directional).
5. Changes vs. the stored connected set:
   - newly connected city: +1 population to that city; lost city: −1 population to that city (the −1 is applied
     with P as the tribe even if P no longer owns it [Tribes quirk]: P's score drops, not the new owner's);
   - the capital gains (added − lost) population;
   - if ≥ 5 cities are connected, Grand Bazaar becomes available.
   Cities P no longer owns are dropped (counted as lost).

[Sim note] Path existence here uses Tribes' A* (section 02). For step 4 the neighbours don't depend on
cost-so-far, so existence is order-independent and a plain BFS gives the same answer. For step 3 they do
(≤ 4 steps), but with unit 8-directional steps and the Chebyshev heuristic (consistent) the A* finds the shortest
path, so "shortest water path ≤ 4" is equivalent. Both are to be confirmed by the difftest.

## Resolved from section 01
- Port / customs semantics (population vs. production), port trade distance, monument unlock conditions
  (Eye of God, Tower of Wisdom), Park of Fortune trigger, population growth rules, consumer of the
  "all researched" flag (Tower of Wisdom).
