# City building (the campaign's Province screen)

Between battles the player builds buildings, towers, roads, walls and mines
on his city map with industrial credits. Buildings unlock unit types and
automated units for every campaign battle, deployment centres earn
deployment points for the battles fought in the city, and everything built
there defends the city in the counter-attacks ([campaign.md](campaign.md) 6).

Status: derived from studying the original program and checked against the
shipped databases. Items marked *(?)* are not fully confirmed.

Terms: positions are tile coordinates; a building's position is its top-left
tile, its footprint `x … x+SizeX−1` × `y … y+SizeY−1`. "Ring" around a
rectangle = the tiles `x−1 … x+w` × `y−1 … y+h` outside it.

## 1. The city

- The city is the scenario `s<c>-1-1` of the colony (`PlayerCity` = 1,
  variant 1 always). All four city maps are 96 × 96.
- Pre-placed: one Headquarters (type 130) per player slot on its `HQ on Map`
  site; colony 1 also has two Guard Towers, an Uplink and 14 paved road tiles
  around slot 1's HQ. HQs of unused slots are removed when the colony starts
  ([campaign.md](campaign.md) 2.1), with their HQ sites.
- The city lasts for the whole colony; the next colony starts with a fresh
  city map.
- The city screen opens scrolled to the local player's Headquarters.

## 2. What can be built

### 2.1 Buildings and towers

Building types with `Race` 0 and `CanBuild` set. A type whose
`TransformIntoUnit` is set is a **tower** (Guard, Rocket, Laser, AA Tower);
towers are listed with the defenses. `Position` is the terrain class every
footprint tile must have: 3 = plain, 1 = deep water (Small Dock, Sub Base,
Patrol Base). `Cost` is paid when building, `Recycle` refunded when recycling
later (6).

The list on screen (the client's list; the server does not check these
rules) walks the colony's `Buildings Available` rows **in table order** and
keeps a building type when:

- `Race` = 0 and `CanBuild` is set;
- its `Prerequisite` is 65535 or the local player owns a building of that
  type in the city — a prerequisite that is a deployment centre
  (`H_Center_Deployment1…4`) is satisfied by **any** deployment centre (this
  makes e.g. the Barracks, prerequisite Mk1, available in colonies that only
  offer Mk2–Mk4);
- it is not (`UniqueOnMap` and already owned by the player).

Each colony offers exactly one deployment centre: Mk1 (200 credits) in
colony 1, Mk2 (300) in 2, Mk3 (400) in 3, Mk4 (600) in 4. A player may own at
most **10** deployment centres (any mix of levels); the 11th is refused
(string 398).

### 2.2 Defenses

Road types with `canBeBuilt` (Paved Road 3 credits per tile, Basic Road 1),
the towers that passed the building rules above, wall types with `canBeBuilt`
(Concrete 5, Laser Fence 10, Force Field 20 per tile; Ruined Walls cannot be
built), mine types with `CanBuy` (Mine 10, Grav Mine 20). The Defenses list
shows them in the order **roads, towers, walls, mines**, each group in
GLOBAL table order. The `prerequisite` columns of walls and mines are not
checked.

## 3. Placement check

One check decides whether a structure may be placed (used for building, for
moving and for the cursor colour). Its result is a code c; the message is
string 384 + c.

| c | String | Meaning |
|---|---|---|
| 0 | 384 | generic failure |
| 1 | 385 | not enough credits |
| 2 | 386 | terrain / occupancy (also "unique building already owned") |
| 4 | 388 | too close to another building |
| 5 | 389 | too close to another player's buildings |
| 6 | 390 | too far from the player's other constructions |
| 7 | 391 | the HQ cannot be moved |
| 8 | 392 | the HQ cannot be recycled |
| 9 | 393 | an HQ must be placed exactly on an HQ site |
| 10 | 394 | province border |
| 12 | 396 | bridges need paved road |
| 13 | 397 | too close to an HQ |
| 14 | 398 | ten deployment centres already owned |
| 15 | 399 | must be next to an own road |

(Codes 3 and 11 exist as strings but are not produced.) The checks, in order:

1. **Area / terrain** → 2:
   - mine: tile on the map; no unit, building or wall; no other mine; the
     terrain is plain (class 3), or for a grav mine also mountains (4) or forest
     (5); any terrain is accepted when the tile holds a road;
   - wall: tile on the map; no unit, building, mine or road; no other wall;
     plain terrain;
   - building or tower: if not a move and the type is `UniqueOnMap` and the
     player already owns one → fail; every footprint tile on the map, of
     terrain class `Position`, without unit, wall, road, mine or another
     building (ruins do not matter);
   - road: tile on the map; no unit, building or wall; no other road; road
     terrain state (4.2) not "forbidden"; bridge-site rules (4.3).
2. **Headquarters**: the position must be exactly an HQ site → OK at once
   (no further checks); otherwise 9.
3. **Near own constructions** → 6 (not for deep-water buildings): some tile of
   `[x−4, x+w+3] × [y−4, y+h+3]` holds a road, wall or building of the player
   (mines do not count; the object being moved is ignored).
4. **Border** → 10: `13 ≤ x < W − w − 13` and `13 ≤ y < H − h − 13`.
5. Buildings, towers, walls, mines:
   1. **Around HQ** → 13: the footprint must not touch the rectangle of any HQ
      site grown by one tile (`[hx−1, hx+5) × [hy−1, hy+5)` for the 4 × 4 HQ),
      the player's own included.
   2. Walls and mines: OK here.
   3. **Road connection** → 15: an own road on the ring around the footprint,
      corners excluded; or a deep-water building; or a tower with any wall on
      that ring.
   4. **Gap** → 4 (not for towers): no building other than towers (and the one
      being moved) on the full ring, corners included, whatever its owner.
   5. **Other players** → 5: no building of another player in
      `[x−6, x+w+5] × [y−6, y+h+5]`.
6. Roads:
   1. bridge rule (4.4);
   2. **connection** → 15: the tile lies on the ring around an HQ site
      (corners included), or an own road is orthogonally adjacent.

The server additionally refuses a deployment centre beyond the tenth (14)
and checks the credits (1).

## 4. Roads

### 4.1 Segments

Roads and walls are built as **segments**: the player clicks the start tile
(string 407 "Click on starting point"), the screen shows the tiles that could
be reached along the start's row and column (8), and a second click on the
end tile (string 408) sends the segment.

The server walks n = max(|dx|, |dy|) + 1 tiles from the start, advancing x
by sign(dx) and y by sign(dy) at every step (an end off the row or column
therefore gives a pure diagonal of length n that misses the end — a quirk;
the screen only offers rows and columns). For each tile: placement check,
credits, add the piece, charge its cost. The first failure stops the segment;
the tiles already built stay. A failure on the first tile is reported with its
code. Because each tile is checked after the previous one was added, the
connection rules are met progressively.

### 4.2 Terrain state of a road tile

| Terrain class | State |
|---|---|
| 3 plain | ordinary road |
| 2 riverside | ramp |
| 1 deep water | bridge |
| 0, 4, 5, 6 | forbidden |

Ramp and bridge tiles must be **bridge-site tiles** (4.3); a bridge tile also
needs an orthogonal neighbour that already holds a road (any owner), so a
bridge grows from a bank outwards.

### 4.3 Bridge sites

Computed from the terrain when the city is loaded:

1. Candidates: every riverside tile (x, y) with x < W−1, y < H−1:
   - if (x+1, y) is deep water or riverside **and** (x−1, y) is plain → a
     candidate running along +x;
   - otherwise if (x, y+1) is deep water or riverside and (x, y−1) is plain → a
     candidate along +y.
2. From the start, step in the candidate's direction over deep-water tiles;
   keep the candidate only if the walk reaches a riverside tile (the far bank)
   **and** the tile after it is plain. The far bank is the end.
3. Of two candidates whose starts differ by exactly 1 (x or y) and whose ends
   differ by exactly 1, the second is dropped.
4. The tiles start … end of every remaining candidate are added to the site
   list, unless one of them is already in it.

The screen marks every bridge-site tile without a road with symbol `BRIDGE`
when a road could be placed there now, else `BRIDG001`.

### 4.4 Bridge rule

For a ramp or bridge tile: paved road (type 1) → OK; basic road → 12.

## 5. Walls and mines

- Walls are built as segments like roads; a new wall piece has its type's
  `Armor`.
- A mine is a single tile. A new mine is visible to every human player and
  hidden from the computer player (`hidden_AI`).

## 6. Recycling

- Refund = the type's `Cost` if the object was built **in the current
  strategic phase**, otherwise its `Recycle` value (roads: 0). The "current
  phase" lists start empty every time the strategic phase begins (after each
  battle); they hold building ids and wall / road / mine positions.
- The HQ cannot be recycled (392).
- The screen asks for confirmation (string 401, yes / no) before recycling a
  structure that was not built in this visit of the screen; after one "yes"
  it does not ask again during the visit.
- Clicking a tile recycles **every** own structure on it (building, wall,
  road, mine). A tile without an own structure: string 400.
- Messages: 404 "%1 has been recycled"; sound `CASHIN` for buildings.

## 7. Moving

Move mode: a first click picks every own structure on a tile (the HQ: string
391), a second click moves them; the placement check is made at the new
position with the picked objects ignored. **Moving is free.** A moved
building keeps its "built in this phase" status; a moved road, wall or mine
loses it (an original inconsistency). Message 405 "%1 has been moved".

## 8. What the buildings unlock

- **Deployable unit types** of a player: every unit type (by `typeID`) that is
  not a tower unit (the `TransformIntoUnit` of some building type), not a
  convoy (`isConvoy`), and whose `Prerequisite` building type the player owns
  in the city. A type that has no tech level yet starts at level 0.
- **Usable automated units**: every automated type whose `Prerequisite`
  building the player owns in the city.
- Computed when the army screen opens and at the start of every campaign
  battle (and again during a city battle when one of the player's buildings
  is destroyed).

## 9. The city screen (`CStratView`)

Same frame as the battle views: map viewport left (504 × 448 at 640 × 480,
840 × 728 at 1024 × 768), side panel to its right, message strip below the
viewport. Panel art: atlas frame **100**. Positions relative to the panel:

| Control | Position, size | Frames | Key | Action |
|---|---|---|---|---|
| emblem | (5, 0) 44×43 | 143–146 | F1 | encyclopedia |
| credit indicator | (64, 26)–(104, 40) | — | — | the player's credits |
| minimap | (12, 49) 112×112 | — | — | as in battle (tactical-interface.md 2.6) |
| MAP | (11, 171) 110×17 | 105–108 | F4 | the city overview map (strategic-interface.md 5) |
| Buildings | (12, 198) 19×19 | 109–114 | `B` | radio: list = buildings |
| Defenses | (12, 222) 19×19 | 115–120 | `D` | radio: list = defenses (checked at start) |
| structure list | (9, 247), 4 rows of 118×40 | — | — | 9.1 |
| list up / down | (9, 407) / (74, 407) 53×17 | 121–124 / 125–128 | — | scroll one row |
| Recycle | (11, 435) 53×17 | 147–152 | `R` | toggle recycle mode |
| Move | (70, 435) 53×17 | 129–134 | `M` | toggle move mode |
| Campaign | (11, 455) 53×17 | 135–138 | `C` | continent screen |
| Army | (70, 455) 53×17 | 139–142 | `A` | army screen ([army-tech.md](army-tech.md)) |

### 9.1 List rows

118 × 40 per row: the 38 × 38 list icon (`<ImageID>999` / `<img_id>999` from
`LISTBOX.DBI`) at (2, 2); the name at (50, 2) and the cost (per tile for
roads and walls) at (50, 24), standard font, white on black. The selected row
gets a one-pixel frame in palette index 255.

### 9.2 Modes and clicks

- Selecting a list entry enters build mode (Recycle and Move off) and, for
  roads and walls, shows string 407.
- Build mode, left click: on an own structure of the selected type →
  recycle it; otherwise local placement check (failure: string 384 + c and
  sound `INVALIDE`); then a credit pre-check (fewer credits than one piece:
  string 402 and `INVALIDE`); buildings, towers and mines are built at once;
  roads and walls take two clicks (4.1).
- Recycle mode: section 6. Move mode: section 7.
- Right button: the info card, as in battle.

### 9.3 Cursor

Build mode: the structure's own map sprites over its footprint (buildings in
the player's colour; road `<img_id>G000`, wall `<img_id>000`, mine
`<ImageID>000`), drawn on top with every second pixel (checkerboard on
(x + y)) replaced by palette **250 (green)** when the site is valid, **249
(red)** when not. Recycle mode: symbol `CRECY2` over an own structure,
`CRECY` elsewhere; move mode: `CMOVE` over an own structure, `CINVA`
elsewhere. The segment preview marks reachable tiles with `ROUTE`: from the
start tile in each of the four directions along its row and column, every
consecutive tile that passes the area test (step 1 of section 3), until the
first failing tile.

### 9.4 Messages and sounds

Shown in the message strip (20 s) for the player's own objects: 403 "%1 has
been built" (sound `BUILD` for buildings), 406 "%1 has been built for %2!d!
credits" for a road or wall segment of several tiles, 404, 405. Errors: 5 s,
sound `INVALIDE`.

## Notes for OpenDay

- The placement check, segment building, recycling and moving are server
  rules; the city screen uses the same check for the cursor colour.
- OpenDay checks the prerequisite and availability rules on the server too
  (the original trusts the client's list).

## Implementation notes (OpenDay)

Implemented in `game/campaign/city.*` (rules) and `app/campaign_city.cpp`
(screen).

- The MAP button (city overview map) is disabled.
- The encyclopedia button is disabled (no encyclopedia yet).

## Open questions

- Whether destroyed roads and exploded mines of the city come back.
- The exact scroll semantics of the two list buttons.
- Why the original sends refresh messages for the diagonal neighbours after
  recycling a road or wall (probably only to recompute sprite variants).
