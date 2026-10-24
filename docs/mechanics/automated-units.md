# Automated units (air strikes, drops, transport, bridge, mine detection, medic)

How the original game handles **automated units**: support actions a player
(or the computer player) calls in during the tactical battle instead of
moving a unit on the map. Bombers and fighters, the nuke and the gene bomb,
paratrooper and Zeppelin drops, transport helicopters, bridge builders, mine
detection and medic helicopters are all automated units. This document
covers their data, which ones a player may use, the targeting interface,
the request, the server's validation and cost, the anti-aircraft fire, the
effect of each category, the messages and the client animation.

Prerequisites: [data-model.md](data-model.md) (tables `Automated Units`,
`Automated Units Infos`, `Automated Units Properties`), [tactical-combat.md](tactical-combat.md)
(side relation 1.1, damage roll 4.1, area and per-tile processing 4.3–4.4,
mines 7), [tactical-movement.md](tactical-movement.md) (tile evaluation 1),
[tactical-interface.md](tactical-interface.md) 6.2 (the `CAutomatView`
panel), [battle-flow.md](battle-flow.md) (objectives, tech buildings 5.2),
[city-building.md](city-building.md) 4.3 and 8 (bridge sites, what the
buildings unlock), [unit-animation.md](unit-animation.md) (clip placement,
timing), [tactical-ai.md](tactical-ai.md) 6 (calls by the computer player).

Status: the rules come from studying the program's behaviour and are
written in our own words. They have not been checked against recorded
games. Items marked *(?)* are inferred, not confirmed.

## 0. Terms

| Term | Meaning |
|---|---|
| type | a row of `Automated Units`, identified by `typeID` (0–26 in the shipped data) |
| category | `Automated Units.Category`: 0 destroy, 1 repair, 2 build, 3 mine, 4 drop, 5 transport (`Automated Category Lookup`) |
| caller / owner | the player slot that calls the automated unit: 1–4 for humans, 0 for the computer player |
| target tile | the tile clicked by the player (first point); for a transport the first point is the **source** (the unit to pick up) and the second point the **destination** |
| tactical resource | the player's per-battle budget that also pays for bought reinforcements (battle-flow.md 4.2) |
| armor | the automated unit's hit points against AA fire (property `armor`) |
| `acc` | the AA damage the automated unit has taken during this call (starts at 0) |
| property `p` | the value of property `p` in `Automated Units Infos` for the type, parsed as an integer; a missing property counts as 0 (and as an empty string where text is used) |

All "trunc" operations truncate toward zero. Fractions like
`acc / armor` are computed as real numbers.

## 1. Data

### 1.1 `Automated Units`

One row per type, loaded in table order; the shipped table is ordered by
`typeID`, so "the n-th type" and "type n" are the same thing.

| Column | Use |
|---|---|
| `typeID` | type id |
| `Category` | category (0–5), decides the effect (§6) and the target test (§3.2) |
| `Name<language>` | name shown in the list |
| `facingsImageID` | clip code in `ANMSUNIT.FF\AUTOUNIT` (§8) and list icon code |
| `Race` | race of the type. **This column is authoritative**, not the race in `AUTOMATED TYPE ID LOOKUP`; the two differ for type 10 (`T_Heli_Medic`: lookup 0, table 1). The value matters for the computer player's filter (§9). |
| `cursorID` | targeting cursor in `SYMBOLS.DBI` (`UA…`) |
| `Explain<language>` | one-line instruction shown when the type is selected (§3.1) |
| `Prerequisite` | building `TypeID` that makes the type usable (§2) |
| `Cost` | price in tactical resource |
| `DeployOnce` | the type can be used only once per battle (§2.4) |
| `AnimationOK_Begin/_Qty`, `AnimationDamaged_Begin/_Qty`, `AnimationDestroyed_Begin/_Qty` | loaded, but **not used**: each outcome has its own clip, which is played in full (§8) |
| `FrameAA` | 1-based clip frame at which the AA units fire (§8.2) |
| `FrameAction` | 1-based clip frame at which the effect becomes visible (§8.2) |
| `AAPosX`, `AAPosY` | screen point (in 1024-wide coordinates) at which the AA units aim; **purely visual**, it plays no part in which units fire (§5.2, §8.4) |
| `Encyclopedia<language>`, `EncyclopediaFrame` | encyclopedia |

Frame numbers in the table are 1-based. The program stores them minus one;
a value of −1 ("none") stays −1.

### 1.2 Properties (`Automated Units Infos`)

`Automated Units Infos` holds (`typeID`, property enum, value as text). The
property enum is `Automated Units Properties`:

| Enum | Name | Used by | Meaning |
|---|---|---|---|
| 0 | `armor` | all categories | hit points against AA fire (§5) |
| 1 | `damage` | destroy | base damage (§6.1) |
| 2 | `damageType` | destroy | text; `AR` (compared case-insensitively) = armor damage, anything else = AP damage |
| 3 | `mineDetectionRange` | mine | radius of the revealed square (§6.4) |
| 4 | `damageArea` | destroy, repair | side of the affected square (§6.1, §6.2) |
| 5 | `unitIDDrop` | drop | `Unit Types.typeID` of the dropped units |
| 6 | `unitTLDrop` | — | **not read**; the tech level comes from the caller (§6.5) |
| 7 | `nbDrop` | drop | number of dropped units |
| 8–17 | `unitPosX1`, `unitPosY1`, …, `unitPosX5`, `unitPosY5` | drop | tile offset of drop slot i: X = property `8 + 2(i−1)`, Y = property `9 + 2(i−1)` |
| 18–23 | `SecondanimOK_Begin` … `SecondanimDestroyed_Qty` | — | read, not used (like the main `…_Begin/_Qty`) |
| 24, 25 | `Secondanim_AAPosX`, `Secondanim_AAPosY` | client | AA aim point of the second animation |
| 26 | `SecondanimFrame_aa` | client | AA frame of the second animation (1-based) |
| 27 | `SecondanimFrame_action` | client | action frame of the second animation (1-based) |
| 28 | `armorInc` | repair | armor restored at the centre |
| 29–33 | `facing1` … `facing5` | drop | facing of the unit in drop slot i (property `28 + i`) |
| 34 | `SecondExplain` | client | instruction after the first point of a transport; the data has one text only (English), shown in every language *(?)* |
| 35 | `actionInc` | repair | AP restored at the centre |

### 1.3 Shipped types (summary of GLOBAL.MDB)

Race: 0 human, 1 Tauran, 2 Shubblar, 3 Shadamar. "Once" = `DeployOnce`.
AA position as (`AAPosX`, `AAPosY`).

| id | symbol | name | race | cat | cost | prereq | once | clip | cursor | AA pos | `FrameAA` | `FrameAction` |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 0 | H_Heli_Transport | Transport | 0 | 5 | 30 | 150 | – | HHTR | UATRANSP | 479, 434 | 30 | 38 |
| 1 | H_Engineer_Bridge | Bridge | 0 | 2 | 10 | 151 | – | HBDG | UABRIDGE | 479, 400 | 28 | 54 |
| 2 | H_Engineer_Mine | Mine | 0 | 3 | 10 | 151 | – | HMIN | UAMINE | 479, 434 | 38 | 53 |
| 3 | H_Heli_Medic | Armor | 0 | 1 | 100 | 153 | yes | HHMA | UAMEDIC | 479, 434 | 38 | 53 |
| 4 | H_Bomber_Bomb | Bomber | 0 | 0 | 150 | 160 | – | HBMB | UABOMB | 581, 344 | 16 | 35 |
| 5 | H_Bomber_Gas | Gas | 0 | 0 | 40 | 161 | – | HGAS | UAGAS | 581, 344 | 16 | 35 |
| 6 | H_Nuke | Nuke | 0 | 0 | 500 | 138 | – | HNKE | UANUKE | 420, 220 | 2 | 28 |
| 7 | T_Heli_Transport | Transport | 1 | 5 | 40 | 250 | – | THTR | UATRANSP | 450, 433 | 25 | 31 |
| 8 | T_Engineer_Bridge | Bridge | 1 | 2 | 15 | 250 | – | TBDG | UABRIDGE | 479, 434 | 35 | 90 |
| 9 | T_Engineer_Mine | Mine | 1 | 3 | 15 | 250 | – | TMIN | UAMINE | 581, 434 | 35 | 57 |
| 10 | T_Heli_Medic | Medic | 1 | 1 | 300 | 250 | yes | THMD | UAMEDIC | 479, 434 | 32 | 50 |
| 11 | T_Fighter | Fighters | 1 | 0 | 200 | 231 | – | TFGT | UABOMB | 768, 176 | 7 | 23 |
| 12 | T_Gene_Bomb | Gene Bomb | 1 | 0 | 500 | 233 | – | TBMB | UANUKE | 420, 220 | 1 | 55 |
| 13 | M_Fighter | Fighters | 3 | 0 | 75 | 360 | – | MFGT | UABOMB | 768, 176 | 2 | 27 |
| 14 | B_Zeppelin_F | Footmen | 2 | 4 | 60 | 431 | – | BZLN | UAZEPP | 274, 434 | 45 | 120 |
| 15 | H_Glider_T | Troopers | 0 | 4 | 90 | 150 | – | HPAR | UAHPAR | 274, 434 | 47 | 74 |
| 16 | H_Glider_C | Commandos | 0 | 4 | 275 | 150 | – | HPAR | UAHPAR | 274, 434 | 47 | 74 |
| 17 | H_Glider_ET | Elite Troopers | 0 | 4 | 120 | 150 | – | HPAR | UAHPAR | 274, 434 | 47 | 74 |
| 18 | H_Glider_G | Grenadiers | 0 | 4 | 120 | 150 | – | HPAR | UAHPAR | 274, 434 | 47 | 74 |
| 19 | B_Zeppelin_G | Pyro-Maniacs | 2 | 4 | 120 | 431 | – | BZLN | UAHPAR | 274, 434 | 45 | 120 |
| 20 | M_Drop_P | Paralyzers | 3 | 4 | 300 | 360 | – | TMDR | UAHPAR | 274, 434 | 45 | 90 |
| 21 | T_Drop_M | Mega-Tanks | 1 | 4 | 500 | 250 | – | TMDR | UAHPAR | 274, 434 | 45 | 90 |
| 22 | T_Drop_S | Shocktrooper | 1 | 4 | 300 | 250 | – | TMDR | UAHPAR | 274, 434 | 45 | 90 |
| 23 | M_Drop_D | Seekers | 3 | 4 | 500 | 360 | – | TMDR | UAHPAR | 274, 434 | 45 | 90 |
| 24 | T_Drop_W | Worms | 1 | 4 | 1450 | 232 | – | TMDR | UAHPAR | 274, 434 | 45 | 90 |
| 25 | T_Drop_F | Flyers | 1 | 4 | 500 | 250 | – | TMDR | UAHPAR | 274, 434 | 45 | 90 |
| 26 | H_Heli_Medic_AP | Movement | 0 | 1 | 200 | 153 | yes | HHMD | UAMEDIC | 700, 434 | 29 | 53 |

Properties per type:

| id | `armor` | other properties |
|---|---|---|
| 0 | 40 | second animation: `Secondanim_AAPos` 479, 434, `SecondanimFrame_aa` 1, `SecondanimFrame_action` 40; `SecondExplain` "Click on destination" |
| 1 | 60 | – |
| 2 | 70 | `mineDetectionRange` 5 |
| 3 | 40 | `armorInc` 200, `actionInc` 0, `damageArea` 3 |
| 4 | 40 | `damage` 150, `AR`, `damageArea` 5 |
| 5 | 40 | `damage` 300, AP, `damageArea` 3 |
| 6 | 1 | `damage` 999, `AR`, `damageArea` 11 |
| 7 | 60 | second animation: `Secondanim_AAPos` 450, 433, `SecondanimFrame_aa` 14, `SecondanimFrame_action` 37; `SecondExplain` "Click on destination" |
| 8 | 70 | – |
| 9 | 70 | `mineDetectionRange` 5 |
| 10 | 60 | `armorInc` 200, `actionInc` 200, `damageArea` 1 |
| 11 | 60 | `damage` 150, `AR`, `damageArea` 7 |
| 12 | 1 | `damage` 999, `AR`, `damageArea` 11 |
| 13 | 30 | `damage` 100, `AR`, `damageArea` 3 |
| 14–25 | 20 | `nbDrop` 4; slot offsets (−1, −2), (2, 2), (−1, 1), (2, −2); facings 0, 3, 6, 7; a fifth slot (0, 0) facing 0 is present but unused; `unitTLDrop` 0. Exceptions: type 15 has facings 6, 2, 4, 0 and no fifth slot; type 25 also has `damageArea` 1 (unused). |
| 26 | 40 | `armorInc` 0, `actionInc` 450, `damageArea` 1 |

Dropped unit types (`unitIDDrop` → `Unit Types.typeID`): 14 → 4 Footman,
15 → 2 Trooper, 16 → 0 Commando, 17 → 58 Elite Trooper, 18 → 56 Grenadier,
19 → 3 Pyro Maniac, 20 → 32 Paralyzer, 21 → 14 Mega Tank,
22 → 1 Shocktrooper, 23 → 34 Seeker, 24 → 59 Worm, 25 → 30 Flyer.

By category:

- **Destroy (0):** 4 bomber (armor damage 150, area 5), 5 gas (AP damage
  300, area 3), 6 nuke (999, area 11, AA armor 1), 11 Tauran fighters (150,
  area 7), 12 gene bomb (999, area 11, AA armor 1), 13 Shadamar fighters
  (100, area 3).
- **Repair (1):** 3 "Armor" helicopter (armor +200, area 3, once),
  10 Tauran medic (armor +200 and AP +200, one tile, once), 26 "Movement"
  helicopter (AP +450, one tile, once).
- **Build (2):** 1 and 8, one bridge section per call.
- **Mine (3):** 2 and 9 only **detect** mines (radius 5); despite the
  symbol `…_Engineer_Mine` nothing is laid (the explanation text also says
  "detect").
- **Drop (4):** 14–25, four units each.
- **Transport (5):** 0 and 7.

## 2. Which automated units a player can use

### 2.1 The usable list

Every player has a per-battle list of usable automated types. The server
sends it to the player's client with `CUpdClientUnitAvailable` {player,
list of unit type ids, list of automated type ids}; the client's automated
units view shows exactly this list. There is no per-battle count limit
apart from `DeployOnce` (2.4) and the tactical resource.

### 2.2 Campaign

Every automated type whose `Prerequisite` building type the player owns in
his city is usable (city-building.md 8); there is no race filter. The list
is built at the start of each campaign battle (and on Army-screen requests,
army-tech.md). In city battles (first attack, counter-attack) it is rebuilt
whenever one of the owner's buildings is destroyed, so losing e.g. the
airfield during the battle removes its types (campaign.md 6.4).

### 2.3 Skirmish and death match

- **Death match**: depending on the objective kind (battle-flow.md 2.10)
  the list is either built like the campaign rule from the prerequisite
  buildings the player owns **on the battle map** (kind 8, and kind 9 for
  slot 1), or taken from the scenario's `One Off Automated Available` list
  (kind 10, and kind 9 for the other slots).
- **Skirmish**: not traced; presumably the scenario's `One Off Automated
  Available` list *(?)*.

### 2.4 `DeployOnce`

After a call of a type with `DeployOnce` by a **human** player has been
executed (also when the automated unit was shot down), the server adds the
type id to a **server-wide** "used" set. It then rebuilds that player's
automated list as "usable list minus every type in the used set" and sends
it with `CUpdClientUnitAvailable` (the unit list unchanged). The set is
shared by all players: a second human player with the same type loses it
the next time his list is sent *(?: probably unintended)*. The computer
player is not subject to `DeployOnce`. Shipped once-only types: 3, 10, 26.

## 3. Player side: selecting and targeting

The view itself (panel, buttons, keys) is described in
tactical-interface.md 6.2.

### 3.1 List and selection

- Each row draws only types the client has in its usable list: the 38 × 38
  list icon (`LISTBOX.DBI`, `<facingsImageID>999` *(?)*) at (row left + 2,
  row top + 2), the name at (row left + 50, row top + 2) and the cost in
  decimal at (row left + 50, row top + 24), white text on a transparent
  background in the standard UI font. The highlighted row gets a frame.
- Opening the view selects the first row.
- Selecting a row starts targeting for that type. If the selected type
  differs from the previous selection, the type's `Explain<language>` text
  is shown in the status line for **10 000 ms**, white.
- **Cursor** over the map: over a tile that passes the type's target test
  (3.2, including the cost test) the type's `cursorID` sprite; otherwise
  `CINVA`. (`CTARG` is also loaded; presumably used when no type is
  selected *(?)*.)

### 3.2 Target tests

Every test first requires **tactical resource ≥ `Cost`**. "Own" means the
side relation of tactical-combat.md 1.1 gives *same side*: the same player;
allies do not count.

| Category | Valid target tile |
|---|---|
| 0 destroy | not a tile with an own building, not a tile with an own unit. Enemy, allied and empty tiles are all valid. |
| 1 repair | if a unit stands there it must be own; else if a building stands there it must be own; else the tile must hold a wall (any owner). Empty tiles are invalid. |
| 2 build | the tile is a **bridge site** (city-building.md 4.3) **and** the road placement test passes: inside the map; no unit, building, wall or road on it; terrain class riverside (gives a ramp) or deep water (gives a bridge); for deep water at least one orthogonal neighbour (x ± 1, y), (x, y ± 1) already holds a road. (The test accepts plain terrain too, but plain tiles are never bridge sites.) |
| 3 mine | no building, unit or wall on the tile, and the terrain class is neither mountains (4) nor forest (5). |
| 4 drop | cost test only: any tile. |
| 5 transport | **first point** (source): a unit stands there, it is own, its movement class is not 0 or 8 (immobile), and it is not a convoy. **Second point** (destination): no building, unit or wall on the tile, and the source unit can stop there (tactical-movement.md 1, enterable for the whole footprint, using the caller's knowledge of mines). |

**Bridge markers.** While a bridge type (category 2) is selected, every
bridge-site tile without a road shows a symbol on overlay layer 13: `BRIDGE`
if the placement test passes now, else `BRIDG001` (as on the city screen).
The layer is cleared when the request is sent or the view is closed.

### 3.3 Clicks

Points needed: 2 for transport, 1 for every other category.

On a map click while a type is selected and points are still missing:

1. If tactical resource < `Cost`: status string **448** ("Need more tactical
   resource"), 5000 ms, white. Stop.
2. Run the target test of 3.2 for the clicked tile (for a transport: the
   source test for the first point, the destination test for the second).
   On failure: string **320** ("Invalid target"), 5000 ms. Stop.
3. Store the point and decrease the number of missing points.
4. If no point is missing: check the cost again (on failure string 448,
   5000 ms, the view stays open); otherwise send `CReqAutomatedUnit`
   (3.4), reset the points (back to 1 or 2) and return to the tactical
   view.
5. If a point is still missing (a transport after its source): show
   `SecondExplain` ("Click on destination") for 10 000 ms.

### 3.4 Request `CReqAutomatedUnit`

Client → server: u32 automated `typeID`; u32 `n` (number of points); `n` ×
(u32 x, u32 y). A transport sends (source, destination); every other type
one point.

## 4. Server: request handling

1. Take the first two points of the request (p1, p2); further points are
   ignored (clients never send more than two).
2. Look up the type and create the server object for its category (0–5).
   An unknown category does nothing. The **owner is the current player** of
   the tactical phase, not checked against the sender *(?: the request is
   presumably only reachable in that player's turn)*. Target = p1 for
   categories 0–4; for a transport source = p1 and destination = p2.
3. The only server-side validity check is "target ≠ (−1, −1)". **The target
   tests of 3.2 are not repeated** (OpenDay should repeat them).
4. If the owner's tactical resource < `Cost`: send `CDeployFailedMsg` to the
   sender and stop.
5. Execute the call (4.1).
6. Deduct `Cost` from the owner's tactical resource and send
   `CUpdClientResources` {player, new resource} to the sender. The cost is
   paid **whatever happened**: shot down, nothing to transport, all drop
   slots failed.

### 4.1 Execution (shared with the computer player)

1. Run the category's effect (§6), which produces a list of tactical
   sub-messages (AA pass included, §5).
2. If the list is not empty: re-evaluate the held tech buildings
   (battle-flow.md 5.2); this may append `CTMMsgTech` records.
3. Battle-over check (objectives, battle-flow.md 2).
4. If the list is not empty: broadcast it as one `CMultiNetMsg` (with the
   turn number) to all clients.
5. If the battle is over: run the end-of-battle routine (result message).
   Otherwise, for a human caller, the `DeployOnce` handling of 2.4.

Objectives are thus re-evaluated right after the call, as after a shot.

## 5. Flight and anti-aircraft fire

There is **no flight path** on the server. The "flight" is only the client
clip, centred on the target (§8). Nothing enters from a map edge, and no
tile other than the target (and for a transport the destination) matters.

### 5.1 Armor and outcome

Each call reads the type's `armor` and starts with `acc` = 0. Outcome code:

| Condition | Outcome |
|---|---|
| `acc` = 0 | 1 OK |
| 0 < `acc` < `armor` | 2 damaged |
| `acc` ≥ `armor` | 3 destroyed |

The effect (§6) runs only if `acc < armor`. A damaged automated unit has a
weaker effect (scaled by `(armor − acc) / armor` or halved, per category).

### 5.2 AA pass at a tile P

Made once with P = the target tile (categories 0–4, before the effect) and
twice for a transport (P = source, then P = destination, 6.6).

1. **Candidates**: every unit on the map that
   - is an **enemy** of the caller (tactical-combat.md 1.1; coop is true in
     every game mode except mode 3),
   - has `IsAA`,
   - has `canOverwatch` and available AP ≥ `actionPointsShoot`, and
   - whose footprint enlarged by `weaponRange` on every side contains **P**
     (the target tile; `AAPosX/AAPosY` play no part).
2. Insert them into the call's candidate list sorted **ascending** by
   average weapon damage `(lo + hi) div 2` of the unit's damage range
   (tactical-combat.md 4.1, `lo = damageBase`, `hi = damageBase +
   damageRandom`), ties by smaller `actionPointsShoot` first; equal keys
   keep map unit-list order.
3. Walk the list from its head: if `acc ≥ armor`, remove this and all
   following candidates; otherwise `acc += DamageAA` of the candidate.
   There is **no hit roll** and no random draw.
4. Append `CTMMsgAutoUnit` {typeID, outcome, P}.
5. For every candidate still in the list (only when `armor` > 0): the unit
   pays `actionPointsShoot`, and a `CTMMsgUnitAA` {unit id, its available AP
   afterwards} is appended.

Shot-down automated units are not units: no kill or loss statistics are
recorded.

### 5.3 Transport: two passes on one list *(as implemented)*

The candidate list is **not cleared** between the source and destination
passes, and the walk of step 3 starts again from the head. So units that
fired at the source fire again at the destination (they pay AP again and
add their `DamageAA` to `acc` again), and a unit in range of both points is
in the list twice. `acc` accumulates over both passes. *(?: looks
unintended; to be confirmed in play.)*

## 6. Effects

`acc` is the value after the AA pass. "Owner" is the caller's slot (0 for
the computer player).

### 6.1 Destroy (category 0)

1. AA pass at the target; stop if destroyed.
2. `base = trunc(damage × (armor − acc) / armor)`.
3. Damage type: armor damage if `damageType` equals `AR`
   (case-insensitive), otherwise AP damage.
4. Apply the area damage of a shot (tactical-combat.md 4.1, 4.3, 4.4) with
   impact tile = target, attacker = owner, `lo = hi = base`, area
   `a = damageArea`. With span 0 the roll gives `D = base + 1`, but **one
   random number is still drawn**. No hit roll, no miss scatter, **no
   difficulty scaling** (also for the computer player).
5. Everything of tactical-combat.md 4.3–4.4 and 5 applies: square of side
   `a` centred on the target, clipped to the map, falloff
   `D − (D div k) × d` with `k = (a + 1) div 2`; mines destroyed, buildings,
   walls and units damaged or destroyed, roads, terrain burning, wrecks and
   rubble; **friendly fire included**. Kills and destroyed buildings are
   credited to the owner.

Undamaged values per ring `d` = 0, 1, …:

| Type | Values |
|---|---|
| 4 bomber (area 5) | 151, 101, 51 |
| 5 gas (AP, area 3) | 301, 151 |
| 6 nuke, 12 gene bomb (area 11, k 6) | 1000 − 166·d for d = 0…5 |
| 11 Tauran fighters (area 7, k 4) | 151 − 37·d for d = 0…3 |
| 13 Shadamar fighters (area 3) | 101, 51 |

### 6.2 Repair (category 1)

1. AA pass at the target; stop if destroyed.
2. `A = armorInc`, `P = actionInc`, `a = damageArea`,
   `f = 1 − acc / armor`, `h = (a + 1) div 2`.
3. Square: x from `tx − a div 2` to `tx − a div 2 + a − 1`, the same for y;
   tiles outside the map are skipped.
4. For a tile at Chebyshev distance `d` from the target and a value `V`:
   `amount(V) = trunc((V − (V div h) × d) × f)`. So `a` = 1 gives the full
   value on the one tile; `a` = 3 gives V at the centre and `V − V div 2` on
   the ring.
5. If `A` > 0: the tile's building, unit and wall (each if present, **any
   owner, enemies included**) get `currentAR += amount(A)`, capped at their
   type's maximum (`Building Types.Armor`, `Unit Types.Armor`, the wall
   type's armor). The armor reported for the tile is that of the last
   object processed (order: building, unit, wall).
6. If `P` > 0 and a unit is on the tile: `currentAP += amount(P)`, capped at
   the type's `actionPoints`; `negAP` is not changed.
7. If the reported armor > 0 or the new AP > 0: append `CTMMsgRepair` {new
   armor, new AP, x, y}.

The client only accepts an own unit, own building or a wall as the centre,
but the area heals everything on its tiles.

### 6.3 Build (category 2): a bridge section

1. AA pass at the target; stop if destroyed. A damaged builder still builds
   (no reduction).
2. Add a road of road type 1 (`Road Types` "Paved Road", the high-tech
   road) on the target, owned by the caller. The terrain class decides
   whether it is drawn as a ramp (riverside) or a bridge (deep water), see
   map-objects.md. The original has no graceful failure if the placement
   test fails here (the client test of 3.2 is its only guard); OpenDay
   should re-check and skip.
3. Append `CTMMsgBridge` {road type id, owner, x, y}.

One tile per call.

### 6.4 Mine (category 3): mine detection

1. AA pass at the target; stop if destroyed.
2. `r = trunc(mineDetectionRange × (armor − acc) / armor)`.
3. Tiles: x in `[max(0, tx − r), min(W, tx + r))`, y in
   `[max(0, ty − r), min(H, ty + r))` (W, H = map size). The **upper bounds
   are exclusive**, so the revealed square is 2r × 2r with the target at
   offset r: x − 5 … x + 4 when undamaged.
4. Every mine on those tiles (any owner, also mines already visible) is made
   visible to the caller: caller 0 clears `hidden_AI`; a human caller clears
   **all four** flags `hidden_1` … `hidden_4`. For each such mine a
   `CTMMsgMineDetect` {player, x, y} is appended. Mines are not removed.

### 6.5 Drop (category 4)

1. AA pass at the target; stop if destroyed.
2. Unit type = `unitIDDrop`. Tech level: 0 for the computer player;
   otherwise the caller's current tech level for that unit type (campaign:
   his campaign tech table, army-tech.md; skirmish and death match: the
   tech table of that mode *(?)*). `unitTLDrop` is ignored.
3. `n = nbDrop`; if damaged (`acc` > 0) `n = n div 2` (4 → 2).
4. For slot i = 1 … n, tile = target + (`unitPosX<i>`, `unitPosY<i>`):
   - **Placement test**: inside the map and enterable for the unit type's
     whole footprint (tactical-movement.md 1, with the caller's knowledge
     of mines). Occupied tiles (unit, building, wall) fail. There is no
     search for another tile.
   - If an **armed** mine lies on the tile, it explodes (tactical-combat.md
     7: mine explosion message, area-1 damage with the mine's own damage
     and owner, then the mines on the 8 neighbours become visible to the
     dropping player). The slot fails.
   - If the test passed and there was no armed mine: create the unit (owner
     = caller), set its `currentAP` to the type's full `actionPoints` minus
     `negAP` and clear `negAP` — **dropped units can act at once** — and set
     its facing to `facing<i>` (only for unit types that have facings).
     Append `CTMMsgNewUnit` {unit typeID, tech level, new unit id, owner,
     x, y, facing}.
   - Otherwise append `CTMMsgNewUnit` with owner **5** and unit id 0: a
     failed slot, nothing is placed.

### 6.6 Transport (category 5)

1. Find the unit on the source tile (any owner; the server does not
   re-check the client's test). None → nothing happens (the cost is still
   paid).
2. AA pass at the **source**. If `acc ≥ armor`: stop; the unit stays where
   it is.
3. Append `CTMMsgUpdateUnit` {unit id, flag 1 (picked up), available AP,
   armor, position (−1, −1)}: the client hides the unit.
4. AA pass at the **destination** (5.3; `acc` accumulates; a second
   `CTMMsgAutoUnit`).
5. Destination test: the unit can stop there (tactical-movement.md 1),
   without the mine-knowledge option. In this form units larger than 1 × 1
   always fail (no shipped mobile unit is larger).
6. If `acc < armor` and the destination passed:
   - new armor = `max(1, trunc(currentAR × (1 − acc / armor)))`;
   - the unit spends all its available AP (available AP becomes 0);
   - append `CTMMsgUpdateUnit` {unit id, flag 0, AP, armor, destination}
     and move the unit to the destination;
   - if an **armed** mine lies on the destination, it explodes under the
     unit (normal mine explosion, attributed to the mine's owner).
7. Otherwise (shot down on the way, or destination no longer valid): append
   `CTMMsgUpdateUnit` {unit id, flag 0, AP 0, armor 0, (−1, −1)} and remove
   the unit from the map. No kill or loss statistics are recorded *(?)*.

### 6.7 Nuke, gene bomb, Zeppelin

No special rules: the nuke and the gene bomb are destroy types with AA
armor 1 (a single AA unit in range shoots them down); the Zeppelins are
drop types.

## 7. Messages

| Message | Direction | Fields |
|---|---|---|
| `CReqAutomatedUnit` | client → server | typeID; n; n × (x, y) (all u32) |
| `CDeployFailedMsg` | server → sender | no payload: not enough tactical resource |
| `CUpdClientResources` | server → sender | player, tactical resource after the cost |
| `CUpdClientUnitAvailable` | server → player | player; unit type ids; automated type ids |
| `CTMMsgAutoUnit` | in `CMultiNetMsg` | automated typeID (u32), outcome (u16: 1/2/3), x, y (u16) |
| `CTMMsgUnitAA` | 〃 | unit id, available AP after firing |
| `CTMMsgRepair` | 〃 | new armor, new AP, x, y |
| `CTMMsgBridge` | 〃 | road type id, owner, x, y |
| `CTMMsgMineDetect` | 〃 | player, x, y |
| `CTMMsgNewUnit` | 〃 | unit typeID, tech level, new unit id, owner (5 = failed slot), x, y, facing |
| `CTMMsgUpdateUnit` | 〃 | unit id, flag (1 = picked up / hidden), AP, armor, x, y ((−1, −1) = off the map) |
| damage and destruction records | 〃 | as for shots (tactical-combat.md 8) |
| `CTMMsgMineExplosion` | 〃 | a drop or transport onto an armed mine |
| `CTMMsgTech` | 〃 | tech-building changes (4.1) |

Order inside one broadcast list: `CTMMsgAutoUnit`, its `CTMMsgUnitAA`s, then
the effect records, then the tech records. A transport gives: AutoUnit
(source), UnitAA…, UpdateUnit (pick-up), AutoUnit (destination), UnitAA…,
UpdateUnit (result), mine records, tech records.

## 8. Client animation

### 8.1 Clip

- Entry `ANMSUNIT.FF\AUTOUNIT\<facingsImageID><s><n>.SMK`:
  - `<n>` = outcome code of the `CTMMsgAutoUnit`: 1 OK, 2 damaged,
    3 destroyed;
  - `<s>` = 0 for the first animation, 1 for the second animation. The
    second animation is used for the `CTMMsgAutoUnit` that directly follows
    another `CTMMsgAutoUnit` of the same call, i.e. the delivery leg of a
    transport (clips `HHTR1n`, `THTR1n`).
- The **whole clip** is played at 15 frames per second (the clip rate; the
  battle playback runs at 15 ticks/s, unit-animation.md 4). The
  `…_Begin/_Qty` columns and properties are ignored.
- Shipped clips: 18 codes × outcomes `01`–`03`, plus `11`–`13` for `HHTR`
  and `THTR` (60 entries). Frame counts: BZLN 131/131/111, HBDG 76/76/51,
  HBMB 51/51/41, HGAS 51/51/41, HHMA 86/86/66, HHMD 97/81/51,
  HHTR 86/86/66 and 66/66/46, HMIN 76/76/66, HNKE 51/51/17, HPAR 76/76/76,
  MFGT 61/61/61, TBDG 96/96/66, TBMB 62/62/11, TFGT 77/67/31,
  THMD 87/87/66, THTR 66/66/36 and 66/66/36, TMDR 116/116/66,
  TMIN 81/81/66. A further entry `AUTOUNIT\BZLN.SMK` (81 frames, no audio)
  is not referenced *(?)*.
- Frame sizes 900 × 788, 960 × 848 or 1024 × 768, larger than the 640 × 480
  screen; the viewport clips them. Colour key = the frame's top-left pixel
  (FF.md).

### 8.2 Timing

- The first animation starts at the start of the message chain (tick 0 of
  the call's playback). The second animation starts when the first clip
  ends *(?: see open questions)*.
- **AA tick** = clip start + (`FrameAA` − 1); for the second animation
  clip start + (`SecondanimFrame_aa` − 1).
- **Action tick** = clip start + (`FrameAction` − 1); for the second
  animation clip start + (`SecondanimFrame_action` − 1).
- The message's chain end is the **action tick**: the effect records that
  follow it (damage, repair, bridge, mine detection, new units, unit
  update) start playing at that tick. For a transport, the pick-up
  (unit hidden) happens at the action tick of the first clip, and the
  unit appears at the destination at the action tick of the second clip.

### 8.3 Placement and view

- When a clip starts, the tactical view is **centred** on its tile (the
  target; for the second transport clip the destination) and redrawn.
- Position: the clip is centred on the tile like every tile animation
  (unit-animation.md 3): frame top-left = tile origin + (27, 42) −
  (width div 2, height div 2).

### 8.4 AA shots

- Each `CTMMsgUnitAA` makes its unit play its **shoot** clip (`T`,
  unit-animation.md 6 "Anti-aircraft fire") starting at the AA tick.
- Aim point: (`AAPosX`, `AAPosY`) (second animation:
  `Secondanim_AAPosX/Y`), both coordinates multiplied by
  `screen width / 1024` (both by the **width** factor) and truncated. It is
  the screen position of the aircraft at frame `FrameAA` on the centred
  view.
- At the start of the clip the unit turns to face the map tile under the
  aim point and its static sprite is hidden; at the end the static sprite
  is shown again (with the new facing) and its AP display is set to the AP
  in the message.
- No projectile, impact or hit is shown for AA fire *(?)*.

### 8.5 Sound

The clips carry their own audio track (22 050 Hz mono), played with the
clip. No separate sound effect is played *(?)*.

## 9. The computer player

The computer player uses the same server objects and the same execution
(4.1), with owner 0, no cost check at the call itself (its budget is
handled by the callers) and no `DeployOnce`. Details of when and where it
calls are in tactical-ai.md 6. Summary:

- **Race filter**: a call is executed unless the type's `Race`
  (`Automated Units` column, 1.1) is 0 (human) **and** its `typeID` is not
  4, 5 or 6. So every alien type is executed, including the destroy types
  11, 12, 13; human types other than the bombers and the nuke are silently
  dropped (this only matters for scheduled rows).
- Random calls (fighters/gene bomb, medic, drops, mine detection),
  scheduled rows of the scenario's `Automated Units` table (exactly in turn
  `TurnArrival`, p1 = (`StartX`, `StartY`), p2 = (`DestX`, `DestY`)), the
  emergency Zeppelin drop (type 19) and the counter-attack transport
  (type 7; the transported unit is simply the unit standing on p1).
- Dropped units of the computer player have tech level 0.

## Notes for OpenDay

- Implement one server routine per category with the shared step "AA pass,
  then effect if not destroyed"; keep the AA rules (target tile, sort, no
  roll) and the `(armor − acc) / armor` scaling with truncation.
- Re-run the client target tests (3.2) on the server; the original does
  not, and it has no graceful failure for an invalid bridge tile.
- Decide whether to reproduce the transport double AA pass (5.3) and the
  server-wide `DeployOnce` set (2.4); both look unintended. Put them behind
  a "faithful" switch if kept.
- Keep the extra random draw of the destroy damage roll (6.1) to stay in
  step with the original's random sequence.
- Ignore the `…_Begin/_Qty` columns; play the per-outcome clip in full and
  start the effect records at `FrameAction`.
- `SecondExplain` exists only in English in the data; OpenDay may add its
  own translations.

## Implementation notes (OpenDay)

Data in `game/map/rules.*` (`AutomatedType` with the parsed properties),
shared rules (target tests, bridge sites, clip names) in
`game/battle/automated.*`, the server side in
`game/battle/server_automated.cpp`, the computer player's calls in
`game/battle/ai.cpp`, the client animation in `game/anim/battle_animations.*`
and `battle_view.cpp`, the view in `app/battle_screen.cpp`.

- Messages are OpenDay's own events: `AutomatedRequest`, `AutomatedArrived`
  (`CTMMsgAutoUnit`), `UnitFiredAA`, `Repaired` (one event per tile with the
  new armor of each object, instead of the single "last object" armor),
  `BridgeBuilt`, `UnitTransported` (`CTMMsgUpdateUnit`), `MinesRevealed`,
  `UnitAdded` (failed drop slots send nothing), `AutomatedUsed` (instead of
  resending the usable list), `DeployBudget` (the cost). A refused request
  is a `RequestRejected`.
- The server repeats the target tests of 3.2 and refuses requests outside
  the sender's turn, of types not in his usable list, with too few points or
  with too little tactical resource; nothing is paid then.
- `CombatOptions::faithfulAutomated` (off by default) switches on the
  transport's shared AA candidate list (5.3) and the server-wide `DeployOnce`
  set (2.4). Off: each AA pass has its own candidates (`acc` still adds up)
  and only the caller loses a `DeployOnce` type.
- Skirmish: the scenario's `One Off Automated Available`, human types only
  (like the unit list); death match: the rule of 2.3; campaign: the city's
  list, and in city battles filtered by the buildings still standing.
- Drops: an armed mine on a slot goes off whether or not the slot would
  have passed the placement test. Human drops use the player's tech level of
  the dropped type (skirmish: the army screen's levels).
- Damage records carry a `DamageCause` (attacker slot, automated type) as
  the hook for the battle statistics (battle-flow.md 5.3); units lost with
  a transport are removed without a cause.
- Client: the clip is centred on the target (the view is centred when it
  starts); the next clip starts after the previous one ends; the effect
  records start at the action frame; AA units play their `T` clip at the AA
  frame, facing the tile under the scaled aim point of a view centred on
  the target (camera clamping ignored). The clip's own audio is played.
- The list icon is `<facingsImageID>999` from `LISTBOX.DBI`; the stored
  transport source is marked with `<H|T>TRANSPS` and a valid destination
  with `<H|T>TRANSPD` in addition to the type's cursor.
- `SecondExplain` is shown as stored (English).

## Open questions

1. Exact use of the `CTARG` cursor (only when nothing is selected?).
2. Whether `CReqAutomatedUnit` is accepted outside the sender's turn (the
   server uses the current player as owner).
3. The exact screen-to-tile conversion used for the AA unit's facing.
4. Playback chain: which message the AA animations are anchored to, and
   whether the second transport clip starts after the first clip or after
   the AA animations in between.
5. The usable list in skirmish (the death-match rule is known).
6. The tech level of human drops in skirmish and death match.
7. Whether removing a transported unit (6.6 step 7) changes objectives or
   statistics beyond the generic battle-over check.
8. Whether `SecondExplain` was meant to be localised.
9. What happens to a request whose target is (−1, −1) (presumably ignored,
   with no message).
10. The use of the unreferenced `AUTOUNIT\BZLN.SMK`.
