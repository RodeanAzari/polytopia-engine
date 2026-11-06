# Rule facts 01: constants and static tables

Source: Tribes at commit `270e7e5`, read for facts only (ADR-002). Wording, structure and naming here are ours.
Items marked **[TBD §n]** are pinned down in a later section of this sheet.
Items marked **[Tribes quirk]** differ from the commercial game; we match Tribes, because Tribes is the oracle.

## Game setup

| Fact | Value |
|---|---|
| Game modes | Capitals, Score |
| Turn limit, Score mode | 30 turns |
| Turn limit, Capitals mode | 50 turns (Tribes' experimental cap) |
| Starting stars per tribe | 5 |
| Default square map side by player count (1..7 players) | 11, 14, 16, 18, 20, 22, 24 |
| Terrain types | plain, shallow water, deep water, mountain, village, city, forest (+ "fog" as an observation-only value) |
| Water terrain | shallow water, deep water |
| Grid directions used for direction-based logic | none, left (−1,0), right (+1,0), up (0,−1), down (0,+1) |

## Tribes

12 tribes. Starting tech and starting unit:

| Tribe | Starting tech | Starting unit |
|---|---|---|
| Xin-Xi | Climbing | Warrior |
| Imperius | Organization | Warrior |
| Bardur | Hunting | Warrior |
| Oumaji | Riding | Rider |
| Kickoo | Fishing | Warrior |
| Hoodrick | Archery | Archer |
| Luxidoor | none | Warrior |
| Vengir | Smithery | Swordsman |
| Zebasi | Farming | Warrior |
| Ai-Mo | Meditation | Warrior |
| Quetzali | Shields | Defender |
| Yadakk | Roads | Warrior |

- The starting tech is granted **alone, without its parent** (Hoodrick has Archery but not Hunting; Vengir has
  Smithery but not Mining). The parent can be researched later as normal.
- Starting score = starting tech's tier × 100 (0 for Luxidoor). Luxidoor has no special start (§03b.11).

## Units

Stats: attack, defence, movement, max HP, range, cost (stars), score points, required tech.

| Unit | Atk | Def | Move | HP | Range | Cost | Points | Tech |
|---|---|---|---|---|---|---|---|---|
| Warrior | 2 | 2 | 1 | 10 | 1 | 2 | 10 | — |
| Rider | 2 | 1 | 2 | 10 | 1 | 3 | 15 | Riding |
| Defender | 1 | 3 | 1 | 15 | 1 | 3 | 15 | Shields |
| Swordsman | 3 | 3 | 1 | 15 | 1 | 5 | 25 | Smithery |
| Archer | 2 | 1 | 1 | 10 | 2 | 3 | 15 | Archery |
| Catapult | 4 | 0 | 1 | 10 | 3 | 8 | 40 | Mathematics |
| Knight | 4 | 1 | 3 | 15 | 1 | 8 | 40 | Chivalry |
| Mind Bender | 0 | 1 | 1 | 10 | 1 | 5 | 25 | Philosophy |
| Boat | 1 | 1 | 2 | (carried unit) | 2 | 0 | 0 | Sailing |
| Ship | 2 | 2 | 3 | (carried unit) | 2 | 5 | 0 | Sailing |
| Battleship | 4 | 3 | 3 | (carried unit) | 2 | 15 | 0 | Navigation |
| Super unit (giant) | 5 | 4 | 1 | 40 | 1 | 10 | 50 | — |

- Naval units have no HP of their own; they remember the land unit they carry and take its HP.
  Exact embark/disembark HP handling: Same HP, max HP, kills and veteran flag both ways (§02.3).
- Ship cost 5 and Battleship cost 15 are **upgrade** costs (Boat→Ship needs Sailing, Ship→Battleship needs Navigation).
  Super unit cost 10 is only used for disband refunds.
- **Spawnable in a city:** every unit except Boat, Ship, Battleship, Super unit.
- **Ranged** (for combat retaliation rules): Boat, Ship, Battleship, Archer, Catapult. Rules never read this flag (only a bundled AI does); retaliation is by range (§02.2).
- **Can fortify** (defence bonus eligibility): Warrior, Rider, Archer, Defender, Swordsman, Knight. Only matters in an owned city without walls (§02.2).
- Mind Bender heals others for 4 HP.

### Unit progression and healing

| Fact | Value |
|---|---|
| Kills needed to become veteran | 3 |
| Veteran max-HP increase | +5 |
| Recover: HP gained | +2 |
| Recover: extra HP inside own borders | +2 |

### Turn status of a unit
Fresh → moved / attacked / moved-and-attacked / pushed → finished. Transition rules: §02.1.

## Combat constants

| Fact | Value |
|---|---|
| Attack scaling constant | 4.5 |
| Defence bonus multiplier (terrain / fortify) | 1.5 |
| Defence multiplier inside city walls | 4.0 |
| Explorer steps (`NUM_STEPS`, ruin reward) | 15 — explorer walk steps (§03b.6) |

Damage formula: §02.2.

## Technology

24 technologies in a forest of 5 roots. Tier and parent:

| Tier 1 (root) | Tier 2 children | Tier 3 children |
|---|---|---|
| Climbing | Meditation, Mining | Philosophy (← Meditation), Smithery (← Mining) |
| Fishing | Sailing, Whaling | Navigation (← Sailing), Aquatism (← Whaling) |
| Hunting | Archery, Forestry | Spiritualism (← Archery), Mathematics (← Forestry) |
| Organization | Farming, Shields | Construction (← Farming) |
| Riding | Free Spirit, Roads | Chivalry (← Free Spirit), Trade (← Roads) |

Shields has no tier-3 child.

- **Cost** = 4 + tier × (number of cities the tribe owns).
- **Philosophy discount [Tribes quirk]:** once Philosophy is researched, cost becomes `floor(cost × 0.2)`,
  i.e. an 80 % discount (the commercial game gives 20 % off). Match Tribes.
- **Score** for researching = tier × 100.
- A tech is researchable iff not yet researched and its parent (if any) is researched.
- **"All researched" flag [Tribes quirk]:** only re-evaluated right after researching a *leaf* tech
  (Shields, Aquatism, Chivalry, Construction, Mathematics, Navigation, Smithery, Spiritualism, Trade, Philosophy).
  A tribe whose last research is a non-leaf (possible when a leaf was a starting tech) never gets the flag.
  Consumer: Tower of Wisdom unlock (§03a.5).
- **Free research (ruins):** picks uniformly among currently researchable techs; does nothing if the flag is set
  or none is researchable. Injected in difftests.

## Map resources

| Resource | Gather cost | Effect | Tech |
|---|---|---|---|
| Fish | 2 | +1 population | Fishing |
| Fruit | 2 | +1 population | Organization |
| Animal | 2 | +1 population | Hunting |
| Whales | 0 | +10 stars | Whaling |
| Ore | — | site for Mine | Mining |
| Crops | — | site for Farm | Farming |
| Ruins | — | Examine for a random bonus | — |

## Buildings

| Building | Cost | Bonus | Tech | Terrain | Needs resource | Needs adjacent |
|---|---|---|---|---|---|---|
| Port | 10 | 2 | Sailing | shallow water | — | — |
| Mine | 5 | 2 | Mining | mountain | Ore | — |
| Forge | 5 | 2 | Smithery | plain | — | Mine |
| Farm | 5 | 2 | Farming | plain | Crops | — |
| Windmill | 5 | 1 | Construction | plain | — | Farm |
| Customs house | 5 | 2 | Trade | plain | — | Port |
| Lumber hut | 2 | 1 | Forestry | forest | — | — |
| Sawmill | 5 | 1 | Mathematics | plain | — | Lumber hut |
| Temple | 20 | 1 | Free Spirit | plain | — | — |
| Water temple | 20 | 1 | Aquatism | shallow or deep water | — | — |
| Forest temple | 15 | 1 | Spiritualism | forest | — | — |
| Mountain temple | 20 | 1 | Meditation | mountain | — | — |

- Base buildings: Farm, Mine, Lumber hut. Each has one paired "multiplier" building (Windmill, Forge, Sawmill);
  Port pairs with Customs house. Semantics: §03a.3.
- Port trade distance: 4 (the count includes the destination port). Path rules: §03a.6.

### Temples
| Fact | Value |
|---|---|
| Turns between temple score increases | 3 |
| Score per temple level (levels 1..5) | 100, 50, 50, 50, 150 |

### Monuments
All cost 0 stars, give bonus 3 (population), are placed on plain or shallow water, and are worth 400 points.
Each starts *unavailable*, becomes *available* when its condition is met, and can be built once (*built*).

| Monument | Tech gate | Unlock parameter |
|---|---|---|
| Altar of Peace | Meditation | 5 turns (of peace) |
| Emperor's Tomb | Trade | 100 stars |
| Eye of God | Navigation | all tiles revealed |
| Gate of Power | — | 10 kills |
| Grand Bazaar | Roads | 5 cities |
| Park of Fortune | — | a city reaching level 5 |
| Tower of Wisdom | Philosophy | "all researched" flag |

Exact unlock conditions: §03a.5.

## City level-up rewards

On reaching a level, the tribe picks one of two rewards:

| New level | Options |
|---|---|
| 2 | Workshop (+1 production), Explorer |
| 3 | City wall, Resources (+5 stars) |
| 4 | Population growth (+3 population), Border growth (+1 tile radius) |
| 5+ | Park (+250 points), Super unit |

- Score for the level-up reward = 50 − 5 × (reward level), i.e. 40 / 35 / 30 / 25 for levels 2 / 3 / 4 / 5.
  (A level-1 branch exists in code returning 100; no reward has level 1, so it is unreachable.)
- Level 5+ rewards (either one) unlock Park of Fortune (§03a.2).

## Ruins (Examine)

Five outcomes, drawn uniformly: Super unit, Research (free tech), Population growth (+3), Explorer, Resources (+10 stars).
In difftests the outcome is **injected** from the Tribes log (ADR-002 §3).

## Cities, scoring, misc.

| Fact | Value |
|---|---|
| Capital production bonus | +1 star/turn |
| Population needed / growth rules | §03a.1 |
| Score per owned tile in borders | 20 |
| Score per city centre | 100 |
| Score per population | 5 |
| Score per tile newly revealed (any reveal, own vision grid) | 5 |
| Vision cleared around a unit after an explorer step | radius 1 |
| Vision cleared around the first city | radius 2 |
| Vision cleared around a new city | radius 1 |
| Border expansion step | 1 tile |
| Road cost | 2 stars |
| Clear forest | +2 stars (Forestry) |
| Grow forest | 5 stars (Spiritualism) |
| Burn forest | 5 stars (Chivalry) |
| Destroy building | requires Construction |
| Disband unit | requires Free Spirit |
| Climb mountains | requires Climbing |

## Diplomacy (post-paper extension in this commit)

| Fact | Value |
|---|---|
| Allegiance maximum | 60 |
| Allegiance change when attacked | −5 |
| … when a city is captured | −30 |
| … when a unit is converted | −5 |
| Minimum stars per Send Stars | 15 |

Declaring war and allegiance thresholds: **[TBD §diplomacy]**.
