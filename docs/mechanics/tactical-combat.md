# Tactical combat: targeting, hit chance, damage, destruction

How the original game resolves an attack on the tactical map: which targets
are legal, what a shot costs, how the hit is rolled, how much damage it does
to which objects, what remains of destroyed objects, how reaction
(overwatch) fire, automatic repeat fire and mines work, and which network
messages carry all of this.

Prerequisites: the tables in [data-model.md](data-model.md), the object
rules in [map-objects.md](map-objects.md), the attack and explosion
animations in [unit-animation.md](unit-animation.md), and the movement,
AP and turn rules in [tactical-movement.md](tactical-movement.md) (which
also describes how a move is interrupted by overwatch and mines; that part is
only summarized here).

Status: the rules come from studying the program's behavior and are
described in our own words. They are precise enough to reimplement, but they
have not yet been checked against recorded games. Items marked *(?)* are
unconfirmed interpretations.

## 0. Conventions

- **Tile distance** `d(a, b)` = Chebyshev distance
  `max(|ax − bx|, |ay − by|)` on the square map grid. Every range in combat
  uses this measure, so the reach of a weapon is a square.
- **Footprint**: the rectangle of tiles a unit or building covers.
  For units it is `SizeX × SizeY` from `Unit Types` (almost always 1 × 1).
  **Center tile** of a footprint = `(left + (SizeX − 1) div 2,
  top + (SizeY − 1) div 2)` (integer division, rounding down).
- **Available AP** = `max(0, currentAP − negAP)` (same as in
  tactical-movement.md).
- **Rules row**: all weapon stats are taken from the `Unit Types` row that
  matches the unit's `typeID` *and* current tech level (`unitTL`). The tech
  level has no other effect on combat: there is no extra tech-level formula,
  higher levels are simply better rows.
- **Random numbers**: every random decision is made by the **server** with
  the C library generator, as `rand() mod n`. The clients only replay the
  results. OpenDay may use its own generator. The order of draws per shot is
  listed in §3.4.
- **Owner** `0` = AI, `1`–`4` = human players.
- Unit type columns used below: `UnitHasWeapon`, `weaponType`
  (0 direct, 1 indirect), `weaponRange`, `weaponMinimumRange`,
  `HitShortRange` / `HitMediumRange` / `HitLongRange`, `actionPointsShoot`,
  `damageType` (0 = reduces armor, 1 = reduces AP), `damageBase`,
  `damageRandom`, `DamageArea`, `IsAA`, `DamageAA`, `canOverwatch`,
  `Armor` (maximum armor = hit points), `Jammer`, `MineDetectionRange`,
  `ruinImageID`.

## 1. Target legality

A unit (the **shooter**) may fire at a **target tile** `T`. A shot request
is checked in this order. The first failing check decides the result.

| # | Check | Failure result |
|---|---|---|
| 1 | `T` is inside the map and **not** inside the shooter's own footprint. | invalid target |
| 2 | `UnitHasWeapon` is true. | "no weapon" |
| 3 | Available AP ≥ `actionPointsShoot`. | "not enough AP" |
| 4 | Side check (human shooters only, see 1.1). | invalid target |
| 5 | Range and line of fire for at least one **aim tile** (see 1.2–1.4). | "out of range", "too close" or "no line of fire" |

If all checks pass, the attack is legal and the chosen aim tile is
remembered (§1.2).

### 1.1 Side check

Only applied when the shooter is owned by a human player (owner ≠ 0). AI
shooters skip it completely.

- If a **building** stands on `T`, it must be an **enemy** of the shooter's
  player (relation below). Otherwise the target is invalid.
- Else, if a **unit** stands on `T`, it must be an enemy of the shooter.
- An **empty tile** is always allowed. Players can fire at empty ground,
  walls, roads and mines.

**Unit–unit relation** (result: same side, ally, enemy). "Coop" is true in
every game mode except one (the mode the program numbers 3, presumably the
death-match / free-for-all mode *(?)*):

1. Neither unit is a convoy (`isConvoy`):
   - same owner → same side;
   - one of them is AI-owned → enemy;
   - two different human players → ally if coop, else enemy.
2. Convoy units belong to their race rather than to a player:
   - an **alien** (race ≠ human) convoy is on the AI's side: same side as AI
     units, enemy of every human unit;
   - a **human** convoy is an enemy of AI units and an ally of human units
     (enemy if not coop);
   - two convoys: allies if both are human convoys, otherwise enemies.

**Player–building relation**: the building's own owner is compared with the
shooter's owner. Same owner → same side; another human player → ally if
coop, else enemy; AI-owned building → enemy of human players. (For the AI
itself an AI-owned building only counts as a target when its
`IsAiTarget` flag is set; this matters for AI target selection, not for the
legality of AI shots.)

### 1.2 Aim tiles

- If a **building** stands on `T`, every tile of the building's footprint is
  a candidate aim tile. They are tried column by column (PosX ascending,
  and within a column PosY ascending). The **first** tile that passes
  range and line of fire becomes the aim tile, so the shot may land on a
  different tile of the building than the one clicked.
- Otherwise the only candidate is `T` itself.

If no candidate passes, the failure reported is: "out of range" if some
candidate was beyond the maximum range, otherwise "too close" if some
candidate was inside the minimum range, otherwise "no line of fire".

### 1.3 Range

Measured from the shooter's **center tile** `S` to the candidate tile `A`:

    legal range  ⇔  weaponMinimumRange < d(S, A) ≤ weaponRange

The minimum range is **exclusive**: a weapon with `weaponMinimumRange` 3
can fire at distance 4 and more. With minimum range 0 every tile except the
shooter's own is far enough.

### 1.4 Line of fire

**Indirect weapons** (`weaponType` = 1) ignore the line of fire. Only range
matters.

**Direct weapons**: trace a grid line from `S` to `A` (below) and look at
every tile of the line **except the first and the last** (the shooter's tile
and the aim tile itself are never checked). The shot is blocked if any
intermediate tile has:

| On the tile | Blocks when |
|---|---|
| terrain | `Terrain Types.can_fire_through` is false (in the shipped data: most mountain and forest tiles, a few plain tiles) |
| ruin | `Ruin Types.can_fire_through` is false (no shipped ruin type has this, so ruins never block in practice) |
| building, 1 × 1 | always |
| building, larger | always, **except** when the aim tile is one of that same building's three corner tiles other than its top-left one, i.e. `(right, top)`, `(left, bottom)` or `(right, bottom)` of its footprint *(?: probably meant to let a shot cross the near part of a big building to reach its far corner)* |
| unit | always: any unit, friend or foe |
| wall | always: every wall type. `Wall Types.BlockShots` is **not read** by the program (all shipped types have it set anyway) |
| road, mine | never |

**Line tracing** (integer Bresenham, symmetric by construction):

1. Let `P` be the end point with the smaller PosY and `Q` the other one (if
   both have the same PosY, `P` = `S`). The line is always generated from
   `P` to `Q` and then read in shooter → target order.
2. `dy = Qy − Py` (≥ 0), `dx = Qx − Px`, `sx` = +1 if `dx` > 0, else −1,
   `ax = |dx|`. The line has `max(ax, dy) + 1` tiles.
3. If `ax ≤ dy` (Y-major, includes the exact diagonal): start with
   `e = 2·ax − dy`. Each of the `dy` steps does `y += 1`, and if `e ≥ 0`
   also `x += sx` and `e += 2·ax − 2·dy`, otherwise `e += 2·ax`.
4. Otherwise (X-major): start with `e = 2·dy − ax`. Each of the `ax` steps
   does `x += sx`, and if `e ≥ 0` also `y += 1` and `e += 2·dy − 2·ax`,
   otherwise `e += 2·dy`.

Because the line is always drawn from the lower-Y end, A→B and B→A give the
same tiles.

### 1.5 Air units and AA

- Units with `UnitHasWeapon` = false cannot fire at map tiles at all. The AA
  vehicles and AA towers are such units. They only take part in anti-air
  fire against **automated units** (§6.3).
- No check limits which movement classes can be targeted. Choppers and
  other flying *units* can be fired at by any weapon like ground units
  *(?: no restriction was found; to be confirmed in play)*. Their
  `Jammer` = 0 makes them immune to reaction fire, though (§6.1).

## 2. AP cost and multiple shots

- One shot costs `actionPointsShoot`. It is checked before the shot
  (legality check 3) and deducted after the hit roll. A unit can fire as long
  as its available AP is at least the cost, so `n` shots per turn
  = `floor(available AP / actionPointsShoot)` (e.g. towers with 2–5 AP and
  cost 1 fire 2–5 times).
- With insufficient AP the shot is refused ("not enough AP"). Nothing is
  spent.
- **Move then shoot.** A shot request may carry a path for each shooter. If
  the target is not legal from the current tile and a path is given, the
  unit first walks the path (with all the normal movement rules, overwatch
  and mine interruptions). If it arrives, the target is checked again and the
  shot is fired if it is now legal.
- **Several shooters.** One request may list several units shooting at the
  same tile. They fire one after another in list order. When the target
  object no longer exists, the remaining units do not fire.
- **Autofire** (the "Autofire" toggle of the tactical screen, default from
  `LIBDAY.INI [User] Autofire`): the request carries an autofire flag. With
  it, each unit keeps firing at the tile until one of these happens:
  - the shooter no longer exists (or the game ended);
  - the shooter's available AP drops below `actionPointsShoot`;
  - for an armor weapon (`damageType` 0): the object that was on the target
    tile is gone (destroyed);
  - for an AP weapon (`damageType` 1): there is no unit on the target tile
    any more, or that unit has 0 available AP left.

  Without the flag each listed unit fires exactly once.

## 3. Hit chance

### 3.1 Distance band

`r = d(shooter's top-left tile, aim tile)`. (For 1 × 1 units this is the
same as the range distance. For the few larger units it is measured from the
top-left tile rather than from the center *(?: probably an oversight in the
original)*.)

- **`r ≤ 1` (adjacent): the shot always hits**. No roll is made.
- Otherwise the band comes from the `Ranges` table: `r ≤ Short` → short,
  `r ≤ Medium` → medium, otherwise long (shipped values Short 3, Medium 6).
  `r` < 1 is impossible here.
- Base chance `C` = `HitShortRange`, `HitMediumRange` or `HitLongRange` of
  the **shooter's** type. Values above 100 occur (laser towers 200/190/170)
  and simply mean "always hits".

### 3.2 Modifiers

There is exactly **one** modifier:

- **Jammer.** If a unit stands on the aim tile **and that unit belongs to the
  player whose turn it is**, `C = trunc(C × Jammer / 100)` with the target
  unit type's `Jammer`. In ordinary fire the target belongs to the
  non-moving side, so this only affects **reaction (overwatch) fire** at a
  unit that moves in its own turn. `Jammer` is a percentage on the chance
  of being hit by reaction fire (the manual: "jamming device"): 100 =
  normal (most units), 10 = all grav units (Grav Bike, Light / Medium Grav,
  Paralyzer, Seeker), 0 = "does not trigger enemy overwatch fire"
  (Commando, Shocktrooper, Pack Freak, Chopper, Submarine, Mind Warrior,
  Flyer; these are never stopped by overwatch, §6.1, so the modifier never
  applies to them). The product is computed in floating point and
  truncated. Adjacent shots (§3.1) make no roll, so `Jammer` does not
  matter there.

`Jammer` is read only here, by the watch zones (§6.1), by the computer
player's free-list order (tactical-ai.md 1.3) and by the ability icons of
the info card and the encyclopedia (tactical-interface.md 2.7). It is
**not** part of the shot legality check, the AI's firing-position test or
target score, movement or visibility.

Terrain, forest cover, facing, elevation, tech level and difficulty have
**no** influence on the hit chance.

### 3.3 Roll and miss scatter

1. Draw `R = rand() mod 100` (0…99). **Hit if `R ≤ C`**. So the real
   probability is `(C + 1) %` for `C` < 100. A displayed 65 % hits 66 times
   in 100.
2. On a **miss**, draw `k = rand() mod 8` and move the impact one tile in
   direction `k`:

   | k | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
   |---|---|---|---|---|---|---|---|---|
   | (ΔPosX, ΔPosY) | (0, +1) | (−1, +1) | (−1, 0) | (−1, −1) | (0, −1) | (+1, −1) | (+1, 0) | (+1, +1) |

   If that tile is outside the map, the impact stays on the aim tile (the
   miss becomes a hit).
3. The **impact tile** (aim tile on a hit, neighbor on a miss) is where the
   damage is applied (§4). A miss is not harmless: the shell still lands,
   damages whatever is on the neighbor tile, and with `DamageArea` > 1 can
   still catch the original target.

### 3.4 Order of random draws per shot

1. hit roll `rand() mod 100` (only if `r` > 1);
2. scatter direction `rand() mod 8` (only on a miss);
3. damage roll (§4.1, always made, even when its range has one value);
4. further draws only if a building is destroyed (ruin placement, §5.2).

## 4. Damage

### 4.1 Damage roll

    lo = damageBase
    hi = damageBase + damageRandom
    (AI shooters in campaign/skirmish modes: lo, hi scaled, see 4.2)
    span = hi − lo, or 1 if that is 0
    D = lo + 1 + (rand() mod span)

So `D` ranges over `damageBase + 1 … damageBase + damageRandom`, uniformly.
When `damageRandom` is 0, `D = damageBase + 1`. (The "+1" is how the
original does it. Keep it for faithful numbers.)

### 4.2 Difficulty

- Only `Difficulty.DamageEasy` and `Difficulty.DamageHard` are read.
  `DamageAvg` is never read: the average value results from interpolation.
- The difficulty setting `L` is the `Difficulty` value of the running game
  (`Server Mode Info`, GAMEINFO), 0–10: the slider of the new-campaign menu
  (strategic-interface.md 6), initially 4.
- Factor `F = DamageEasy + ((DamageHard − DamageEasy) × L) div 10`
  (percent; 70…130 with the shipped data).
- It applies **only to shots fired by AI-owned units**, and only in the two
  game modes the program numbers 0 and 1: a new and a loaded **campaign**
  (all campaign battles, campaign.md 4.1). Skirmish and death match are not
  scaled. There `lo = trunc(lo × F / 100)` and
  `hi = trunc(hi × F / 100)` before the roll. Human players' damage is never
  scaled. Higher difficulty makes the AI hit harder. It does not weaken the
  player.
- Mines, and the follow-on damage of destroyed objects (§5), are not
  scaled.

### 4.3 Area of effect

`DamageArea` = `a` (1, 3 or 5 in the data). The affected tiles are the
square

    left = Ix − a div 2,  top = Iy − a div 2,  size a × a

around the impact tile `I`, clipped to the map. Each tile gets

    k = (a + 1) div 2
    D_tile = D − (D div k) × d(I, tile)

So `a` = 1: only the impact tile, full damage. `a` = 3: center full, ring
`D − D div 2`. `a` = 5: rings `D − D div 3` and `D − 2·(D div 3)`. Area
damage hits **everything** on those tiles, own units included (friendly
fire). The damage type is the weapon's `damageType` on every tile.

### 4.4 What one tile takes

For every affected tile the server processes the tile's contents in this
fixed order, with that tile's damage `D_tile` and the weapon's damage type.
Each step can add a "something changed here" mark to the tile, which later
steps look at.

1. **Mine.** Any mine on the tile is **destroyed**, whatever the damage and
   damage type (even 0). There is no explosion damage from it. Marks the
   tile.
2. **Building** (any tile of its footprint):
   - not damaged if the type has `isDestructible` = false or if the damage
     type is AP;
   - otherwise `currentAR −= D_tile` (not below 0); destroyed at 0 (§5.2).
   Note that a large building in a blast area loses `D_tile` for **every**
   footprint tile inside the area: each tile is processed on its own.
3. **Wall**: AP damage has no effect. Otherwise `currentAR −= D_tile`
   (not below 0); destroyed at 0 (removed from the map). The damaged sprite
   state follows from the new `currentAR` (see map-objects.md).
4. **Unit**:
   - armor damage: `currentAR −= D_tile` (not below 0); destroyed at 0
     (§5.1);
   - AP damage: if `currentAP ≥ D_tile`: `currentAP −= D_tile`. Otherwise
     the remainder `D_tile − currentAP` is added to `negAP` (capped at the
     type's `actionPoints`) and `currentAP` becomes 0. AP damage never
     destroys a unit. How `negAP` is paid back at the next turn is in
     tactical-movement.md.
5. **Road**: it is only considered if no unit is on the tile, **or** the
   road is a bridge (the terrain under it is deep water), **or** a mine or a
   unit was destroyed on this tile in steps 1/4. Then it is destroyed if the
   damage type is armor and `D_tile ≥ Road Types.ar` (a one-hit threshold;
   roads have no hit points: paved 10, basic 999, i.e. practically
   indestructible). A destroyed **bridge** with a unit on it takes the unit
   with it: the unit gets armor damage equal to its type's full `Armor`,
   which always destroys it.
6. **Terrain burning**: the terrain tile changes to its `burned_id` sprite
   if all of these hold:
   - the tile has no building, unit, wall, road or mine on it, **or** the
     tile was marked by an earlier step (something was destroyed or changed
     on it by this hit);
   - the damage type is armor;
   - the terrain type has a non-empty `burned_id`.

   The program does **not** read `Terrain Types.burned_min`. The threshold
   it compares the damage against is always 0 (2000 for tiles without a
   burned variant), so every armor hit burns burnable terrain, even a hit
   whose falloff damage is 0. Burnable tiles in the data are plain,
   mountain and forest tiles. Burned tiles can still be burned again only if
   their own `burned_id` is set.

### 4.5 Plausibility check against the data

TL0 rows, `D` range = `base+1 … base+random`, armor = `Armor`:

| Shooter → target | `D` | target armor | hits to kill |
|---|---|---|---|
| Trooper (3+1) → Trooper | 4 | 5 | 2 |
| Trooper → Main Tank | 4 | 50 | 13 |
| Light Tank (15+5) → LAV | 16–20 | 20 | 1–2 |
| Main Tank (30+15) → Main Tank | 31–45 | 50 | 2 |
| Heavy Tank (80+20) → Heavy Tank | 81–100 | 100 | 1–2 (2 almost always) |
| Laser Tower (15+10) → Grav Bike | 16–25 | 10 | 1 |
| Artillery (20+5, area 5) → 3 troopers in a row | 21–25 centre, ≥ 14 ring 1 | 5 | all three |

Typical duels end in 2–3 hits. Hit chances of 50–90 % with 1–4 shots per turn
(AP / shot cost) give fights that last a few turns, which matches how the
game plays. With AI damage at 130 % on the hardest setting, an AI Main Tank
does 40–58 and kills an equal Main Tank in 1–2 hits.

## 5. Destruction

### 5.1 Units

- Removed from the map. Kill/loss statistics are recorded for both players
  (attacker slot, victim's owner, victim type); who gets the kill for each
  damage source (shot, mine, automated unit, bridge collapse) and the
  death-match rule are in battle-flow.md 5.3.
- **Wreck**: if the type's `ruinImageID` is not empty (the placeholder value
  `UBRUIXXX` counts as empty), the tile's terrain class is **plain** and no
  ruin lies there yet, a ruin object with that sprite (`RUINS.DBI`) is placed
  on the tile. On water, forest, mountains etc. no wreck is left.
- The destruction message carries the wreck name (or nothing). The client
  plays the `E` explosion and then places the wreck (unit-animation.md).
- **Chain effect on the same tile only**: the remaining steps for this tile
  (road, terrain) are processed with the damage replaced by the destroyed
  unit's full `Armor`. So a destroyed vehicle with armor ≥ 10 also wrecks a
  paved road under it, and the tile burns. Neighbor tiles are **not**
  affected: there is no splash explosion.

### 5.2 Buildings

- Removed from the map. Destroyer/loser statistics are recorded
  (battle-flow.md 5.3), the building's `TechReward` is credited *(?: the field is read here; to whom
  and how is not traced)*.
- **Rubble** (only if `hasRuin`): collect the footprint tiles whose terrain
  class is plain and that have no ruin yet, shuffle them, and place
  `n = clamp(SizeX × SizeY div 2, 1, 7)` rubble objects on the first `n`
  of them *(?: behavior when fewer than `n` tiles qualify is not confirmed;
  assume "as many as there are")*. Each one is a random building-rubble
  sprite of the building's race: `B` + race letter (`H`, `T`, `B`, `M` for
  race 0–3) + `RUI` + a uniformly random 3-digit index
  `0 … (number of such sprites − 1)`.
- Every footprint tile then goes through the **terrain burning** step with
  armor damage equal to the building type's `Armor`. The whole footprint
  burns where burnable.
- No damage to neighbors. The destruction message lists the rubble
  (tile + sprite name per entry). The client plays the building explosion
  (unit-animation.md).

### 5.3 Walls, roads, mines

Removed from the map with their own destruction message. Walls leave no
rubble: the "Ruined Walls" wall type is a separate, pre-placed type, and a
wall is not turned into one. Neighboring walls and roads are redrawn with new
connection variants (map-objects.md).

## 6. Reaction fire, overwatch toggle, AA

### 6.1 Reaction (overwatch) fire

Reaction fire is automatic. There is no "put unit on overwatch" order.

- **Eligible reacting units**: enemies of the moving unit (relation as in
  §1.1, coop per game mode) with `canOverwatch` and available AP
  ≥ `actionPointsShoot`. Nearly every armed unit, and the AA units too, have
  `canOverwatch` in the shipped data.
- **Watch zones** are built when a move starts, and only if the moving
  unit's `Jammer` > 0: for each eligible unit, its footprint grown by
  `weaponRange` on every side.
- **Trigger**: the moving unit's tile is checked before its first step and
  after every step. The move **stops** on the first tile that lies inside a
  zone **and** from which that zone's owner could legally shoot it (full §1
  check: AP, minimum range, line of fire).
- **Volley**: then *every* eligible enemy unit (not only the one whose zone
  triggered), in map list order, that can legally target the mover's tile
  fires one ordinary shot at it (§3 with the Jammer modifier, §4). This
  repeats in rounds as long as at least one unit fired in the previous round
  and the mover still exists. So reacting units keep firing until they run
  out of AP or the mover is destroyed. Each shot costs normal shooting AP.
- Mover's AP for the steps already taken is spent. The rest of the path is
  dropped and the player must re-issue the order (tactical-movement.md).

### 6.2 The "Overwatch" toggle (`LIBDAY.INI [User] Overwatch`)

A purely **client-side** planning option. It does not change the server
rules above. When it is on, every human move planner of the tactical screen
(two-click move, MOVE ALL, the path preview of a move-to-fire click) holds
back one shot's worth of AP for units with `canOverwatch` (if their
available AP is at least `actionPointsShoot`, the AP usable for movement is
reduced by that amount; exact rule in tactical-movement.md 2, "Keeping AP
for a shot"). The unit therefore keeps enough AP to react during the enemy
turn. The shot request itself (move-to-fire) does not use this reserve.
The INI value is read at start-up and written back whenever the lamp is
switched; it sets the lamp's state when a tactical view opens.

`Autofire` (`[User] Autofire`) is stored the same way. It only sets the
autofire flag of shot requests (§2); it does not change how the client
chooses between a direct shot and move-to-fire, and it has no effect on
reaction fire.

### 6.3 Anti-air fire against automated units

Used when an automated unit (bomber, transport, …) is called in. The full
rules are in [automated-units.md](automated-units.md) 5; in short:

1. Candidates: units that are enemies of the automated unit's owner, have
   `IsAA`, `canOverwatch` and available AP ≥ `actionPointsShoot`, and whose
   footprint grown by `weaponRange` contains the call's **target tile**
   (for a transport: once the source, once the destination).
   `AAPosX/AAPosY` and `FrameAA` are purely visual (where the AA units aim
   on screen, and when they fire).
2. Sort them by average weapon damage `(lo + hi) div 2` (ascending, weakest
   first), ties by lower `actionPointsShoot` first, then map order.
3. Add `DamageAA` of the candidates in that order until the sum reaches the
   automated unit's `armor` property (`Automated Units Infos`). Candidates
   after that point do not fire. There is **no hit roll**.
4. Outcome: sum 0 → OK (1), 0 < sum < armor → damaged (2),
   sum ≥ armor → destroyed (3). The outcome selects the clip
   `AUTOUNIT\<code>0<outcome>` (the `Animation…_Begin/_Qty` columns are not
   used), and a damaged automated unit has a reduced effect.
5. Each unit that fired pays `actionPointsShoot`. One `CTMMsgUnitAA` per
   firing unit reports it.

## 7. Mines

- **Data**: `Mines On Map.armed`, owner, and the visibility flags
  `hidden_AI`, `hidden_1` … `hidden_4` (true = that player does not know
  about the mine). The shipped scenarios place AI mines that are visible to
  the AI and hidden from all human players.
- **Trigger** (during movement; details in tactical-movement.md): an armed
  mine on the tile entered explodes for ground and trooper units (any mine),
  grav units (grav mine only), air and underwater units (any mine, only on
  their final path tile), teleport and grav-fly units (grav mine only, only
  on the final tile). Boats are never affected. The program decides by
  movement class and mine type. The `Mine Types.affectUnit_*` columns are
  loaded but this decision does not use them *(?)*.
- **Explosion**: no hit roll. Damage `D` from `damageBase` / `damageRandom`
  of the mine type with the §4.1 formula (no difficulty scaling), area 1, the
  mine type's `damageType`, attributed to the mine's owner. It is applied to
  the mine's tile with the normal §4.4 steps. The mine itself is destroyed in
  step 1, the unit takes the damage, roads and terrain may follow. Then every
  mine on the 8 neighbor tiles becomes **visible** to the moving unit's
  player. The move stops.
- **Detection**: a moving unit whose type has `MineDetectionRange` `m` > 0
  (all infantry in the data, `m` = 1) reveals all mines hidden from its
  player within the square of radius `m` around its footprint and stops
  there.
- **Shooting at mines**: a mine on any tile reached by a shot (impact or
  area) is destroyed without exploding (§4.4 step 1). Hidden mines are found
  this way too, by accident.
- **Line of fire**: mines never block shots.

## 8. Message flow of an attack

The client sends **`CRequestUnitShootMsg`** to the server's tactical phase
(`CServerPhaseTactic`, through a `CNetMsgHandler_template<…>`). It carries
the same body as a move request, i.e. a list of up to 64 unit ids, each with
an optional path (per step: tile and step data), plus the **target tile**
and the **autofire flag**. The server checks the sender is the current
player, runs the per-unit logic above (move, reaction fire, legality, roll,
damage) and collects the results as a list of small tactical sub-messages
(`CTemplTacticMMsg<…>` kinds, gathered in a `CTacticMMsgList`). The list is
broadcast to all clients in one `CMultiNetMsg`. The sub-messages, in the
order of events:

- `CTMMsgMovement`: walked path;
- `CTMMsgShooting`: shooter id, its available AP after the shot, its
  facing and tile, and the impact tile of each shot. Consecutive shots of
  the same unit (autofire, several shots) are merged into one message with a
  shot count and a list of impact tiles;
- `CTMMsgDmgUnit`: unit id, new `currentAR`, new available AP;
- `CTMMsgDmgBuilding`, `CTMMsgDmgWall`: object and new armor;
- `CTMMsgDtrUnit`: unit id + wreck sprite name (8 characters, may be empty);
- `CTMMsgDtrBuilding`: building id + list of (tile, rubble sprite);
- `CTMMsgDtrWall`, `CTMMsgDtrRoad`, `CTMMsgDtrMine`: tile;
- `CTMMsgBurnedTerrain`: tile + new terrain sprite;
- `CTMMsgMineDetect`, `CTMMsgMineExplosion`: mine tile (+ player who now
  sees it);
- `CTMMsgUnitAA`, `CTMMsgAutoUnit`: AA fire and automated-unit outcome;
- `CTMMsgTech`: control changes of tech buildings, re-evaluated after each
  action.

The damage messages refer to the shooting message they belong to, so the
client can play them at the impact of the matching projectile
(unit-animation.md). The clients do not compute any combat result
themselves. The AI's own actions use the same server code (`CAItacticMsg`,
and the AI action classes such as `CAIaction_shoot_overwatch` *(?)*).

## Notes for OpenDay

Implemented in `game/battle/` (`combat.*` for the checks, `server.*` for
the resolution). Choices and differences:

- The shooter's own footprint never blocks its line of fire (relevant for
  the 3 × 3 towers, whose center tile is inside their footprint).
- One `UnitShot` event per shot instead of merged shot records; the client
  plays aim, shot and recoil for each.
- The "something changed here" mark of 4.4 is set when a mine, building,
  wall, unit or road on the tile is destroyed.
- Damage with a value of 0 or less is still applied (it changes nothing)
  and still burns the tile, as described.
- Anti-air fire against automated units (6.3) is implemented with the
  automated units (automated-units.md).
- Not implemented yet: the `TechReward` of destroyed buildings, statistics (now specified in
  battle-flow.md 5.3), objectives, "move to fire"
  orders of the AI. (Damaged building sprites and mine explosion clips are
  drawn, map-objects.md and unit-animation.md implementation notes.)
- Game options: `coop` true, AI damage scaling on, difficulty 5 (average)
  by default.

## 9. Open questions

- Exact meaning of game modes 0, 1, 3 (which modes get the AI difficulty
  damage factor and which one disables coop between human players), and the
  scale and menu values of `Server Mode Info.Difficulty`.
- Whether flying units (`Movement` air / grav-fly) are meant to be hittable
  by ordinary weapons. No rule prevents it.
- Rubble placement when a building has fewer qualifying plain tiles than
  `n`.
- Whether `TechReward` of a destroyed building goes to the destroyer.
- The unused columns: `Terrain Types.burned_min`, `Wall Types.BlockShots`,
  `Mine Types.affectUnit_*`, `Difficulty.DamageAvg`. Confirm in play that
  they really have no effect.
- How the client shows hit chances (if at all). (The errors are known: a
  failed legality check is reported in the message strip for 5 s with
  string 320 invalid target, 321 no weapon, 322 not enough AP, 323 out of
  range, 325 too close, 324 no line of fire; tactical-interface.md 3.2.)
- (Answered: AA is checked at the target tile, and the damage of
  "destroy"-type automated units uses §4.3–4.4 with the properties of
  `Automated Units Infos`; see automated-units.md 5 and 6.1.)
