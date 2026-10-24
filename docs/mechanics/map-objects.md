# Tactical map objects: sprite selection and drawing

How the original program turns the objects stored in a scenario database
(`Roads On Map`, `Walls On Map`, `Mines On Map`, `Ruins On Map`,
`Buildings On Map`, `Units On Map`) into sprites on the isometric tactical
map. The database only stores type and position. Everything else here (the
connection variant, the terrain-dependent sprite set, damage state, color
variant, layer and draw position) is computed by the program.

The tables in [data-model.md](data-model.md) and the screen geometry in
[isometric-map.md](isometric-map.md) are prerequisites.

Status: the rules below come from studying the program's behavior. The
road and wall rules were then checked against all shipped scenarios: every
road (19,654 placements) and every wall (11,479 placements) resolves to a
sprite name that exists in `ROADS.DBI` / `WALLS.DBI`. Items marked *(?)*
are not fully confirmed.

## Directions and the neighbor mask

Grid directions as in [isometric-map.md](isometric-map.md):

| Neighbor | Grid offset | Screen direction |
|---|---|---|
| NW | `(PosX − 1, PosY)` | up-left |
| SE | `(PosX + 1, PosY)` | down-right |
| NE | `(PosX, PosY − 1)` | up-right |
| SW | `(PosX, PosY + 1)` | down-left |

Roads, walls and deployment markers all use a **4-bit neighbor mask**
with these bit values:

| Bit value | Neighbor |
|---|---|
| 8 | NW |
| 4 | SE |
| 2 | NE |
| 1 | SW |

A neighbor outside the map never counts as connected.

### Connection variant (roads and walls)

Roads and walls share one mask → variant table. The variant is the
three-digit (roads) or two-digit (walls) number in the sprite name.

| Mask | Open sides | Variant | Shape |
|---|---|---|---|
| 0 | none | 0 | isolated (walls: a free-standing post) |
| 15 | NW SE NE SW | 1 | crossing |
| 7 | SE NE SW | 2 | T, closed toward NW |
| 13 | NW SE SW | 3 | T, closed toward NE |
| 14 | NW SE NE | 4 | T, closed toward SW |
| 11 | NW NE SW | 5 | T, closed toward SE |
| 12 | NW SE | 6 | straight NW–SE |
| 3 | NE SW | 7 | straight NE–SW |
| 4 | SE | 8 | dead end, open toward SE |
| 1 | SW | 9 | dead end, open toward SW |
| 2 | NE | 10 | dead end, open toward NE |
| 8 | NW | 11 | dead end, open toward NW |
| 6 | SE NE | 12 | corner NE + SE |
| 5 | SE SW | 13 | corner SE + SW |
| 10 | NW NE | 14 | corner NW + NE |
| 9 | NW SW | 15 | corner NW + SW |

This confirms the variant meanings previously guessed from the sprite
pixels, including 009 (open SW) and 010 (open NE).

## Roads

### Which neighbors connect

A neighbor tile counts as connected only if it holds a road of the **same
road type** (`Roads On Map.roadtype`, i.e. `Road Types.RoadType`: 0
low-tech, 1 high-tech). Low-tech and high-tech roads never join. Bridges
and ramps are ordinary high-tech road tiles, so they connect to the
high-tech roads next to them. Buildings, walls and other objects do not
count. The owner (`playerOwner`) is ignored.

### Sprite name

```
<img_id> <set letter> <three-digit number>
```

- `img_id` = `Road Types.img_id` (`ROAL` for roadtype 0, `ROAH` for 1).
- The **set letter** depends only on the terrain class
  (`Terrain Types.type`) of the road's own tile:

  | Own terrain class | Set | Number |
  |---|---|---|
  | 3 plain | `G` | the connection variant (table above) |
  | 1 deep water | `B` (bridge) | the connection variant |
  | 2 riverside | `R` (ramp) | ramp number, see below |
  | anything else | none | not supported; the shipped data never places a road there |

- Only `ROAHB000` and `ROAHB006`–`ROAHB011` exist in the `B` set. A bridge
  can only be isolated, straight, or a dead end. The other variants have no
  sprite.
- No `ROAL` sprite exists for the `B` and `R` sets. Low-tech roads only
  occur on plain terrain.

### Ramp number (`R` set, riverside tiles)

First the normal connection variant `v` is computed. Then the program
picks an **orientation** `o` from the terrain classes of the four
*neighbor* tiles. "Water side" means terrain class 1 (deep water) or 2
(riverside), and "land side" means class 3 (plain):

1. `o = 0` if the NW neighbor is a water side and the SE neighbor is a land side;
2. otherwise `o = 1` if the SE neighbor is a water side and the NW neighbor is a land side;
3. otherwise `o = 2` if the NE neighbor is a water side and the SW neighbor is a land side;
4. otherwise `o = 3`.

Note that the last case has no condition at all, so `o = 3` is the
fallback, not a test of SW against NE.

The ramp number is then looked up by `(v, o)`:

| Variant `v` | `o = 0` | `o = 1` | `o = 2` | `o = 3` |
|---|---|---|---|---|
| 0 isolated | 16 | 20 | 24 | 28 |
| 6 straight NW–SE | 17 | 21 | — | — |
| 8 dead end open SE | 18 | 22 | — | — |
| 11 dead end open NW | 19 | 23 | — | — |
| 7 straight NE–SW | — | — | 25 | 29 |
| 9 dead end open SW | — | — | 26 | 30 |
| 10 dead end open NE | — | — | 27 | 31 |

Every combination marked "—" (and every other variant) gives the number
32. No `ROAHR032` sprite exists, so those cases are invalid placements. The
shipped scenarios never produce one. A ramp therefore always runs along
the axis that crosses the bank: orientations 0/1 belong to the NW–SE axis
and 2/3 to the NE–SW axis.

### When variants are recomputed

- When a scenario is loaded, every road's variant is computed.
- When a road is added or removed, the 3 × 3 block of tiles around it
  (the tile and its 8 neighbors) is recomputed. Only the orthogonal
  neighbors affect the mask.
- The ramp number is computed again every time the tile is drawn. An
  implementation can simply compute everything from the current map
  state on demand.

## Walls

### Which neighbors connect

Only walls of the **same wall type** (`Walls On Map.roadtype` =
`Wall Types.WallType`: 0 concrete, 1 laser fence, 2 force field, 3 ruined)
connect. Different wall types do not join each other. Walls do not join
buildings, roads or any other object.

### Sprite name

```
WALL <type letter> <damage digit> <two-digit number>
```

- Type letter: from `Wall Types.img_id` (`WALLC`, `WALLL`, `WALLF`,
  `WALLR`).
- **Damage digit** `1` if `currentAR ≤ 0.33 × Wall Types.Armor`, else `0`.
  It is **computed from armor**, not read from `isDamaged`. Many shipped
  walls start with reduced `currentAR` (e.g. concrete at 10 of 60) and so
  show the damaged sprites from the start.
- Number: the connection variant, possibly replaced by a broken-end number
  (next section).
- There are no separate wall sets per terrain.

### Broken ends (`WALLC016`–`WALLC021`) and the `isDamaged` column

`Walls On Map.isDamaged` is misnamed. It is a per-wall **"broken end"**
flag. When it is set, dead ends and isolated pieces lose their end post
and show a broken, rubble-strewn end instead:

| Connection variant | Number with the flag set |
|---|---|
| 10 (dead end open NE) | 16 |
| 11 (dead end open NW) | 17 |
| 9 (dead end open SW) | 18 |
| 8 (dead end open SE) | 19 |
| 0, and the wall's previous number was 7 (straight NE–SW) | 20 (straight NE–SW, broken on both sides) |
| 0, and the wall's previous number was 6 (straight NW–SE) | 21 (straight NW–SE, broken on both sides) |
| 0, otherwise | 0 (plain post) |
| any other variant | unchanged |

The "previous number" rule means that a straight segment that loses both
neighbors keeps its straight shape, now broken on both ends. Only the
number stored by the previous computation counts. Recomputing the same
isolated piece again yields 0, because 20/21 are not 6/7. This quirk is
visible in the original *(?)*. Freshly loaded walls have no previous
number, so an isolated flagged wall starts as 0.

The program sets the flag at run time: when a wall segment is destroyed,
each of its four orthogonal neighbors gets the flag if it is a
**concrete** wall (type 0). Other wall types never get it, which is why
only `WALLC` has the numbers 16–21. The flag is never set in the shipped
scenarios. If a database set it on a non-concrete wall, the result would be
a nonexistent sprite name.

### Recompute

As for roads: all walls on load, and the neighborhood of a changed tile on
build or destroy *(?: exact trigger set not traced)*.

## Mines and ruins

### Mines

- **Sprite** (`MINES.DBI`, layer 4, offset (0, 0)): name =
  `Mine Types.ImageID` in upper case + a suffix chosen by the mine's
  **`armed`** flag: armed → `001`, not armed → `000`.
- Every shipped mine placement (2,392) has `armed` = 1, so the shipped maps
  always show `MINE001` / `GMINE001`. In `MINES.DBI` the `000` and `001`
  images are pixel-identical, so the distinction is invisible in practice.
- Draw mode: the mine's own mode value, 0 in battle; 1 (darkened) while the
  mine is being moved on the city screen (*Draw mode* below).
- **Visibility**: a mine is put on the visual map only if it is not hidden
  from the **local player**: the flag `hidden_AI` (slot 0) or
  `hidden_1` … `hidden_4` of the local player's slot must be false.
- **Revealing** a mine for slot 0 clears only `hidden_AI`. Revealing it for
  **any human slot clears all four human flags at once**: human players
  share their knowledge of mines.
- Disarming a mine (armed = 0) also reveals it to everybody. The client does
  this when an update says the mine is no longer armed; no server rule that
  disarms a mine was found *(?)*.
- Mine detection and mine explosions on the map: see
  [unit-animation.md](unit-animation.md) 7.

### Ruins

`RUINS.DBI`: name = `Ruin Types.id`, one tile, no variants. Wrecks of
destroyed units and the rubble of destroyed buildings are ruin objects too
(*Changes during an animation phase* below).

## Buildings

### Footprint

`Buildings On Map.PosX/PosY` is the footprint corner with the **smallest
x and y**. On screen this is the topmost (northern) tile. The building
covers the tiles

```
PosX … PosX + SizeX − 1   ×   PosY … PosY + SizeY − 1
```

(`Building Types.SizeX/SizeY`).

### Per-tile sprites (`UB.DBI`)

Each footprint tile is drawn as its own sprite on its own map cell. The
tile at `(PosX + i, PosY + j)` uses

```
<ImageID> <c> <d> <i> <j>
```

- `ImageID` = `Building Types.ImageID` (compare case-insensitively).
- `c` = color variant (below).
- `d` = `1` if `Building Types.DamagedImages` is true **and**
  `currentAR ≤ 0.33 × Building Types.Armor` (floating-point comparison),
  otherwise `0`. This is the only visible damage state: there is no smoke,
  fire or flame overlay on damaged buildings (no such clips or sprites
  exist). The digit is recomputed whenever the buildings layer is rebuilt,
  so a hit that brings the armor to a third or below swaps the sprites
  instantly at the scheduled tick (*Changes during an animation phase*).
  Shipped data: 58 building types with `DamagedImages` = 1 (and an
  explosion clip), 19 with 0; the four towers have 1 but never use it.
- `i` = offset along PosX (`0 … SizeX−1`), `j` = offset along PosY
  (`0 … SizeY−1`), each written as a decimal digit.

So the `<x><y>` digits of the sprite name are the offsets along PosX and
PosY respectively.

**Towers** (`TransformIntoUnit` ≠ 65535: building types 100 `HTLA`,
101 `HROK`, 102 `HLAS`, 103 `Htaa`, which turn into unit types 37–40) use
the unit naming scheme instead, with the **facing digit fixed to 0** and
**no damage digit**: `<code><c>0<i><j>` (e.g. `HLAS1000`), where `code` is
the tower unit type's `facingsImageID`. A tower *building* has no facing
state and never shows a damaged sprite. Two shipped SCEN1 scenarios place
towers as buildings.

As a **unit** (city battles turn towers into units, campaign.md) a tower
follows the ordinary unit rules: its unit type has `Facings` = 1 and only a
`T` shot clip (plus `E` and `I`), so when it fires it turns toward the
target, plays `T` in that facing and afterwards keeps the new facing in its
static sprite `<code><c><f>00` (unit-animation.md 6). A newly created tower
unit starts with the facing given by its creation (database row or
message). Static tower-unit sprites exist for facings 0–7 in colours 1–4
(`HTLA`, `HLAS`) and 0–4 (`HROK`, `HTAA`).

### Color variant

The color digit is not `playerOwner` itself. Every owned object (unit,
building, tower) keeps a **color value** that the client computes when the
object enters its copy of the scenario: `c = colorTable[playerOwner]`. The
value is not stored in any database and not sent by the server.

**The table is built per client** at the start of every game:

1. Start with the identity: `colorTable = [0, 1, 2, 3, 4]` (slot 0 = AI,
   slots 1–4 = humans).
2. Take the **local** player's slot `s` and the color `k` that player chose
   (1–4, see below).
3. Swap the entries `s` and `k`: `colorTable[s] = k`, `colorTable[k] = s`.

Consequences:

- The AI (slot 0) always gets color 0 (no player can choose 0).
- The local player always sees himself in his chosen color; the player
  whose slot number equals that color is shown in the local player's slot
  color; everybody else keeps color = slot number. Only the local player's
  choice is honoured, so two clients of the same game may show the same
  player in different colors.
- **Single player** (campaign, skirmish): the local player is slot 1 with
  color 1, so the table is the identity (slot *n* → color *n*).

Color numbers: 1 blue, 2 yellow, 3 purple, 4 green; 0 = the AI / race
default (red in the interface lamps and selection triangles). The color is
chosen in the multiplayer player set-up menu (`CMenuPlayerMulti`): four
19×19 radio toggles at (35, 152), (70, 152), (105, 152), (140, 152), frames
989, 995, 1001, 1007; the chosen color is the radio index + 1. It is stored
per user as `User Color` in the `User List` of `GAMEINFO.MDB` (the program
accepts 0–5 there, the menu produces 1–4). For a new game the server numbers
the players in join order (slot = index + 1); a loaded game keeps its slots.

The `Color Player Lookup` table of `GLOBAL.MDB` is never used by the
program.

Where the color value is used:

| Use | Rule |
|---|---|
| static unit sprite (`UB.DBI`) | `<facingsImageID><c><facing or A–H><i><j>`, `c` = color value as is (0 stays 0) |
| unit clip folder `P<c>` | unit-animation.md 1 |
| static building sprite | `<ImageID><c><d><i><j>` |
| tower building sprite | `<code><c>0<i><j>` (above) |
| selection triangles `SU00c` (layer 17) | `c` = the unit's color value; the "marked tile" triangle (layer 18) always uses `SU000` |
| deployment markers `DEPL<c><nn>` (layer 3) | `c` = `colorTable[local slot]`, the local player's own color |
| minimap / overview-map dots | `Colors` rows by color value; owner 0 by race (tactical-interface.md 2.6) |

Shipped sprites: Tauran, Shubblar and Shadamar buildings are always owned by
the AI and exist only in color 0. Human buildings exist in colors 1–4 (some
also in 0). Some human units and towers have no color-0 static sprite
(e.g. `HTLA0…`, `HLAS0…` are missing; `HROK0…`, `HTAA0…` exist); the
original then draws nothing for such an object *(?)*.

## Units (for reference)

- Name: `<facingsImageID><c><f><i><j>`. `c` comes from the same color
  lookup as for buildings, `f` = facing digit `0`–`7`, and `i`, `j`
  = tile offsets inside the footprint (`0 0` for 1 × 1 units). Each
  footprint tile is drawn on its own cell, like buildings.
- **Infantry in forest:** if the unit type's `Movement` is 2 (trooper)
  and the tile's terrain class is 5 (forest), the facing digit `0`–`7` is
  replaced by the letter `A`–`H`. This explains the second pose found in
  `UB.DBI`. Exactly the eight trooper unit codes (`HTRO`, `HCOM`, `HGRE`,
  `HATR`, `TSTR`, `TMUL`, `BFOT`, `BGRD`) have these letters.
- **Units on bridges:** a unit on a deep-water tile (class 1) that also
  holds a road is drawn **5 px higher** (vertical offset −5).

## Drawing: cells, layers and anchoring

### Per-cell feature lists

The visual map keeps, per tile, the ground (terrain) sprite and a list
of **features**. A feature is a sprite plus a layer number, a small
signed pixel offset (dx, dy, each −128…127) and a draw-mode value. Each
cell has at most one feature per layer, and adding a feature on an
occupied layer replaces it. The list is kept sorted by layer.

Object layers:

| Layer | Content |
|---|---|
| 0 | road |
| 4 | mine |
| 5 | ruin |
| 10 | building tile |
| 11 | wall |
| 15 | unit tile |

User-interface layers (same mechanism): 1 road-build preview, 2 objective
zone (`OBJT000`), 3 deployment-zone markers (`DEPL…`), 7 movement path
(`PATH…`), 8 floating images *(?)*, 9 a full-map overlay drawn on every
cell *(?)*, 12 wall-build preview, 13 bridge-site symbols (`BRIDGE` /
`BRIDG001`), 14 objective targets (`OBJT001`/`OBJT002`), 16 tech-building
marker (`LILBULB`), 17 and 18 selection / target markers.

### Anchoring

All features (roads, walls, mines, ruins, building and unit tiles) are
drawn with their sprite's **top-left corner at the same screen position
as the cell's terrain sprite**, plus the feature's (dx, dy). Every object
uses (0, 0) except units on bridges (0, −5). All these sprites are 54 × 56
like the terrain, so they line up without any per-sprite hotspot.

### Draw order

The map is drawn cell by cell in order of increasing depth `PosX + PosY`
(back to front). Within one depth the cells go left to right on screen
(increasing PosX). For **each cell**, the program draws its terrain sprite
first and then all its features in increasing layer order, before it
moves on to the next cell.

Consequences:

- Walls, buildings and units are drawn in the **same depth-sorted pass**.
  There is no separate terrain pass and no separate object pass. A wall
  tile in front of a unit covers it, and a unit in front of a wall covers
  the wall.
- Multi-tile buildings sort correctly against units because every tile is
  a separate feature on its own cell.
- On a single cell, a wall (11) is drawn over a building tile (10), and a
  unit (15) over both.

### Draw mode

Each feature carries a mode byte: the **low 4 bits** select the blit, **bit
4 (value 16)** defers the feature.

| Low bits | Visual effect | Rule |
|---|---|---|
| 0 | normal | plain color-keyed copy |
| 1 | 50 % darkened | checkerboard: opaque sprite pixels whose (column + row) is even keep their color, the other opaque pixels are written as palette index **0** (black) |
| 2 | 50 % see-through | checkerboard with holes: only the even-parity opaque pixels are written, the others keep the background |
| 3 | red stipple | like 1 with index **249** (red) |
| 4 | green stipple | like 1 with index **250** (green) |
| 5 | stipple in color 79 | like 1 with index **79** |

Parity is per sprite pixel: (column, row) counted inside the sprite, not on
the screen. Transparent (color-key) pixels are never written.

**Bit 4 (16) — drawn on top**: the feature is not drawn in the cell pass.
It is queued and drawn after all cells of the current redraw region, with
its low-bits blit, so it is never covered by objects of cells in front.
Used by the objective targets `OBJT001` / `OBJT002`, the tech marker
`LILBULB`, the selection triangles, and the path markers **`PATH0002` /
`PATH0003`** (the "crossable only" markers stay visible over the unit or
building they cross; every other `PATH` sprite uses mode 0,
tactical-movement.md).

Who uses which mode:

- **Units**: the unit's explicit mode if it is non-zero (1 for a unit
  placed in the deployment view); otherwise **1 (darkened) when the unit's
  usable AP (`currentAP − negAP`, at least 0) is below both its type's
  `actionPointsShoot` and 4**, else 0. So units that can no longer fire this
  turn are drawn darkened. The mode is recomputed whenever the units layer is
  rebuilt (e.g. after a move or a hit that costs AP).
- **Buildings, walls, mines**: their own mode, 0 in battle; the city screen
  sets 1 on the object being moved.
- Modes 2–5 were not seen on map objects; they are presumably used by
  interface overlays (layer 8, cursor images) *(?)*. The city screen's
  build cursor uses the 249 / 250 stipple (city-building.md 9.3), which
  matches modes 3 / 4.

## Changes during an animation phase

Map objects change at fixed ticks of the animation phase
(unit-animation.md 4). Every change is applied to the client's scenario copy
and then the affected layer is **rebuilt** (all sprites of that layer are
recomputed: variants, damage digits, colors, draw modes). Layers: roads,
mines, ruins, buildings, walls, units; burned terrain forces a full
redraw.

| Event | When | Change |
|---|---|---|
| unit damaged | start tick of its record | AP and AR written, units rebuilt (may switch the unit to mode 1) |
| unit destroyed | **end** of the unit's `E` clip (start tick if it has no clip) | unit removed; if the message carries a wreck name, a ruin object with that sprite is added on the unit's tile; ruins and units rebuilt |
| building damaged / repaired | start tick | AR written, buildings rebuilt (damage digit recomputed, instant swap) |
| building destroyed | start and end of its clip (26 frames, 25 ticks) | start: the building is **hidden** (its tiles are no longer drawn), buildings rebuilt; end: building removed, one ruin object per (tile, ruin name) pair of the message added, ruins and buildings rebuilt. Clip placement: unit-animation.md 3 |
| tower building destroyed | start tick (towers have no clip) | nothing is hidden first; removal and rubble at once |
| wall damaged | start tick | AR written, walls rebuilt (damage digit) |
| wall destroyed | start tick (no clip) | the four orthogonal neighbors get their broken-end flag (*Walls*), the wall is removed, walls rebuilt |
| road destroyed | start tick (no clip) | road removed, roads rebuilt |
| bridge built, new unit, unit updated | start tick (no clip) | data change, layer rebuilt |
| terrain burned | start tick (no clip), after the impact that caused it | the tile's terrain type is replaced, the new terrain sprite and its `Overlapping` flag are written into the cell, full redraw. An **instant sprite swap**: there is no fire or smoke animation |
| tech building taken / lost | start tick (no clip) | `LILBULB` (layer 16, mode 16) added or removed; when added, the interface sound `TECH` plays |
| mine detected | start tick | the mine is revealed for the detecting slot (*Mines*), mines rebuilt |
| mine exploded / destroyed | clip start: the mine's feature is removed; end: mines rebuilt | unit-animation.md 7 |

A hidden building is kept by a hide counter that starts at 0; its tile
features are put on the map only while the counter is ≥ 0. The destruction
clip's start lowers it by one.

## Deployment markers (bonus)

`DEPL<p><nn>` (`SYMBOLS.DBI`, layer 3): `p` = the **local player's color**
(`colorTable[local slot]`, *Color variant*), `nn` = two digits from the
neighbor mask above. The neighbor test was not traced.
It presumably checks whether the neighbor tile is in the same player's
deployment zone *(?)*. This table differs from the road/wall table:

| Mask | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 | 15 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| `nn` | 0 | 13 | 15 | 14 | 10 | 1 | 7 | 4 | 12 | 3 | 9 | 6 | 11 | 2 | 8 | 5 |

## Notes for OpenDay

- Build the color table per client as described (identity, then swap the
  local player's slot and chosen color). Single player needs nothing but
  the identity.
- Implement draw modes 0, 1 (units without enough AP to fire, deployment
  units, moved city objects) and the deferred bit 16; modes 3 / 4 can share
  the code of the city build cursor.
- Use the mine's `armed` flag for the suffix even though the two images are
  identical, so that modded data behaves as in the original.
- Mine visibility is evaluated for the local player only; when a human
  player detects a mine, clear all four human flags.

## Implementation notes (OpenDay)

Implemented in `game/map/` (`map_objects.*`, `map_renderer.*`),
`engine/render/indexed_surface.*` and `game/anim/` (`battle_view.*`,
`battle_animations.*`):

- Color table: `ColorTable` / `makeColorTable(localSlot, chosenColor)`.
  Units and buildings get `colorTable[owner]` when they are loaded or added;
  the selection triangles `SU00c` and the deployment markers `DEPL<c>…` use
  the same table. The game has no multiplayer yet, so the table is always
  the identity; a missing color-0 sprite simply draws nothing.
- Mines: `<ImageID>001` / `000` from `armed`. A battle shows a mine only if it
  is not hidden from the local player (`BattleView`; in the hot-seat viewer
  the human whose turn it is or was last). `MinesRevealed` for slot 0 clears
  `hidden_AI`, for any human slot all four human flags (`applyEvent`).
  OpenDay has no rule that disarms a mine.
- Damaged buildings: `isDamaged` compares `currentAR ≤ 0.33 × Armor` in
  floating point (also for walls). `BuildingDamaged` and repairs rewrite the
  building's tiles when the record is played. Tower buildings use the tower
  unit type's `facingsImageID` with facing 0 and no damage digit.
- Draw modes: `MapSprite::mode` / `Overlay::mode`, blitted by
  `IndexedSurface::blit(sprite, x, y, mode, clip)`; modes 6–15 draw normally.
  Units get mode 1 from their usable AP (`unitDrawMode`) or while they were
  placed in the deployment view (until the battle starts). Bit 16
  (`kDrawOnTop`) is used by `PATH0002`/`PATH0003`, `OBJT001`/`OBJT002`,
  `LILBULB` and the selection triangles; the interface markers carry the
  layer numbers of the table above (tile cursors: layer 8). The city build
  cursor still uses its own tinted copies instead of modes 3 / 4.
- The changes of an animation phase follow the table above, except that a
  triggered mine plays its clip once (with the explosion record; a
  `MineDestroyed` of the same mine right after it adds no second clip).

## Open questions

- (Answered: the player-slot → color table, the mine suffix (`armed`),
  the tower-building facing (always 0) and the draw-mode values, see the
  sections above.)
- Whether any server rule disarms a mine (which would show the `000`
  sprite); none was found.
- Who uses draw modes 2–5 (presumably interface overlays and cursors).
- What the original shows for an object whose color-0 static sprite does
  not exist (probably nothing).
- Exact events that trigger wall recomputation, and whether the quirk
  with repeated recomputation of broken isolated walls is visible in
  practice.
