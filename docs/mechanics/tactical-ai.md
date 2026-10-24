# Tactical AI (computer player in battle)

How the computer player (player slot 0) plays its turns on the tactical
map: in which order its units act, what the scripted unit orders
(`Units On Map.OrderID`) do, how a unit picks its target and its firing
position, when it moves, when it fires, and when it calls automated units
(air strikes, drops, medics).

Prerequisites: [data-model.md](data-model.md) (tables and columns),
[tactical-movement.md](tactical-movement.md) (tile evaluation, path search
and its AI variant, cutting a path to the available AP, group moves),
[tactical-combat.md](tactical-combat.md) (shot legality, autofire, damage,
reaction fire) and [battle-flow.md](battle-flow.md) (turn counter `T`,
which modes have a computer player, reinforcements, objectives).

Status: the rules below come from studying the program's behavior and are
written in our own words. They are detailed enough to reimplement, but
they have not been checked against recorded games. Items marked *(?)* are
not fully confirmed. Several rules look like slips in the original; they
are described as the program behaves and flagged as such.

## 0. Terms

| Term | Meaning |
|---|---|
| AI unit | a unit with `playerOwner` = 0 |
| enemy unit | a unit the AI regards as hostile: every unit of a human slot, plus human-race convoy units owned by the AI (unit–unit relation of tactical-combat.md 1.1 seen from player 0) |
| mobile | `Unit Types.Movement` is neither 0 (none, ground) nor 8 (none, water); towers and platforms are *immobile* |
| alien | `Unit Types.Race` ≠ 0 |
| AP | available AP = `max(0, currentAP − negAP)` |
| shot cost | `actionPointsShoot` of the unit's rules row |
| stopping tile | a tile the unit may end its move on ("enterable" in tactical-movement.md 1); the opposite is a fly-over tile |
| path | a path from the AI path search (section 4.1); its length in **steps** is the number of tiles entered, its **cost** the sum of the AP step costs |
| cut to AP | shorten a path to what the unit can pay, with the rule of tactical-movement.md 2 ("When AP runs short") |
| legal shot from tile X | the shot check of tactical-combat.md 1 (range, minimum range, line of fire) made as if the shooter stood on X *(?: whether the AP check is part of it when X is not the unit's own tile is not confirmed; it does not matter for the outcome because the AP rules below are applied separately)* |
| counter-attack battle | the battle phase in which the computer player attacks a province it lost (battle-flow.md 4.3) |
| strategic AI info | the computer player's colony data: `Continent Info.AiAutoUnitChanceCall`, `AiAutoUnitRes` and the colony's `AI automated available` list; present in campaign and counter-attack battles, absent in skirmish |

All random numbers are drawn from the same C library generator the server
uses for combat (`rand() mod n`), so AI decisions and shot results share one
random sequence (section 7).

## 1. The AI turn

### 1.1 Who runs it and how it talks to the server

- The computer player is not a network client. It is an object that lives
  inside the server's tactical phase and is created when the battle starts
  (one per battle; its automated-unit budget lasts for the whole battle,
  section 6). Campaign, counter-attack and skirmish battles create one; the
  counter-attack version is created with a "counter-attack" flag that
  changes several rules below. Death match has none.
- When slot 0 becomes the current player (battle-flow.md 1.3: AP refill
  first), the server sends itself a `CAItacticMsg`. Handling it runs the
  computer player for **at most 4 seconds of real time**; if it is not
  finished, the server posts another `CAItacticMsg` to itself and continues
  where it stopped. This only keeps the program responsive. The decisions
  do not depend on where a time slice ends. OpenDay can run the whole turn
  in one go.
- The computer player does **not** send `CRequestUnitMoveMsg` or
  `CRequestUnitShootMsg`. It calls the server's own move and shot
  resolution directly (the same code that handles those requests, with all
  checks: path re-check, reaction fire, mines, hit roll, damage, autofire).
  The results are broadcast to the clients in the same tactical
  multi-messages a human's actions produce.
- After every step of the AI the server's "battle over" check is
  consulted; if the battle is decided, the AI turn stops at once.
- When the AI is done, the server performs its end-of-AI-turn handling and
  posts a `CRequestEndTactTurnMsg` for slot 0, i.e. the AI ends its turn
  with the same request a human uses. (Reinforcements are placed at that
  point, battle-flow.md 4.1.)

### 1.2 Phases of one AI turn

The turn is a fixed sequence of phases. Each phase works through a list of
per-unit *actions*; an action is driven to completion before the next one
starts (one unit at a time, never interleaved).

| # | Phase | Details |
|---|---|---|
| 0 | Order upkeep | 2.2: probability roll for every unit, order expiry, then the **order list** is built: every AI unit that is mobile, has an `OrderID` ≠ 0 and **AP > 4** gets the action for its order. |
| 1 | – | no effect |
| 2 | Automated unit call | 6.1, first chance |
| 3 | Emergency drop | 6.3 |
| 4 | Build the **stationary-fire list** | every AI unit that is mobile, alien and has AP > 4, followed by every AI unit that is immobile (any race, no AP condition). Sorted, 1.3. |
| 5 | Stationary fire | each unit of the list fires from where it stands, 5.1 |
| 6 | Scheduled automated units | every `Automated Units` row of the scenario whose `TurnArrival` equals the turn counter `T` is called in now (no budget, no prerequisite check), then dropped from the list, 6.2 |
| 7 | Automated unit call | 6.1, second chance |
| 8 | Build the **free list** | every AI unit that is mobile, alien and has `OrderID` = 0 (any AP). Sorted, 1.3. |
| 9 | Free units act | only if at least one enemy unit exists, or in a counter-attack battle: each unit of the free list runs the hunting behavior, 2.8 |
| 10 | Goto | all Goto (1) actions of the order list are run (they only register a destination), then all those units move as one group with one shot of AP kept back, 4.4 |
| 11 | Goto Fast | the same for Goto Fast (2), without keeping AP back |
| 12 | Other orders | the remaining actions of the order list in list order: Escort (3), Defend (4), Patrol (5), Attack (6) |
| 13 | End | the AI turn is over (1.1) |

Consequences worth keeping:

- Every armed alien unit first shoots from its standing position (phase 5)
  and only then moves. A Goto unit that used its AP in phase 5 may not move.
- A unit whose order was dropped in phase 0 already hunts freely in phase 9
  of the same turn. A unit whose order ends during phases 10–12 (last
  waypoint reached, protected object gone, …) does nothing more this turn.
- Units with an order but AP ≤ 4 at phase 0 do nothing in phases 10–12.
- Immobile AI units (towers) only ever take part in phase 5 (and in
  reaction fire, which the server handles on its own).
- Human-race units owned by the AI (e.g. convoys) take part only through
  their orders, never in phases 4–5 or 8–9.

### 1.3 Order inside the lists

The lists start in map list order (the order of the units in the battle's
unit list: `Units On Map` load order, reinforcements appended) and are then
sorted with a stable insertion sort: each unit is inserted before the first
unit already in the sorted list for which "a goes before b" holds.

- **Order list:** a goes before b if a's `OrderID` < b's. So Goto, Goto
  Fast, Escort, Defend, Patrol, Attack; equal orders keep map order.
- **Stationary-fire list:** a goes before b if
  1. a is immobile, otherwise
  2. not if b is immobile; a goes first if a's `damageType` is "reduces
     AP"; not if b's is;
  3. a's `Jammer` must be > 0, and then a goes first if a's `weaponRange` is
     smaller than b's.

  Result: towers, then AP-damage units, then the other units with
  `Jammer` > 0 by ascending range, and units with `Jammer` = 0 at the end.
- **Free list:** "a goes before b" is decided by the first rule that
  applies:
  1. a has an AP-damage weapon (`damageType` "reduces AP") → yes;
  2. b has one → no;
  3. a's `Jammer` ≤ 0 → yes;
  4. b's `Jammer` ≤ 0 → no;
  5. a's `Jammer` < b's `Jammer` → yes;
  6. (the intended "a's `Jammer` > b's → no" compares b's value with itself
     and never applies);
  7. otherwise yes exactly if a's `weaponRange` > b's.

  So a lower `Jammer` does put a unit first when it is the unit being
  inserted (a grav unit with 10 before a normal unit with 100), but the
  reverse case falls through to the range test: a normal unit with a longer
  range is inserted before a grav unit. (Correction: an earlier version of
  this document said that only the range decides after rule 4.) Keep this
  for faithful ordering.

### 1.4 Actions are small state machines

Every action is advanced in **steps**. A step does one thing (evaluate
targets, move, fire) and may send the action back to an earlier state (for
example "target destroyed, look for the next one"). Each action may take
**at most 10 steps per turn**; at the 11th step it ends whatever its state.
In practice this limits a unit to about three "evaluate → move → fire"
cycles per turn. An action also ends at once if its unit no longer exists.

## 2. Unit orders

### 2.1 Data

Per AI unit (`Units On Map`, also set for reinforcements, battle-flow.md 4.1):

| Column | Meaning |
|---|---|
| `OrderID` | 0 none, 1 Goto, 2 Goto Fast, 3 Escort, 4 Defend, 5 Patrol, 6 Attack (`Unit Order Id Lookup`). Values 0–7 are accepted; 7 has no action. |
| `Turn` | last turn of the order: the order is cleared at the start of an AI turn when `Turn` < `T`. 99 means "for the whole battle". |
| `Probability` | per-turn chance (in %) that the unit keeps its order, 2.2 |
| `EnemyUnitType` | a unit `typeID` the unit particularly wants to attack (its target priority is doubled, 3.3); 65535 = none |
| `Unit Order Pos List` (`Unit ID`, `PosX`, `PosY`) | tiles for Goto, Goto Fast (waypoints, in `Counter` order) and Patrol (the patrol route) |
| `Unit Order ID List` (`Unit ID`, `Id`) | object ids for Escort, Defend (the protected own unit or building) and Attack (the target unit or building). Ids are looked up among the units first, then among the buildings. Only the first id is used; it is removed when its object no longer exists. |

### 2.2 Order upkeep (phase 0)

1. **Probability roll.** For **every** unit in the battle's unit list, in
   list order (human units and units without orders included), draw
   `r = rand() mod 100`. If `Probability < r`, the unit's order becomes 0.
   So the order is kept with probability `(Probability + 1) / 100`
   (always at 100). A cleared order never comes back.
2. **Expiry.** Every AI unit that is mobile and has an order: if
   `Turn < T`, the order becomes 0.
3. Build the order list (1.2).

### 2.3 Goto (1) and Goto Fast (2)

- Destination = the first tile of the unit's position list. If the list is
  empty, the order becomes 0.
- If AP > 3 and the unit is not on that tile, it is registered for the
  group move of its phase (4.4). If it is exactly on the tile, the tile is
  removed from the list and the unit does nothing more this turn.
- After the group move, for every member: if it stands within one tile of
  its first waypoint (the 3 × 3 square around it), that waypoint is removed;
  when the list becomes empty the order becomes 0.
- Goto keeps one shot of AP back while moving (only units with
  `canOverwatch`), Goto Fast does not (4.4). That is the only difference.
- Goto units never attack-move; they only fire in phase 5 (and by reaction).

### 2.4 Escort (3)

- Needs AP ≥ 4, else the action ends. If the id list is empty the order
  becomes 0. If the first id belongs to no existing object, it is removed
  and the next step tries again *(?)*.
- **Area** = the protected object's footprint rectangle enlarged by 3 tiles
  to the left and top and by 4 tiles to the right and bottom (the program
  enlarges by 3 and then adds one more column and row; probably meant to be
  symmetric).
- If the escort stands in the area, it switches to the free hunting
  behavior of 2.8 for the rest of this turn (same action, same 10-step
  budget). This includes the idle fallback of 2.8, so an escort without a
  target walks toward the map center.
- Otherwise it moves toward the object: among the 8 neighbor tiles of the
  object's anchor tile (`PosX`/`PosY`, i.e. the top-left tile of a
  building), tried in facing order 0…7 (tactical-movement.md), it takes the
  reachable one with the fewest path steps (first one on ties). It walks
  that path, cut to AP, but only up to the **first stopping tile inside the
  area**; if the path never enters the area, it does not move. If it ends
  on the planned tile the action is over; if the move was interrupted
  (reaction fire, mine), it plans again from the start.

### 2.5 Defend (4)

Like Escort, with two differences:

- the area is enlarged by 2 (left/top) and 3 (right/bottom);
- a defender already inside the area does **nothing** more this turn (no
  hunting). It only fires in phase 5.

### 2.6 Patrol (5)

The position list is the **route**: in the shipped data a closed chain of
neighboring tiles (often 5–50 of them).

- Needs AP > 3, else the action ends. With an empty route the action ends
  (the order is kept).
- **Not on the route:** go to the route tile with the shortest path (fewest
  steps; the first such tile in list order wins ties), cut to AP. When it
  arrives, it continues as below in the same turn; if not, it plans again.
- **On a route tile:** evaluate all enemy units like a free unit (3.1–3.4,
  units only). If there is no candidate, the action ends: a patrolling unit
  does **not** walk along its route by itself. Otherwise the path to the
  chosen firing tile must start on the route: if its first step is not a
  route tile, the action ends. The path is kept up to and including the
  first stopping tile that is **not** on the route (so the unit can end one
  tile off the route; probably meant to stop before it). Then the
  move-to-fire procedure (4.2) runs on that path and the unit fires (5.2).

### 2.7 Attack (6)

- Needs AP > 3. If the id list is empty the order becomes 0. If the first id
  belongs to no existing object it is removed and the next id is tried in
  the next step.
- Only that object is evaluated: a unit through its firing rings, a
  building through its four building tiles (3.2). The AI's own buildings can
  be targets this way (in the shipped data most Attack lists that point at
  buildings name neutral tech centers and ruins, so the order effectively
  sends the unit next to them).
- If no firing position was found, or the path to it is empty, the action
  ends. Otherwise: move-to-fire (4.2), fire (5.2). When the target is
  destroyed its id is dropped and, within the step budget, the next id is
  attacked.
- There is no idle fallback.

### 2.8 No order: hunting (free units and escorts in their area)

1. Needs AP ≥ 4, else the action ends.
2. Evaluate targets (3.1–3.4).
3. If there is no candidate:
   - normal battle: move toward the **map center**: in the central
     12 × 12 square (starting at `(size − 12) div 2` on both axes) take the
     first tile, scanning columns with x ascending and, inside a column, y
     ascending, that the unit can reach (stopping tile, path exists); walk
     there, cut to AP;
   - counter-attack battle, AP ≥ 10: possibly call a transport, 6.4.
   The action ends.
4. Otherwise run the move-to-fire procedure (4.2) toward the chosen firing
   tile and fire (5.2). If the target was destroyed, go back to 2.

### 2.9 Shipped scenarios (cross-check)

Counted over all 114 scenario databases (AI units in `Units On Map`):

| `OrderID` | units | list data |
|---|---|---|
| 0 | 3501 | – |
| 1 Goto | 180 | 1–23 waypoints (mostly 1) |
| 2 Goto Fast | 189 | 1–10 waypoints (mostly 1) |
| 3 Escort | 984 | almost always 1 id: own units (≈ 2/3) or own buildings |
| 4 Defend | 448 | 1 id: own units or own buildings |
| 5 Patrol | 526 | route of 1–50 tiles, mostly contiguous loops |
| 6 Attack | 179 | 1 id: human units, or neutral (owner 0) tech centers / ruins |

`Turn` is mostly 1–15 or 99 (a few other values up to 90); `Probability`
is mostly 33, 50, 66, 75, 80 or 100; `EnemyUnitType` is 65535 in about
95 % of the ordered units. Human units never have orders. Examples: `s1-2-1` has Escort and
Defend groups with `Turn` 1–7 and Patrol units with 20-tile loops;
`s1-4-1` mixes Goto Fast (sparse waypoints across the map), Escort, Defend
and short Patrol loops; `s2-3-2` has 51 AI units of which 17 have orders and
10 are towers. Some scenarios (e.g. `s1-1-1`) have no AI units at all on the
map.

## 3. Target selection

### 3.1 Candidates

- **Enemy units**, in map list order (for hunting, patrol and stationary
  fire; Attack uses only its own target).
- In **counter-attack battles only**, and only for shooters whose
  `damageType` is "reduces armor", hunting also considers:
  - buildings not owned by the AI (`playerOwner` ≠ 0), in map order;
  - mines the AI knows about (`hidden_AI` false), whoever owns them;
  - walls, but only if no candidate was found so far, or the best one so
    far has a path of more than 25 steps **and** a negative score.
- `Unit Types.isAiTarget` is read but has no effect. `Building Types.
  isAItarget` only makes an AI-owned building count as an enemy in the
  player–building relation (tactical-combat.md 1.1); the AI's own target
  search does not use it. `Buildings On Map.IsAiTarget` and
  `Unit Types.DanyUnitValue` are not read at all.
- Hit chance, terrain cover, the target's `Jammer` and reaction fire danger
  are **not** part of the evaluation (only the "approach" rule 4.2 b looks at
  the target's ability to shoot back).

### 3.2 Firing positions

For every candidate the AI tries a set of tiles from which to shoot. A tile
qualifies if it is inside the map, it is a stopping tile the unit can reach
(a path exists in the unit's path map, of any length; AP does not matter
here) and the **range and line-of-fire part** of the shot check passes from
it: for each aim tile of the target (a building's footprint column by
column, otherwise the target tile) `weaponMinimumRange < d ≤ weaponRange`
from the tile, and the line of fire (skipped for `weaponType` 1). Weapon
presence, AP, side (relation) and `Jammer` are **not** checked here (AP is
handled by the move-to-fire rules of 4.2). Each qualifying tile is scored
(3.3) and compared with the best so far (3.4); the path to the best tile is
kept.

- **Unit:** rectangular rings around the target's footprint. With
  `R = weaponRange` and `m = max(1, weaponMinimumRange)` of the shooter, the
  rings are the border tiles of the footprint enlarged by `R`, then (only
  if `R > 1`) by `m`, then by `(R + m) div 2`. Each ring is walked: top row
  left → right, right column top → bottom, bottom row right → left, left
  column bottom → top (the corner tiles of the right side are visited
  twice, which only matters for ties). Because the minimum range is
  exclusive, the `m` ring of a weapon with minimum range ≥ 1 never
  qualifies.
- **Building:** four tiles computed from its rectangle (left `L`, top `T`,
  right `R = L + SizeX`, bottom `B = T + SizeY`, right and bottom
  exclusive): `(L − 1, T − 1)`, `(R + 1, B + 1)`, `(T + 1, R − 1)` and
  `(B − 1, L + 1)`. The last two have x and y swapped, a slip in the
  original; keep them for faithful behavior. The candidate's tile (used for
  the shot) is the building's top-left tile.
- **Mine:** the 2 × 2 block `x − 1 … x`, `y − 1 … y` (the original probably
  meant 3 × 3).
- **Wall:** the 3 × 3 square around it.
- **Stationary fire (phase 5):** only the unit's own tile; only enemy units
  that it can legally shoot right now (including the AP check) are
  candidates.

### 3.3 Score

For shooter `S` and candidate `X` (all intermediate values are real
numbers, the result is truncated toward zero):

    shots   = AP(S) div shotCost(S)                       (integer)
    avgDmg  = damageBase(S) + damageRandom(S) div 2       (integer, no difficulty scaling)
    power   = shots × avgDmg
    k       = min(100, 100 × power / armor(X))
    penalty = min(500, 10 × steps)
    base    = k / 100 × W − penalty
    if X is a unit and k > 99:  base = base + Cost(X)
    P       = Priority[AIcategory(S)][category(X)]       (AI Priority table)
    if X is a unit:
        if typeID(X) = EnemyUnitType(S):  P = 2 × P
        if damageType(S) = "reduces AP" and X cannot fire now:  P = 0
    else if damageType(S) = "reduces AP":  P = 0
    score   = trunc(P / 100 × base)

- `AP(S)` is the shooter's current AP; the AP needed to walk to the firing
  tile is **not** subtracted.
- `armor(X)`: a unit's or building's `currentAR`, a wall's current armor,
  1 for a mine.
- `steps`: number of steps of the path to the firing tile (0 in stationary
  fire). *(?: in stationary fire the program actually reads the length of a
  leftover path from an earlier evaluation, which subtracts the same
  constant from all candidates of that unit; OpenDay should use 0.)*
- `Cost(X)`: `Unit Types.Cost` of the target's rules row. It is a "can kill
  it this turn" bonus: `k > 99` means the expected damage of all shots this
  turn reaches the target's armor.
- `category(X)`: `Unit Types.AIcategory`, `Building Types.AIcategory`, 7
  for walls, 8 for mines. The `AI Priority` table is read into a 12 × 12
  matrix; a missing pair counts as 0, a duplicated pair is a load error.
- `W` depends on the situation:

  | Situation | W |
  |---|---|
  | unit target, firing ring (hunting, Patrol, Attack) | 500 |
  | building tile | 1000 |
  | mine | 500 |
  | wall | 5 |
  | stationary fire, target can fire now | 1000 |
  | stationary fire, otherwise | 500 |

- "X can fire now": X has `UnitHasWeapon` and its own AP ≥ its shot cost,
  i.e. it could still react during the AI turn.
- The per-unit priority bonus that the program adds to `P` for unit targets
  is never set by anything and is always 0.

### 3.4 Keeping the best candidate

The best candidate starts with the score −65535 ("none"). For each new
candidate C against the current best B (scores compared as integers):

1. If `B > C + 1`: keep B.
2. Else if `B > C − 1` (the two are within ±1):
   - if B and C have the same target tile: take C (so for one target, the
     last qualifying firing tile among equal scores wins);
   - otherwise keep B if B's target tile is strictly closer to the
     shooter in x **or** strictly closer in y (absolute differences);
     else take C.
3. Else (`B ≤ C − 1`): take C.

## 4. Movement

### 4.1 Path search used by the AI

- The AI computes a fresh path map (tactical-movement.md 3.1) for a unit
  whenever it evaluates targets or destinations for it, with the **AI
  variant** switched on (tactical-movement.md 3.1): on maps of at least 57
  tiles (the 72 and 96 maps), for a unit whose top-left tile is outside the
  central 12 × 12 square, the sweeps are limited to the **42 × 42** window
  `x − 20 … x + 21`, `y − 20 … y + 21` around the unit (pushed back into the
  map without shrinking). Independently of map size and window, every AI
  path map stops after **7 rounds** of sweeps. Boats always search the
  whole map without a round cap. On maps of at least 57 tiles every AI path
  map (unit inside or outside the centre) also draws and ignores one random
  number. (Correction: earlier versions said 41 × 41 and limited the round
  cap to large maps.)
- Group moves of Goto/Goto Fast (4.4) use the normal full-map search.
- The danger map the program allocates for the AI is never filled or read.

### 4.2 Move-to-fire procedure

Input: the path to the chosen firing tile. It is first cut to AP. If the
cut path is empty, nothing happens (the action just uses up steps). Then
the first rule that applies is used:

0. If the shooter can already make a legal shot at the target from where
   it stands (full check, including AP), it does not move and fires (5.2).
1. **Shortest walk to a firing spot with AP to spare.** Walk the cut path
   from its first step, adding up step costs. The first step tile that is a
   stopping tile, from which a shot at the target is legal, and where
   `AP − cost so far > shotCost` (strictly more than one shot left), ends
   the move. The unit walks there.
2. **Stay out of the target's reach.** Only if the target is a unit that
   can fire now (3.3). If the target can already legally shoot at the
   shooter's present tile, skip to rule 3. Otherwise walk the path and find
   the first tile at which the target could legally shoot at the shooter;
   the move ends on the tile before it, if that tile is a stopping tile
   (else look for the next such tile). If already the first step is in the
   target's reach, the rule fails.
3. **Advance while a shot remains.** Only for shooters with
   `canOverwatch`, and only if `AP ≥ shotCost`. Walk the path adding up
   costs; the move ends on the first stopping tile where
   `AP − cost < shotCost`, or at the end of the path. *(As implemented, the
   unit therefore arrives with less than one shot of AP; it looks as if
   the tile before was meant.)*
4. **Plain approach.** Walk the whole cut path.

Each move is a single-unit move through the server (tactical-movement.md
5 applies: reaction fire and mines can stop it). After the move: if the
unit stands on the planned end tile it goes on to fire (5.2), otherwise
(interrupted) it starts again with target evaluation, within its step
budget.

### 4.3 What the AI does not do

- No retreat: there is no rule that moves a unit away from enemies, and
  low own armor has no influence.
- No deliberate AP saving for reaction fire, except the Goto reserve (4.4)
  and the side effect of rule 3 above.
- No coordination between units beyond the group move of Goto orders and
  the shared claim list of that group move.

### 4.4 Group move of Goto / Goto Fast units

All units registered in phase 10 (or 11) are moved as one group, in list
order, with the rules of a human group order (tactical-movement.md 3.5):

- Destination: the unit's current waypoint. If it is not a stopping tile
  for the unit **or** already claimed by an earlier member, the 8 neighbors
  are tried in the order of tactical-movement.md 3.5 (orthogonal neighbors
  (X−1, Y), (X, Y−1), (X, Y+1), (X+1, Y), then the diagonals (X−1, Y−1),
  (X−1, Y+1), (X+1, Y−1), (X+1, Y+1)); with none left the unit does not
  move. (Unlike a human order, a non-stopping waypoint also leads to the
  neighbor search.)
- Start tiles of earlier members count as free; their destinations as
  blocked (fly-over still allowed).
- The path is cut to the unit's AP reduced by its shot cost, for Goto
  units with `canOverwatch` (not below 0), or to its full AP for Goto Fast.
- Members whose cut path is empty are left out; the rest are moved one after
  the other by the server. If the battle ends during the group move, the
  remaining members do not move.
- Then the waypoint check of 2.3 runs.

## 5. Shooting

### 5.1 Stationary fire (phase 5)

For each unit of the stationary-fire list:

1. Score every enemy unit that it can legally shoot from its tile right now
   (3.3 with `steps` = 0, W = 1000 or 500). With no candidate, the unit is
   done.
2. Fire at the best one with **autofire** (tactical-combat.md 2: keeps
   firing until the target is destroyed, or for AP weapons has no AP left,
   or the shooter runs out of AP).
3. If the target is gone, go back to 1; otherwise the unit is done.

With the 10-step budget a unit can kill at most three targets this way per
turn. The internal name of this action (`CAIaction_shoot_overwatch`) is
misleading: it is ordinary fire in the AI's own turn. Reaction fire during
the human turns is not controlled by the AI at all; the server fires for
every eligible AI unit automatically (tactical-combat.md 6.1).

### 5.2 Fire after moving

When the move-to-fire procedure is done, the unit fires at its target if
the target still exists and the shot is legal from its tile with its AP
(full check), with autofire. If the target was destroyed, the unit evaluates
again (hunting and Attack: next target; Patrol: back to the route check);
otherwise the action ends.

### 5.3 Notes

- The AI always uses autofire. It never fires single shots.
- AI damage is scaled by the difficulty (tactical-combat.md 4.2); the
  target evaluation uses unscaled values.
- AI shots skip the side check (tactical-combat.md 1.1); together with the
  Attack order this lets the AI fire at its own or neutral buildings.

## 6. Automated units called by the AI

The automated units themselves (AA fire, effects, animation) are described
in [automated-units.md](automated-units.md). Every AI call below goes
through one **call routine**: it builds the same server object as a human
request, with owner 0 and the two points p1, p2 given by the caller, and
executes it like a human call (automated-units.md 4.1: effect, tech
buildings, battle-over check, broadcast), **unless** the type's `Race`
(the `Automated Units` column; it differs from the lookup table for type
10) is 0 (human) and its `typeID` is not 4, 5 or 6. The call routine checks
no cost (the callers handle the budget) and ignores `DeployOnce`. So every
alien type arrives, including the destroy types 11, 12 and 13; human types
other than the bombers and the nuke are silently dropped (this only
matters for scheduled rows, 6.2). Units dropped by the AI have tech
level 0.

### 6.1 Random calls (phases 2 and 7)

Only with strategic AI info (not in skirmish).

1. Chance `c` = `AiAutoUnitChanceCall` of the colony; in a counter-attack
   battle always 25. Draw `rand() mod 100`; if it is not below `c`, nothing
   happens.
2. Pick one entry of the colony's `AI automated available` list uniformly:
   `rand() mod count`.
3. Outside counter-attacks the AI must own (owner 0), on this battle map,
   a building of the entry's `Prerequisite` type; otherwise nothing
   happens.
4. By automated type (only alien types are listed in the shipped data):

   | type | target chosen |
   |---|---|
   | 9 Tauran mine detection (`T_Engineer_Mine`; it reveals mines, it lays none) | counter-attack battles only. 50 % (`rand() mod 100 < 50`): the tile of the first mine the AI knows about (tile 0,0 if there is none); otherwise a random tile (`rand() mod width`, `rand() mod height`) |
   | 10 Tauran medic | the AI unit (alien, mobile) with the largest `Armor − currentAR`; nothing if none is damaged |
   | 11, 12, 13 fighters, gene bomb | the top-left footprint tile of the enemy unit with the highest `currentAR` (strictly greater wins, so the first one on ties; the running maximum starts at 0) |
   | 14, 19–25 drops | starting at list position `rand() mod (number of units on the map)`, the first enemy unit further down the list (no wrap-around) with `currentAR` ≤ 30 and unit `Category` grav or ground |
   | others (7 transport, 8 bridge) | not called this way |

5. A call needs `Cost` ≤ budget and lowers the budget by `Cost`. The
   target tile is passed as both p1 and p2 to the call routine (above), and
   the automated unit is then handled by the server as a player's call
   would be (anti-air fire at the target tile, effect:
   automated-units.md 5–6). The fighters and the gene bomb do arrive (the
   call routine filters by race, not by category).

The **budget** starts at the colony's `AiAutoUnitRes` (× 1.5 in a
counter-attack battle, truncated) and is spent over the whole battle.

### 6.2 Scheduled automated units (phase 6)

The scenario's `Automated Units` rows (`AutomatedType`, `StartX`,
`StartY`, `DestX`, `DestY`, `TurnArrival`) are kept in a list. In phase 6
of every AI turn, each row with `TurnArrival` **exactly equal** to `T` (the
turn counter as read at the start of the AI turn) is passed to the call
routine with **p1 = (`StartX`, `StartY`)** and **p2 = (`DestX`, `DestY`)**
and removed from the list. No budget, no prerequisite. Rows whose turn has
passed without a match are never called.

- For non-transport types the target is (`StartX`, `StartY`); `Dest` is
  ignored (in the data it always equals `Start`).
- For transports `Start` is the tile of the unit to pick up (any owner) and
  `Dest` the destination.
- Shipped data: 312 rows in 67 scenario files, turns 1–11. By type: 14
  (58 rows), 19 (58), 23 (49), 12 (25), 25 (23), 22 (20), 7 (18), 13 (16),
  21 (11), 11 (10), 20 (7), 4 (6), 5 (5), 6 (4), 0 (2). The rows of the
  human types 4, 5, 6 (`s4-18-*`, turn 1) are executed as **AI-owned**
  strikes; the two type-0 rows (`s3-7-*`, human transport) are dropped by
  the race rule.

### 6.3 Emergency drop (phase 3)

In every AI turn one `rand() mod 100` is drawn. If it is below 67 and
`T` < 5: count the enemy units and, for five human types (Trooper,
Grenadier, Assault Trooper, LAV, Light Tank, by symbol), how many of them
are of that type. For each of the five types whose share is above 60 %,
while more than 30 enemy units are on the map, the automated type with
`typeID` 19 is called 1–3 times, as a drop (6.1 table) at no net cost
(for each call the budget is first raised by the type's `Cost`, then the
drop spends it again). The number of calls depends on the scenario's
**`Scenario Info.One Off Deployment`** (shipped values 150–2300): 3 if it is
above 1200, 2 above 700, 1 above 300, otherwise none.

Details:

- The enemy count is made by stepping an enemy-unit iterator over the map
  unit list once per type pass, summing the steps over the five passes and
  multiplying by 0.2. A pass whose list does not end with an enemy unit
  counts one step too many (an artefact of the loop), so the count can be
  "enemies + 1". Keep this for faithful behaviour.
- Type 19 (`B_Zeppelin_G`, four Pyro Maniacs) is the element at position 19
  of the full automated type list; with fewer than 20 types nothing is
  called.

### 6.4 Transport in counter-attacks

A hunting unit without any candidate (2.8 step 3) in a counter-attack
battle (strategic AI info present), with available AP **≥ 10**, that is
neither a boat (movement class 4) nor an underwater unit (6), may call the
colony's transport (automated type 7, if it is in the colony's list). If
the budget covers its `Cost`, the cost is deducted at once (even if no tile
is found). Then, for the enemy units in list order: the tiles of the
enemy's footprint enlarged by 10 are collected (row by row), shuffled (for
each position i, swap with position `rand() mod n`), and the first one that
passes the unit placement test for this unit's type (inside the map,
enterable for its whole footprint, owner 0's mine knowledge) is used as the
destination.

The call is p1 = the hunting unit's own tile (the top-left tile of its
footprint), p2 = that destination. This is how the unit is "attached": the
transport picks up whatever unit stands on p1 (automated-units.md 6.6),
flies it to p2 under AA fire and leaves it there with 0 AP.

## 7. Randomness and difficulty

Random draws made by the AI, in turn order:

1. Phase 0: one `rand() mod 100` per unit on the map (2.2).
2. Phases 2 and 7: the call chance, the list pick, then per type: mine layer
   1 or 3 draws, drops 1 draw (6.1).
3. Phase 3: always one draw; drop calls add their own draw (6.3).
4. Every AI path computation on a large map: one ignored draw (4.1).
5. Transport tile shuffle: one draw per collected tile (6.4).
6. All shots and their effects: the server's combat draws
   (tactical-combat.md 3.4).

Target choice and movement are otherwise deterministic. The difficulty
setting is not read by the AI's decision rules; it only scales AI damage
(tactical-combat.md 4.2) and the strategic deployment budget.

## Notes for OpenDay

- Implement the AI as a server-side component that issues the same move and
  shot operations as the request handlers, phase by phase as in 1.2.
- Keep the step budget (10 per action), the list orders (1.3), the score
  formula with truncation (3.3) and the ±1 tie rule (3.4); together they
  decide which unit attacks what.
- Faithful-but-odd rules to keep (or to put behind a "fixes" switch): the
  building tile coordinates (3.2), the 2 × 2 mine block, the asymmetric
  escort/defend areas, the patrol path that may end one tile off the route,
  the free-list comparator, and rule 3 of 4.2.
- Use `steps` = 0 for stationary fire.
- The time slicing of 1.1 is not needed.

## Implementation notes (OpenDay)

Implemented in `game/battle/ai.*` as a component of the server
(`TacticalAi`), called when slot 0's turn begins; it uses the server's own
move, shot and autofire code, and order changes reach the clients as
`OrderChanged` events.

- Implemented: phase 0 (the Probability roll is made by the server at the
  start of slot 0's turn, then expiry and the order list), phases 4–5
  (stationary fire), 8–9 (hunting), 10–11 (Goto group moves), 12 (Escort,
  Defend, Patrol, Attack), the score of 3.3, the ±1 rule of 3.4, the firing
  rings and building tiles of 3.2, the move-to-fire procedure of 4.2 and
  the list orders of 1.3.
- Step budget: evaluate, move and fire count as one step each, so every
  action gets three cycles (section 1.4 says "about three").
- The unit's own tile counts as a firing position with no steps, so a unit
  that is already in range fires without moving.
- Not implemented yet: automated units (phases 2, 3, 6, 7 and 6.4), the
  counter-attack rules (buildings, mines and walls as targets), the AI
  variant of the path search (42 × 42 window, 7-round cap and the ignored
  random draw),
  so the random sequence differs from the original's.

## Open questions

- (Answered in 3.2: the firing-position test checks only range and line of
  fire, not weapon, AP, side or `Jammer`.)
- What happens with an Escort/Defend/Attack id that refers to no object:
  the program's test for "neither a unit nor a building" reads a variable
  that also holds the AP value, so a missing object might not be removed.
- Why the emergency drop (6.3) always uses `typeID` 19 (answered: the
  battle value is `One Off Deployment`; the AI's destroy calls do arrive;
  the AI transport picks up the unit on p1).
- (Answered in 4.4: the replacement-tile order of the Goto group move.)
- The intended meaning of the unused per-unit priority bonus, of
  `DanyUnitValue` and of the danger map; they may be remains of a planned
  "unit value" evaluation.
