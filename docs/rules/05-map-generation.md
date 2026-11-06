# Rule facts 05: map generation and map load

Source: Tribes at commit `270e7e5`, read for facts only (ADR-002). Wording, structure and naming here are ours.
Conventions as in sections 01–04 (**[Tribes quirk]**, **[Sim note]**).

**Two precision levels in this section:**
- **Map load** (§1) turns a level description into the initial state. It is deterministic apart from the
  clear-view RNG, and must match **bit-exactly**: difftests inject Tribes' generated level and compare from there.
- **Generation** (§2–§3) only needs the **same distribution** (ADR-002 §3). Java's RNG call order is irrelevant.
  Quirks that change the distribution are listed, and we keep them because Tribes' maps are the reference.

Probe-verified facts (500 seeds each, 2 and 4 tribes, throwaway probe, 2026-11-06) are marked **[probed]**.

## 1. Map load (level → initial state)

### 1.1 Level description

An n×n grid. Row index = **x**, column index = **y**. Each cell is `terrain[:extra]`:
- terrain char: `.` plain, `s` shallow water, `d` deep water, `m` mountain, `v` village, `c` city, `f` forest;
- extra on a non-city cell: resource char `h` fish, `f` fruit, `a` animal, `w` whales, `o` ore, `c` crops,
  `r` ruins; any other char (e.g. a space) = no resource;
- extra on a city cell: the owning tribe's **type key** (decimal): Xin-Xi 0, Imperius 1, Bardur 2, Oumaji 3,
  Kickoo 4, Hoodrick 5, Luxidoor 6, Vengir 7, Zebasi 8, Ai-Mo 9, Quetzali 10, Yadakk 11. City cells get no resource.

### 1.2 Tribes, ids and seats

- One tribe per city cell. **Tribe id = rank of its city in row-major order** (x, then y). Turn order (04 §1)
  is tribe-id order, so it depends on where the capitals landed, not on the requested tribe order [probed:
  ids differ from input order in 250/500 two-tribe maps, 491/500 four-tribe].
- Seat binding for generated maps: the agent at position i of the player list controls the tribe whose
  **type** = i-th requested type, wherever its id ended up.
- Each tribe starts with its starting tech and its score from 01 (tech tier × 100).
- **Duplicate tribe types are unsupported:** both cities attach to the higher id, and map load crashes [probed].
  Our simulator rejects duplicate types at setup.

### 1.3 Per-city setup (row-major order of city cells)

The board is filled cell by cell in row-major order. For a city cell, before its terrain is written:
1. Create the city: level 1, population 0, population needed 2, border radius 1, **capital**, no walls.
2. **Luxidoor** (correction to 03b §11): the city levels up twice (03a §1 rule), then population := 0 and walls := on.
   Result: level 3, population 0, needed 4, walls, border radius still 1. Production 3 + 1 (capital) = **4 stars/turn**
   [probed]. No level-up rewards or score for these levels.
3. "Add city" (03b §1): next actor id; capital id; tribe's city list; clear view radius **1** using a **copy of the
   board as loaded so far**, so cells later in row-major order have no terrain yet and later capitals have no
   unit yet; the city tile gets the trade-network flag.
4. Tribe score += 100 (city centre).
5. Starting unit (01 table) on the city tile: next actor id, 0 kills, not veteran, belongs to the city (added to its
   unit list). Score += the unit's points.
6. Assign city tiles at radius 1: the neighbourhood, then the centre; each tile still unowned → owned by this city,
   tribe score += 20 and city points-worth += 20. Overlap: the earlier city in row-major order keeps the tile.
7. Write terrain = city.

Non-city cells: write terrain, then the resource if the extra char is a resource.

Resulting starting score [probed]: Xin-Xi 390 = 100 (tech) + 100 (centre) + 9 × 20 (tiles) + 10 (Warrior);
Luxidoor 290.

[Sim note] The "extra cities → villages" branch in Tribes' loader is unreachable (one tribe per city).

After load: clear view radius 2 per tribe in id order (04 §1.1).

## 2. Generation parameters

| Parameter | Value |
|---|---|
| Map side n | 01 table by player count (11, 14, 16, …) |
| Initial land fraction | 0.5 |
| Smoothing passes | 3 |
| Relief | 4 → land threshold (0.5 + relief) / 9 = 0.5 |
| Border-expansion factor for resources | 1/3 |
| Ruins target R | round(n² / 40), half-up (11 → 3, 14 → 5, 16 → 6) |
| Water-ruin cap W | round(R / 3) |

Neighbourhoods here: **ring(c, k)** = cells at Chebyshev distance exactly k (clipped to the map);
**cross(c)** = the 4 orthogonal neighbours. Distances are Chebyshev.

### 2.1 Terrain and resource probabilities

Probability = base × tribe multiplier of the cell's **owner** (§3 step 4). Capital cells have no owner (multiplier 1;
irrelevant, as nothing is generated on them).

| Feature | Base | Multipliers ≠ 1 |
|---|---|---|
| Forest | 0.4 | Oumaji 0.1, Hoodrick 1.5, Zebasi 0.5, Yadakk 0.5 |
| Mountain | 0.15 | Xin-Xi 1.5, Ai-Mo 1.5, Kickoo 0.5, Hoodrick 0.5, Zebasi 0.5, Yadakk 0.5 |
| Fruit | 0.5 | Bardur 1.5, Yadakk 1.5, Luxidoor 2, Quetzali 2, Zebasi 0.5, Vengir 0.1 |
| Crops | 0.5 | Bardur 0.1, Ai-Mo 0.1, Quetzali 0.1 |
| Animal | 0.5 | Bardur 2, Luxidoor 0.5, Vengir 0.1 |
| Fish | 0.5 | Kickoo 1.5, Vengir 0.1 |
| Whales | 0.4 | none |
| Ore | 0.5 | Vengir 2, Quetzali 0.1 |

[Tribes quirk] The table also has a "water" entry (Kickoo 0.4) that the generator never reads: Kickoo gets no
extra water.

## 3. Generation steps (in order)

1. **Random land.** Start all deep water. Convert ⌈n² × 0.5⌉ distinct uniformly random cells to plain.
2. **Smoothing,** 3 passes. In each pass, a cell qualifies if (deep-water cells in its 3×3 block, clipped to the map)
   / (cells in that block) ≤ 0.5. After the pass, the land is every cell that qualified in **this or any earlier
   pass**, and everything else is deep water. [Tribes quirk] The qualifying set is never cleared between passes,
   so land only grows after pass 1.
3. **Capitals,** one per requested tribe, in request order. Candidates: plain cells with x and y in [2, n−3].
   Score of a candidate = min(n, distance to each capital already placed). Pick uniformly among the candidates with
   the maximum score (the first capital is uniform over all candidates). A chosen cell can be chosen again only if
   every candidate scores 0. [Sim note] With no candidate at all, Tribes crashes later; our generator rejects the seed.
   The capital cells become cities of their tribes.
4. **Territory (cell owner).** Each tribe grows a region from its capital. Each round, tribes in request order
   take one step if they still have active cells: pick a uniform active cell of their own; among its ring-1 cells
   not yet claimed, prefer non-deep-water ones (deep water only if no others); claim a uniform one (it becomes
   active and owned by this tribe). If there are no unclaimed neighbours, the picked cell is deactivated (that step
   claims nothing). Stop when every cell is claimed.
5. **Forest and mountain,** per plain cell: one uniform draw u. Forest if u < p_forest; else mountain if
   u > 1 − p_mountain (owner-adjusted, §2.1).
6. **Placement map** M, per cell: −1 if deep water, mountain or on the map edge (x or y ∈ {0, n−1}); else 0.
   (Computed **before** step 7.)
7. **Coast.** For each deep-water cell in row-major order: its first cross neighbour (order: y−1, y+1, x−1, x+1)
   that is plain, forest or mountain **becomes shallow water**. [Tribes quirk] The land tile is converted, not the
   water tile: coasts erode by about one tile and the deep cell stays deep. Converted tiles keep their M value.
8. **Capital marks.** For each capital: M = 3 on it; ring 1 → max(M, 2); ring 2 → max(M, 1). This lifts nearby
   water, mountains and edge tiles out of −1.
9. **Villages.** While some cell has M = 0: take the **first** such cell in row-major order, set M = 3, ring 1 →
   max(M, 2), ring 2 → max(M, 1). Deterministic given steps 1–8.
10. **Resources and village terrain,** per cell. "Roll p" = with probability p if M = 2, p/3 if M = 1, never otherwise:
    - plain: M = 3 → village terrain. Else roll fruit · (1 − crops/2) → fruit; else roll crops · (1 − fruit/2) →
      crops (fruit and crops are the owner-adjusted probabilities; two independent rolls);
    - forest: M = 3 → village terrain (no resource). Else roll animal → animal;
    - shallow water: roll fish → fish; deep water: roll whales → whales; mountain: roll ore → ore.
    [Tribes quirk] A tile converted to shallow in step 7 that got M = 3 in step 9 yields **no village**, and it
    still suppressed villages around it.
11. **Ruins** until R have been placed. Each attempt: pick uniformly among cells with M ∈ {−1, 0, 1} (recomputed per
    attempt). Reject shallow water; reject deep water once W water ruins are placed. Otherwise set the resource to
    ruins (**overwriting** any resource: whales, ore, fruit, crops) and count it (a water ruin also counts toward W).
    [Tribes quirks]
    - The same cell can be picked again and is counted again, so a map can have fewer than R distinct ruins
      [probed: 35/500 maps short at n = 11, min 1 of 3; 110/500 at n = 16, min 4 of 6].
    - The intended "no adjacent ruins" marking is applied around the cell whose **index equals the picked cell's
      M value** (−1, 0 or 1), not around the ruin. Its only effect: after a pick, a few cells in the top-left corner
      (cell indices 0, 1, 2, n, n+1, n+2, as applicable) get M ≥ 2 and stop being ruin candidates.
    - Ruins can land on mountains, map-edge land and distant deep water (all M = −1).
12. **Starting-resource guarantee,** per capital in request order:
    - Imperius: while fewer than 2 ring-1 cells have fruit, pick a uniform ring-1 cell, set it to plain with fruit
      (overwriting terrain and resource), then turn its deep-water cross neighbours to shallow.
    - Bardur: same with forest + animal.
    - No other tribe has a guarantee.

Output: one level (§1.1), loaded as in §1.

## 4. Verification plan

- **Map load:** difftested directly. Tribes logs the generated level; we load it and compare the initial state
  (ids, scores, city fields, units, ownership, vision) before the first action.
- **Generator:** statistical comparison on ≥ 1000 seeds per player count against Tribes' generator: land/shallow/deep
  fractions [probed reference: n = 11: shallow 0.180, deep 0.312, other 0.508; n = 16: 0.183, 0.348, 0.470],
  village count, resource counts per type, distinct-ruin count distribution, capital pairwise distance.
  Two-sample tests with a stated threshold; exact protocol fixed in the evaluation ADR.
