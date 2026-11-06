# Rule facts 04: turn flow, action generation, diplomacy, observation, game end

Source: Tribes at commit `270e7e5`, read for facts only (ADR-002). Wording, structure and naming here are ours.
Conventions as in sections 01–03b (**[Tribes quirk]**, **[Sim note]**). "Native" = the real game state.

## 1. Game structure

- A **tick** (round) = one turn for every tribe still in the game, in **tribe-id order** 0..n−1. Tribes whose
  result is no longer *incomplete* are skipped. The tick counter starts at 0 and increases after the last tribe
  id's slot, whether or not that tribe played.
- **Native loop vs. Tribes' forward model** [Sim note]: the real game is driven by the game loop below. Tribes'
  agent-side forward model (`advance`) behaves differently: it never increases the tick, and it executes the
  End-turn command. Our simulator follows the game loop; the agent-side behaviour is irrelevant to difftests
  (we replay logged actions on the native state).

### 1.1 Game start (after map load)

1. Map load creates each tribe's capital through "add city" (03b §1: clear view radius 1, on a board copy).
2. Then, in tribe-id order, clear view radius **2** around each tribe's first city (03b §1; under partial obs
   this can meet tribes and draw random techs, using the game RNG).
3. Stars are not set here; see turn start, tick 0.

Actor ids are assigned from a counter that is incremented **before** use, so the first actor id is 1 and a
board cell value of 0 means "no unit".

## 2. One turn of tribe P (game loop)

1. **Turn start** (§3).
2. **Generate actions** (§5).
3. **Ask the agent** for an action. The agent is always asked at least once per turn, even if End turn is the
   only action.
4. If the action is **End turn**: the turn ends (go to 7). The End-turn command itself is **not** executed in
   the game loop; nothing in the rules depends on it.
5. Otherwise execute it, check game end (§8; if over, go to 7), regenerate actions.
6. **Auto end:** if the only remaining action is End turn (no city action, no unit action, and the tribe's
   action list is exactly [End turn]), the turn ends without asking the agent (go to 7). Otherwise go to 3.
   A pending level-up (§5) has no End turn but has actions, so the agent is asked.
7. **Turn end** (§4). If the game is over, the loop stops; no further tribe plays.

[Sim note] Tribes also ends a turn on agent timeout when `TURN_TIME_LIMITED`; it is false at this commit.
A `null` action from the agent is ignored and the agent is asked again.

## 3. Turn start for tribe P

In this order:
1. Clear P's "turn must end" flag.
2. **Income and temples**, for each city C in P's city list order:
   - C **produces** unless a unit of **another tribe** stands on C's tile (diplomacy ignored). If it produces,
     C's production (03a §1; may be negative) is added to a running sum.
   - Every temple of C (C's building-list order) ticks (03a §3). Points it returns go to P's score and C's
     points-worth.
3. **Stars:** if tick = 0, P's stars are **set** to 5 (the income sum is discarded). Otherwise P's stars +=
   max(0, sum). The sum is clamped as a whole: one negative city cancels others' income. Adding stars checks
   the Emperor's Tomb threshold (03a §5).
4. **Unit status reset**, for P's city units (city list order, each city's unit list) then P's tribe-owned
   units: *Pushed* → *Moved* (a unit pushed during another tribe's turn starts as if it already moved);
   any other status → *Fresh*.
5. **Pacifist counter:** only if P has Meditation: counter += 1; if it is now **exactly** 5, Altar of Peace
   becomes *available* (03a §5 quirk).
6. Stars sent this turn := 0; "has declared war this turn" := false.

## 4. Turn end for tribe P

For P's city units (city list order, unit-list order) then tribe-owned units: each unit whose status is
**Fresh** executes the **first** Recover variant, if any is generated (03b §7). Units that did anything this
turn do not heal. Nothing else happens at turn end.

## 5. Action generation (order matters for logs and policy indices)

For the active tribe P, actions are grouped as city actions (per city), unit actions (per unit) and tribe
actions. No actions at all once the game is over.

1. **Cities, in P's city list order.** For each city, the Level-up variants (03a §2) are generated first.
   - If a city has any Level-up variant: that city's level-up variants are the **only** actions of the whole
     turn state: city actions already generated for earlier cities are dropped, no unit or tribe actions
     are generated, and **End turn is unavailable**. Only the first such city in list order is offered.
   - Otherwise the city's actions are, in order: Build, Burn forest, Clear forest, Destroy, Grow forest,
     Gather, Spawn.
2. **Units:** P's city units (city list order, unit-list order), then tribe-owned units. Per unit, in order:
   Upgrade (generated even for *Finished* units); then, unless *Finished*: Attack, Capture, Convert, Disband,
   Examine, Heal others, Make veteran, Move, Recover.
3. **Tribe actions**, in order: Build road (03b §8), Research (03a), Declare war (§6.3), Send stars (§6.4),
   End turn (available iff no level-up is pending).

## 6. Diplomacy

### 6.1 State

An n × n integer **allegiance** matrix A, all 0 at start. It is symmetric in practice (every update adds the
same value to A[i][j] and A[j][i]). Allegiance has **no effect on any rule** except Declare-war legality: units
attack, convert and capture regardless of it. It is fully visible to every tribe (§7).

### 6.2 Operations

**Update(v, i, j):**
- If A[i][j] + v is outside [−60, 60], v is replaced by sign(v) × (60 − |A[i][j]|) (clamps to the bound when v
  and A[i][j] have the same sign; with the action values below an opposite-sign overflow cannot occur).
- A[i][j] += v; A[j][i] += v.
- [Tribes quirk] If i = j the same cell gets 2v, and the clamp was computed for v only, so a diagonal entry
  can pass ±60. Only reachable through the convert quirk (03b §7, which hits (P, P)).

**Consequences(v, i, t)** ("enemies of the target react"): w = v / −2 (Java integer division, truncates toward
zero). For every tribe k in id order with A[k][t] < 0 and k ≠ i: Update(w, k, i). k = t is included if A[t][t] < 0.
The test A[k][t] < 0 is read live, so earlier updates in the same loop can change later tests.

| Trigger | Update | Then Consequences | Consequence step w |
|---|---|---|---|
| Attack (02 §2), on every attack | (−5, attacker, target) | (−5, attacker, target) | +2 |
| Attack that kills, additionally | (−5, attacker, target) | (−5, attacker, target) | +2 |
| City capture (03b §3) | (−30, P, Q) | (−30, P, Q) | +15 |
| Convert (03b §7; quirk: (P, P)) | (−5, P, P) | (−5, P, P) | +2 |
| Declare war on Q | (−30 − A[P][Q], P, Q) → A[P][Q] = −30 | (−30, P, Q) | +15 |
| Send n stars to Q | (+n, P, Q) | (+n, P, Q) | −⌊n/2⌋ (0 for n = 1) |

Village capture has no diplomacy effect.

### 6.3 Declare war

**Legal** (tribe P, target Q): Q ≠ P; A[P][Q] > −30 (strict); P has not declared war this turn.
[Tribes quirk] No check that Q is alive or met: targets include eliminated tribes. Generated for Q in id order.
**Execute:** "declared this turn" := true; P's wars-declared count += 1 (a ranking tie-break, §8.3);
the update and consequences in the table. No score, no other effect.

### 6.4 Send stars

**Generated** (tribe P with stars s > 0): for each Q ≠ P in id order (eliminated tribes included), for each
n = 1 .. min(s, 15) − 1, if P's stars sent this turn < 30 and n < s.
So n ≤ 14 and n < s. [Tribes quirk] The 30-per-turn cap is checked before sending, not against the total:
29 + 14 = 43 stars can be sent in one turn.
**Execute:** P's stars −= n; Q's stars += n (Q's Emperor's Tomb check applies); P's sent-this-turn and
lifetime sent counters += n; **P's score += n**; the update and consequences in the table.

## 7. Observation (what an agent sees)

Agents always receive a filtered copy, in **both** observability modes. Under full obs (this commit's build,
ADR-004) every tile is visible but everything below that is not tile-visibility still applies.

**Tiles.** Not visible to the observer → terrain *fog*; no unit, resource, building, owner or network flag.
Visible → everything, except resources are masked by the observer's tech: crops need Organization, ore needs
Climbing, whales need Fishing (shown as no resource otherwise).

**Other tribes.** Hidden (zero/empty): stars, kills, pacifist counter, tech tree, city list, connected-cities
list, tribes-met list, tribe-owned units, monuments. Their vision grids appear all-visible.
Visible: score, result, capital id, stars sent this turn, declared-war flag, wars-declared and lifetime
stars-sent counters, and the whole allegiance matrix.

**Actors.** All of the observer's own actors. Another tribe's city or unit only if its tile is visible; such a
city shows level, population needed, capital flag, walls, border radius, buildings, but population 0,
production 0 and no unit list; such a unit shows no home city and 0 kills. Visible foreign actors are listed
in their tribe's city list / tribe-owned units of the copy.

[Sim note] Our observation type must expose exactly this set (no more) for agents to be comparable with Tribes
agents, and to keep the "no peeking" rule honest.

## 8. Game end

Checked after every executed action, and before each tick.

### 8.1 Score mode (turn limit 30)

Over when at most one tribe is not *lost*, or when **tick > 30**. The tick check first passes after tick 30
finishes, so a full game lasts 31 ticks (0..30).

### 8.2 Capitals mode (turn limit 50)

The capital list is fixed at game start (each tribe's starting capital's actor id; capture keeps the id).
For each tribe in id order that has not lost: if it owns every capital in the list, it is set to *win* and the
game is over. Also over when **tick > 50**. (No "last tribe standing" rule: the last tribe owns all capitals
anyway, since cities are never destroyed.)

### 8.3 Ranking and result

When over, tribes are ranked; the first gets *win*, every other tribe *loss*. Order:
1. *win* before anything else (only set in capitals mode before ranking);
2. higher score;
3. more techs researched;
4. more cities;
5. higher production: sum of all its cities' production, **ignoring** enemy occupation and the
   non-negative clamp of §3;
6. more wars declared (lifetime);
7. more stars sent (lifetime);
8. random, from an **unseeded** RNG (known caveat; only exact ties).

[Tribes quirk] *Lost* is not a ranking criterion. In score mode an eliminated tribe with a higher score than
every survivor is ranked first and **wins**, even when the game ended because only one tribe survived.

**Elimination** (03b §3 step 8) sets a tribe to *lost* immediately; it is skipped in later turns.

## Resolved
- 02: diplomacy repercussion for attacking (§6.2).
- 03a: when income is collected and temples tick (§3); pacifist counter tick (§3).
- 03b: capture and convert diplomacy (§6.2); win/loss handling (§8).
- 01: declaring-war and allegiance thresholds (§6).
