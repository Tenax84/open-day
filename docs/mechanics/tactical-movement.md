# Tactical movement and turn rules

How units move on the tactical battle map: which tiles a unit may enter or
stop on, what a step costs in action points (AP), how the path for a move
order is chosen, how a move can be cut short, and how the turns of the
tactical phase follow each other.

Prerequisites: the tables and columns in [data-model.md](data-model.md), the
map objects in [map-objects.md](map-objects.md), and the facing numbering
0–7 in [unit-animation.md](unit-animation.md) (section 2). Facings are used
here as direction numbers: facing `d` belongs to the grid step
`(dx, dy)` given there (0 = (0,+1), 1 = (−1,+1), 2 = (−1,0), 3 = (−1,−1),
4 = (0,−1), 5 = (+1,−1), 6 = (+1,0), 7 = (+1,+1)). Odd facings are the
diagonal grid steps.

Status: the rules below come from studying the program's behavior. Items
marked *(?)* are not fully confirmed. Combat itself (hit chance, damage,
line of fire) is out of scope; it is only mentioned where it interrupts a
move.

## Terms

| Term | Meaning |
|---|---|
| movement class | `Unit Types.Movement` (0 none-ground, 1 ground, 2 trooper, 3 grav, 4 boat, 5 air, 6 underwater, 7 teleport, 8 none-water, 9 grav-fly) |
| terrain class | `Terrain Types.type` of the tile's terrain sprite (0 shallow water, 1 deep water, 2 riverside, 3 plain, 4 mountains, 5 forest, 6 crack) |
| available AP | `max(0, currentAP − negAP)` of a unit |
| enterable | the unit may stop on the tile |
| crossable | the unit may pass over the tile during a move without stopping there; every enterable tile is also crossable |
| step cost | AP charged for entering a tile |

## 1. Tile evaluation (passability and cost)

For a given unit and tile the game computes four values: **enterable**
(yes/no), **crossable-only** (yes/no, "fly-over"), **step cost** (a small
integer) and a **reason code** (why the tile is blocked; used for feedback
only). They are computed in three stages.

### 1.1 Base table: movement class × terrain class

`E` = enterable, cost 10. `E13` = enterable, cost 13. `F` = crossable-only
(fly over, cannot stop). `EF` = enterable and also marked fly-over.
`–` = blocked.

| terrain \ movement | 0 none-g | 1 ground | 2 trooper | 3 grav | 4 boat | 5 air | 6 underwater | 7 teleport | 8 none-w | 9 grav-fly |
|---|---|---|---|---|---|---|---|---|---|---|
| 0 shallow water | – | – | – | E | – | F | – | E | – | EF |
| 1 deep water    | – | – | – | E | E | F | EF | E | – | EF |
| 2 riverside     | – | – | – | E | – | F | – | E | – | EF |
| 3 plain         | – | E | E | E | – | EF | – | E | – | EF |
| 4 mountains     | – | – | – | – | – | F | – | – | – | F |
| 5 forest        | – | – | E13 | – | – | F | – | – | – | F |
| 6 crack         | – | – | – | – | – | F | – | – | – | F |

Notes:

- The "fly-over" mark is what lets a unit cross a tile it cannot stop on.
  Only air (5), grav-fly (9) and underwater (6, deep water only) units have
  it. It also survives the obstacle rules below, so these units can pass
  over (or under) units, buildings, walls, ruins and known mines, but never
  stop on such a tile.
- Blocked tiles also carry the cost 10; it is never used.
- Movement classes 0 and 8 (towers, platforms) can never move.
- Teleport units (7) do not walk a path at all (see 3.6); the table only
  decides where they may land.

### 1.2 Roads and bridges

If the tile has a road (`Roads On Map`), the base value is changed by
movement class:

| movement class | effect of a road |
|---|---|
| ground (1), trooper (2) | On water terrain (classes 0, 1, 2) the tile becomes **enterable** (a bridge or ford). On **every** terrain the step cost is replaced: low-tech road (`roadtype` 0) **7**, high-tech road (`roadtype` 1) **4**. A road does not make mountains, forest (for ground units) or cracks enterable; only the cost changes there, which has no effect. |
| grav (3), air (5), grav-fly (9), teleport (7) | On water terrain (0, 1, 2) the tile becomes enterable (an air unit can land on a bridge). The cost is not changed. |
| boat (4) | On water terrain the tile becomes **blocked** and loses the fly-over mark: boats cannot pass under a bridge. |
| underwater (6) | On water terrain the tile becomes not enterable but keeps the fly-over mark: a submarine can pass under a bridge but not stop there. |

Roads on terrain classes 3–6 have no effect for classes 3–9.

### 1.3 Obstacles

Applied after the road rule, in this order. Each obstacle only clears
"enterable"; the fly-over mark is kept.

| Obstacle (map layer) | Rule | Reason code |
|---|---|---|
| any unit on the tile (own or enemy, including the moving unit itself) | not enterable | 2 |
| wall (any wall type, also ruined walls) | not enterable | 3 |
| building (any owner, any category) | not enterable | 4 |
| mine that is **armed** and **not hidden from the moving unit's player** (`hidden_<player>` false) | grav mine (`typeID` 1): not enterable for every movement class. Standard mine (`typeID` 0): not enterable unless the class is grav, air, grav-fly or teleport. | 5 |
| ruin whose `Ruin Types.can_move_through` is false | not enterable | 7 |

Other reason codes: 0 = free, 1 = terrain, 6 = bridge blocks a boat,
8 = made enterable by a road. A mine the player does not know about does
not affect path planning; it is dealt with during the move (section 5).

### 1.4 Map edge and unit size

- Tiles outside `0 … size−1` are blocked. Edge tiles themselves are normal.
- Units with a footprint other than 1 × 1 get "blocked" for every tile, so
  multi-tile units never move. *(The 3 × 3 types in the shipped data are
  immobile anyway.)*

### 1.5 Deployment variant

When a unit is placed during deployment, the same evaluation is done over
its whole footprint (every footprint tile must be enterable; the fly-over
mark is kept only if all tiles have it; the highest step cost wins), with
two differences: a tower (class 0) may be placed on plain and a platform
(class 8) on deep water, and the unit being placed does not block itself.

## 2. AP cost of a step

- **Step cost** = the value of the tile being entered after 1.1–1.3:
  10 for normal terrain, 13 for a trooper entering forest, 7 or 4 for a
  ground/trooper unit entering a road tile.
- **Direction:** for choosing the path, a diagonal step (odd facing) counts
  `cost + 1`; an orthogonal step counts `cost`. **The AP actually charged
  ignores this +1**: every step is charged its tile cost only. The +1 just
  makes the planner avoid needless diagonals.
- No cost for turning, starting, stopping or the first step.

### When AP runs short

The path to the clicked tile is first computed without regard to AP (see 3),
then cut to what the unit can afford:

1. If the first step costs more than the available AP, the unit does not
   move.
2. Otherwise walk the steps, subtracting each step cost from the available
   AP. When the remaining AP becomes negative on a step, and the previous
   step's tile is enterable, the path ends on that previous tile.
3. If the previous tile is crossable-only (fly-over), the unit keeps going:
   the path ends on the first enterable tile after the AP ran out. So an
   aircraft over mountains is allowed to overrun its AP to reach a landing
   spot.

When the move is executed, the summed step cost is subtracted from
`currentAP`. If it exceeds the available AP (only possible through the
overrun above), air, grav-fly and underwater units pay their whole
available AP instead; for other classes the original subtracts nothing in
that case *(a quirk; OpenDay should simply clamp at 0)*. Moving never
creates `negAP`.

### Keeping AP for a shot

The reserve is the human client's **Overwatch** option (the overwatch lamp
of the tactical panel, `[User] Overwatch`, default on; tactical-interface.md
2.2). Every human move planner — the two-click move (preview and send), MOVE
ALL, and the path preview of a move-to-fire first click — uses this
**movement budget** instead of the available AP:

    budget = available AP
    if the Overwatch option is on
       and the unit type has canOverwatch
       and actionPointsShoot ≤ available AP:
        budget = available AP − actionPointsShoot
    budget = max(budget, 0)

The path is cut to the budget with the rules above. The server does not know
the option: it re-prices the path and cuts it to the full available AP, so
the reserve exists only because the client sends a shorter path. The shot
request (move-to-fire, 3.7) does not use the reserve; it picks a firing tile
whose cost leaves at least one shot. The computer player keeps a shot's
worth of AP in its Goto orders (tactical-ai.md 4.4).

Neither the Overwatch nor the Autofire option changes reaction fire on the
server (section 5): reaction fire is fully automatic. The only effect of the
Overwatch option is that units keep enough AP to be eligible for it later.
The Autofire option only sets the autofire flag of a shot request
(tactical-combat.md 2).

### Plausibility against the data

AP values are 80–450 (troopers 80–170, vehicles 60–450, grav 80–310). At
10 per plain tile a basic Trooper (80 AP) walks 8 tiles (6 in forest,
11 on a low-tech road, 20 on a high-tech road), a Main Tank (200) 20
tiles. Shot costs are 30–180, so a trooper can still walk 4 tiles and fire.

## 3. Pathfinding

### 3.1 Algorithm: whole-map "remaining value" relaxation

The planner fills a grid the size of the map, one record per tile:
enterable, crossable (= enterable or fly-over), step cost, and a value.
It is a label-correcting shortest-path search (equivalent to Dijkstra in
result, not in procedure):

1. Evaluate every tile with section 1 for the moving unit. Set every value
   to 0, the start tile's value to the largest signed 32-bit integer.
2. Repeat sweeps until a whole sweep changes nothing:
   - a forward sweep over the inner tiles (`1 … size−2`), column by column
     with x ascending and, inside each column, y ascending;
   - a backward sweep over the same tiles, row by row with y descending
     and, inside each row, x descending;
   - a pass over the four border lines (row y = 0, row y = size−1,
     column x = size−1, column x = 0, each in ascending order), with
     neighbors outside the map ignored.
3. Updating one tile T (only if T is crossable): take the neighbor N with
   the largest value among the 8 neighbors, scanned in facing order 0…7
   (first one wins on equal values; only values above 0 count). The
   candidate value is `value(N) − (cost(T) + 1 if N is a diagonal
   neighbor else cost(T))`. If it is larger than value(T), store it.

A tile's final value is "start value minus the cheapest planning cost to
reach it"; tiles that cannot be reached keep 0. The search is not limited
by AP or by distance. Crossable-only tiles take part as intermediate tiles.
**There is no corner-cutting rule**: a diagonal step between two blocked
orthogonal neighbors is allowed.

**AI variant.** The computer player's own path maps use a restricted search
(the human client and the AI's Goto group move use the normal one). With an
`n × n` map and the usable area `1 … n−2` in both axes:

1. Boats (class 4) always use the normal search (no window, no round cap).
2. If `n < 57`, the whole usable area is swept (so the window below
   concerns the shipped maps of 72 and 96 tiles).
3. If `n ≥ 57`: one random number is drawn and discarded (always, for every
   AI path map on such a map — this matters for reproducing the random
   sequence). The central square is the 12 × 12 tiles starting at
   `((n − 12) div 2, (n − 12) div 2)`. If the unit's top-left tile is inside
   it, the whole usable area is swept. Otherwise the sweep window is columns
   `x − 20 … x + 21` and rows `y − 20 … y + 21` inclusive (**42 × 42**
   tiles) around the unit's top-left tile (x, y). The window is pushed back
   into the map without shrinking (top < 1 → move it down by `1 − top`;
   bottom > n − 2 → move it up by the excess; the same for left / right) and
   finally clipped to the usable area.
4. **Round cap:** for every AI path map except boats, at most **7 rounds**
   (forward sweep + backward sweep + border pass) are made, regardless of
   map size and window, also on small maps.

Correction: earlier versions of this document said 41 × 41 and limited the
round cap to large maps.

### 3.2 Destination check

The clicked tile must be inside the map and **enterable** (section 1),
otherwise the order is rejected for this unit. In a group move (3.5) a
replacement tile may be chosen.

### 3.3 Path extraction

Walk back from the destination to the start:

1. At the current tile, look at the 8 neighbors in facing order 0…7 (inside
   the map only). The best neighbor is the one with the largest value,
   which must be larger than the current tile's value.
2. Tie-break between neighbors with equal value: a later neighbor replaces
   the earlier choice only if it is strictly closer to the start tile in x
   **or** strictly closer in y (comparing absolute differences).
3. If no neighbor has a larger value, the search fails (unreachable).
4. Prepend each tile; stop when the start tile is reached.

Then the start tile is removed. Each remaining step stores: tile x, y, its
step cost (without the diagonal +1), whether the tile is enterable, and
the facing of the step (from the previous tile to this one).

### 3.4 Path preview (`SYMBOLS.DBI`)

After the path is cut to the affordable part (section 2), every step of
the full path is marked with one sprite:

| Sprite | Step | Look |
|---|---|---|
| `PATH0000` | affordable, enterable | blue marker |
| `PATH0001` | not affordable, enterable | blue marker with an X |
| `PATH0002` | affordable, crossable-only | lighter marker *(drawn after all cells, on top: map-objects.md draw modes)* |
| `PATH0003` | not affordable, crossable-only | lighter marker with an X *(same flag)* |
| `PATH0004` | last step (destination), reachable this turn | red marker |
| `PATH0005` | last step, not reachable this turn | red marker with an X |

"Not affordable" starts at the first step after the cut point of section 2
(including the overrun rule).

### 3.5 Group orders

Up to 64 selected units can be sent in one order (a 65th entry is
skipped; a unit already in the order is skipped). The client keeps two
lists while planning: the **claimed** tiles and the **start tiles** of the
units planned so far. For each unit:

1. A fresh path map is built for the unit; claimed tiles are made not
   enterable (keeping their fly-over mark); the start tiles of the earlier
   members are made free (those units will have left); then the full-map
   search runs.
2. **Destination.** Human orders: if the clicked tile is not enterable for
   this unit, the unit gets **no order**. If it is enterable but already
   claimed, a replacement tile is searched (below). The computer player's
   Goto group (tactical-ai.md 4.4) searches a replacement in **both** cases
   (not enterable or claimed).
3. **Replacement tile:** the 8 neighbors of the clicked tile (X, Y) that
   are not claimed and enterable for the unit, ordered by squared Euclidean
   distance to (X, Y), ties in the scan order "x from X−1 to X+1, inside
   that y from Y−1 to Y+1". The resulting order is
   (X−1, Y), (X, Y−1), (X, Y+1), (X+1, Y), then (X−1, Y−1), (X−1, Y+1),
   (X+1, Y−1), (X+1, Y+1). The first one is taken; if none qualifies the
   unit does not move.
4. The path is computed and cut to the unit's movement budget (2, "Keeping
   AP for a shot"); a unit whose cut path is empty is skipped.
5. The **last tile of the cut path** (where the unit really stops this turn)
   is added to the claimed list — not the clicked tile — and the unit's
   current tile to the start tiles.

**Repeat passes** (human two-click group move): the selected units are
processed in selection order; units that got an order are removed and the
whole pass is repeated over the remaining units as long as the previous
pass gave at least one unit an order. (So a unit blocked by a member planned
after it can still move.) MOVE ALL makes only **one** pass.

### 3.6 Teleport units

A teleport unit (class 7) gets a single step straight to the destination,
with no path search. The destination must be enterable. The step costs the
destination's tile cost (10), and the arrival facing is random (0–7).

### 3.7 "Move to fire" orders

A second order type moves a unit to a tile from which it can shoot a chosen
target. The client chooses the firing tile:

1. **Candidates:** the tiles of the target's footprint (a building's
   rectangle, otherwise the target tile) grown by the shooter's
   `weaponRange` on every side, **minus** the tiles of the footprint grown by
   its `weaponMinimumRange`; enumerated with x as the outer loop and y as the
   inner loop.
2. The path map is a fresh full-map human search (no AI window); when the
   order is sent for a group, with the claimed / start-tile lists of 3.5.
3. A candidate qualifies if the unit can stop on it, a path exists, the path
   cost (sum of step costs, without the diagonal +1) is **strictly below**
   the available AP, and the full shot legality check (tactical-combat.md 1,
   including "AP ≥ shot cost") passes from that tile with the path cost
   counted as already spent.
4. The qualifying candidate with the **lowest** path cost is taken; on equal
   cost the first in enumeration order is kept.

The unit's own tile is never a candidate (the unit itself blocks it). How
the interface uses this (two-click "mark, then fire") is described in
tactical-interface.md 3.2.

## 4. Turn structure of the tactical phase

### Player order

The participants are the connected human player slots plus slot 0 (the
AI side), sorted by slot number: **0 (AI), 1, 2, 3, 4**. The players take
whole turns **one after another** (I-go-you-go, no simultaneous moves).
During a player's turn only that player's move requests are accepted; the
server ignores requests from others.

If the AI side is not active in the battle *(?: flag set by the scenario
mode)*, slot 0 is skipped.

### Start of a player's turn

1. All units owned by the player get fresh AP:
   `currentAP = actionPoints` (of the unit's type and tech level), then if
   `negAP > 0`: `currentAP = actionPoints − negAP` and `negAP = 0`.
   Unused AP is not carried over.
2. The player is sent `CGoTacticMsg` (turn data); the other human players
   get `CGoTacticWaitMsg`. When slot 0 is current, the AI client is told to
   play (`CAItacticMsg`).

At the very start of the battle every unit of every player gets its AP
this way.

### negAP

`negAP` is AP debt caused by AP-damaging weapons (`damageType` = "reduces
action points"). Such damage is taken from `currentAP` first; any excess is
added to `negAP`, capped at the type's `actionPoints`. The debt reduces the
unit's next AP refill (above). While `negAP > 0` the available AP is
`currentAP − negAP` (never below 0). The shipped scenarios have `negAP` = 0
everywhere.

### End of a turn and turn counter

- A human ends the turn with `CRequestEndTactTurnMsg` (accepted only from
  the current player); the AI ends it internally.
- The **turn counter** (`Tactic IO.Current Turn`) starts at 0 and goes up
  by one whenever the turn of slot 0 ends (or slot 0 is skipped). One
  counter step is therefore one full round.
- Then the next player in the order becomes current. Before that the
  server checks whether the battle is over (objectives, see below); this
  check is also done after every move.
- Scripted reinforcements with `turn ≤ T` are placed at the **end** of the
  computer player's turn, before the round counter step (battle-flow.md
  4.1).

### Turn limit

`Scenario Info.turn_qty` is the turn limit. For every objective kind, if
the objective is not decided yet and the counter is above 0 and **greater
than `turn_qty`**, the battle ends; for most objective kinds this means the
attacking (human) side failed *(?: the winner code differs for the
"protect the HQ" kinds)*. The objective rules themselves belong in a
separate document.

## 5. Interruptions during a move

The server walks the accepted path step by step. Before the first step (at
the start tile) and after entering each tile it checks, in this order:

1. **Reaction (overwatch) fire.** At the start of the move the server
   lists enemy units that have `canOverwatch`, at least `actionPointsShoot`
   available AP, and whose zone covers the tile; the zone is the shooter's
   footprint enlarged by its `weaponRange` in every direction (a square).
   The list is only built if the moving unit's type has `Jammer` > 0.
   `Jammer` is a per-cent multiplier on the chance of being hit by reaction
   fire (tactical-combat.md 3.2), with 0 meaning "does not trigger overwatch
   fire" (the manual's icon text): 100 = normal (most units), 10 = jamming
   device (all grav units: Grav Bike, Light / Medium Grav, Paralyzer,
   Seeker), 0 = never triggers reaction fire (Commando, Shocktrooper, Pack
   Freak, Chopper, Submarine, Mind Warrior, Flyer). A watching unit must also
   be an enemy of the mover. If any listed unit could fire at the
   moving unit on this tile (the normal shot check), the move **stops
   here**, and all eligible enemies fire, repeating rounds while someone
   fired and the target still exists. Because the check also runs on the
   start tile, a unit that starts inside a zone is fired upon as soon as it
   tries to move, without moving.
2. **Mine detection.** If the unit's type has `MineDetectionRange` r > 0
   and a mine hidden from its player lies within the square of radius r
   around the tile, the move **stops here**. All hidden mines within r of
   the unit become visible to its player.
3. **Mine trigger.** An armed mine on the tile explodes (move stops) if:
   - ground or trooper: any mine;
   - grav: grav mine only;
   - air, underwater: any mine, but only on the final tile of the path;
   - teleport, grav-fly: grav mine, only on the final tile;
   - boats and immobile classes: never.

   On a shortened path "final tile" means the last tile of the accepted
   path. The `Mine Types` "affects" columns are not consulted for this.

The unit ends on the tile where the move stopped. The AP charged is the
sum of the costs of the steps actually walked. When one unit of a group
order is interrupted, the remaining units of that order do not move.

There is no interruption when an enemy merely comes into view (see 6).

## 6. Visibility

There is **no fog of war for units**: there is no sight-range field, and all
units and buildings are known to every player. The only hidden objects are
mines (`hidden_AI`, `hidden_1` … `hidden_4` per player). A hidden mine is
ignored by path planning and revealed by mine detection (5.2), by
triggering it (5.3), or when it is fired at.

## 7. Message flow of a move order

The client computes and cuts the paths itself (sections 2–3) and sends one
`CRequestUnitMoveMsg` to the server. It carries up to 64 entries; each
entry is a unit id and its path as parallel arrays per step: x, y, step
cost, enterable flag and facing (the step type is named `MoveMapPos`). The
server phase (`CServerPhaseTactic`) handles it only if the sender is the
current player. For each unit in order it re-checks that every step is
still enterable or crossable on the server's map, walks the path with the
checks of section 5, places the unit, charges AP, and adds a movement
record to one multi-message for all clients (`CTacticClientMMsg` holding
`CTMMsgMovement` parts, template `tactic_mmsg_movement`): unit id (32 bit),
AP left after the move (16 bit), step count (16 bit) and room for **75**
steps of (x, y) (16 bit each), followed by records for any reaction fire,
mine reveal or mine explosion. The original copies the walked tiles without
a bounds check; a walk of more than 75 tiles (possible in principle for a
450-AP unit on high-tech roads *(?)*) would overrun the record. OpenDay
simply allows longer lists. In the request the path arrays have no fixed
length; only the number of units (64) is limited. Processing stops at the first unit whose check fails or
whose move is interrupted, or when the battle is over. The clients play the
move animations from these records (`CIsoAnimBegMove`,
`CIsoAnimTranslateMove`, `CIsoAnimEndMove`, see
[unit-animation.md](unit-animation.md)). `CUnitMoveFailedMsg` (no payload)
is **never sent**: only the receiving side exists (if it arrived, the
tactical view would leave its waiting state and play `INVALID`). OpenDay
may drop it or use it for "request rejected". The turn is ended with
`CRequestEndTactTurnMsg`.

## Notes for OpenDay

- The relaxation of 3.1 can be replaced by Dijkstra with edge weight
  `cost(T) + (diagonal ? 1 : 0)`, as long as path extraction (3.3) and its
  tie-break are kept, so that the same path is chosen. (Not quite: the
  update takes the neighbor with the largest value and only then adds the
  diagonal +1, so on ties the result can differ from Dijkstra by 1. OpenDay
  (`game/battle/movement.cpp`) therefore runs the sweeps as described.)
- Keep AP charging (tile cost only, no diagonal +1) separate from the
  planning weight.
- Clamp the AP charge at the available AP for all classes.
- Turn counter and player order: see [battle-flow.md](battle-flow.md) 1.3,
  which supersedes section 4 here. The counter starts at **1** (0 only
  during deployment), the human slots move first in slot order and the
  computer player (slot 0) last; the counter steps when slot 0's turn ends
  (or where it would be, in death match).
- Choices where the original is not known or not followed:
  - The checks of section 5 are only made on tiles the unit can stop on, so
    a move never ends over a fly-over tile.
  - Group replacement tiles are tried orthogonal neighbors first, then the
    diagonals, each in x-then-y order (now confirmed as the original order,
    3.5).
  - The AI's search (3.1) is implemented in `PathMap` (the `aiSearch`
    flag; the discarded random draw is made by the computer player before
    each such search). The border pass of every round still covers the
    whole map border, also with the window *(OpenDay reading)*.
  - The server re-prices the client's path itself and cuts it to the AP.
  - Movement records have no step limit (the original's hold 75 steps,
    section 7).
- Apply the Overwatch reserve (2) in every human move planner; it is purely
  client-side. Implemented as `movementBudget` and used by the two-click
  move, MOVE ALL and the move-to-fire preview of the game program.
- Human group orders (3.5, `planGroupOrders` / `planGroupMove`): no order
  for a clicked tile the unit cannot stop on, replacement only for a
  claimed tile, the last tile of the cut path is claimed, and the
  two-click move repeats its passes while someone got an order (MOVE ALL:
  one pass). The computer player's Goto group keeps the replacement search
  for both cases (`groupTile` without the human flag).
- Move to fire (3.7) is `planMoveToFire`; the shot request of the game
  program gives every selected unit such a path with the group claims.

## Open questions

- (Answered: `Jammer` meaning, 5; the Overwatch reserve and the options, 2;
  `CUnitMoveFailedMsg` is never sent, 7; first player and the AI-active
  flag, battle-flow.md 1.3; movement record capacity 75, 7; replacement-tile
  order, 3.5; reinforcement timing, battle-flow.md 4.1.)
- Whether a walk of more than 75 steps can actually occur in the original.
- The trooper-in-forest flag mentioned in unit-animation.md is set for
  steps of a trooper (class 2) onto forest (class 5); it matches the
  forest standing pose of map-objects.md, but its effect during the move
  is still untraced.
