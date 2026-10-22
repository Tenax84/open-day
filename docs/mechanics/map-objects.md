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

- **Mines** (`MINES.DBI`): name = `Mine Types.ImageID` in upper case +
  `000` or `001`. A Boolean state of the mine selects the suffix. Most
  likely it is `armed`, but the mapping is not confirmed *(?)*. A mine is
  drawn only if it is not hidden from the viewing player. The flags
  `hidden_AI`, `hidden_1` … `hidden_4` are indexed by the viewing
  player's slot (0 = AI, 1–4).
- **Ruins** (`RUINS.DBI`): name = `Ruin Types.id`, one tile, no variants.

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
  `currentAR ≤ 0.33 × Armor` (the type's armor *(?: field identity
  inferred)*), otherwise `0`.
- `i` = offset along PosX (`0 … SizeX−1`), `j` = offset along PosY
  (`0 … SizeY−1`), each written as a decimal digit.

So the `<x><y>` digits of the sprite name are the offsets along PosX and
PosY respectively.

**Towers** (`TransformIntoUnit` ≠ 65535) use the unit naming scheme
instead: `<ImageID><c><facing><i><j>`. Where the tower's facing comes from
was not traced *(?)*.

### Color variant

The color digit is not `playerOwner` itself. The program looks it up in a
per-game **player-slot → color** table: `c = colorTable[playerOwner]`.
Observed in all shipped scenarios:

- Tauran, Shubblar and Shadamar buildings are always owned by the AI
  (`playerOwner` 0), and only color variant `0` exists for them. So the
  AI slot maps to `0`.
- Human buildings are always owned by player slots 1–4 and exist in
  variants `1`–`4` (some also in `0`).

How slots 1–4 map to colors was not traced. Most likely each slot uses the
color the player chose in the user list, and by default slot *n* uses
color *n* *(?)*. Until this is confirmed, `c = playerOwner` is a safe
choice for slot ≥ 1, and `c = 0` for the AI slot.

## Units (for reference)

- Name: `<facingsImageID><c><f><i><j>`. `c` comes from the same color
  lookup as for buildings *(?)*, `f` = facing digit `0`–`7`, and `i`, `j`
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

Each feature carries a mode value. Bit 4 (16) selects a special drawing
path that the UI markers use (`OBJT`, `LILBULB`, selection) *(?:
probably translucent/blinking)*. The low 4 bits select one of several
blit routines. Roads, walls, mines and buildings pass a per-object field
that is normally 0. Units pass 0 or 1 depending on their state *(?)*.
For a first implementation, draw everything with mode 0 (plain
transparent blit).

## Deployment markers (bonus)

`DEPL<p><nn>` (`SYMBOLS.DBI`, layer 3): `p` = player slot, `nn` = two
digits from the neighbor mask above. The neighbor test was not traced.
It presumably checks whether the neighbor tile is in the same player's
deployment zone *(?)*. This table differs from the road/wall table:

| Mask | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 | 15 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| `nn` | 0 | 13 | 15 | 14 | 10 | 1 | 7 | 4 | 12 | 3 | 9 | 6 | 11 | 2 | 8 | 5 |

## Open questions

- Content of the player-slot → color table for slots 1–4 (user-chosen
  colors).
- Which mine state selects `…001`.
- Facing source for tower buildings; meaning of the draw-mode values.
- Exact events that trigger wall recomputation, and whether the quirk
  with repeated recomputation of broken isolated walls is visible in
  practice.
