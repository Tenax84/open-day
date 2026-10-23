# Unit animations on the tactical map

How the original program plays the animations of `ANMSUNIT\ANMSUNIT.FF`
on the isometric tactical map: which entry it opens for a unit and an
action, where the frames are placed, how the frames are timed, how a move
and a shot are sequenced, and what happens when something is destroyed.

Prerequisites: the container and Smacker format in
[../formats/FF.md](../formats/FF.md), the tile geometry in
[isometric-map.md](isometric-map.md), static sprites and draw order in
[map-objects.md](map-objects.md), and the `Unit Types` columns in
[data-model.md](data-model.md).

Status: the rules below come from studying the program's behavior. The
facing table was checked against the static `UB.DBI` sprites, and the
frame placement was checked by pixel-matching animation frames against
the matching static sprites. Items marked *(?)* are not fully confirmed.

## Terms

| Term | Meaning |
|---|---|
| tile origin | top-left corner of the tile's 54 × 56 sprite: `((x − y + n − 1) × 28, (x + y) × 14)` |
| tile center | tile origin + **(27, 42)**, the middle of the ground diamond |
| tick | one step of the global animation clock (15 per second, see *Timing*) |
| clip | one Smacker entry, played as one animation object |

## 1. Entry name

### Action letters and database columns

The program numbers the unit actions 0–8. Each number has a fixed letter
and a fixed `Unit Types` column that tells whether the clip exists:

| No. | Letter | Clip | Column | Directions |
|---|---|---|---|---|
| 0 | `L` | move start (take-off, diving, …) | `MoveStartAnimation_Begin` | 8 |
| 1 | `M` | move (looped while moving) | `MoveAnimation_Begin` | 8 |
| 2 | `N` | move end (landing, surfacing, …) | `MoveEndAnimation_Begin` | 8 |
| 3 | `S` | shoot begin (aim, deploy weapon) | `ShootBeginAnimation_Begin` | 8 |
| 4 | `T` | shot (one per shot of a burst) | `ShootShootAnimation_Begin` | 8 |
| 5 | `U` | shoot end | `ShootEndAnimation_Begin` | 8 |
| 6 | `P` | projectile | `ShootProjectileAnimation_Begin` | 1 |
| 7 | `I` | impact at the target | `ShootImpactAnimation_Begin` | 1 |
| 8 | `E` | unit explosion (destruction) | `ExplosionAnimation_Begin` | 1 |

When a `…_Begin` column is `−1` the name is empty: no clip is opened, and
the action runs without a picture (see *Missing clips*). On load, every
`…_Begin` value other than `−1` is reduced by one (stored 0-based). The
value is `1` for every shipped unit, so in practice it only acts as an
"exists" flag *(?: whether a value other than 1 would select a start
frame was not traced)*.

### Name pattern

```
<code> \ P<c> \ <code><c><letter><d>
```

`.smk` is appended before the archive lookup.

- `code` = `Unit Types.facingsImageID` (stored in mixed case, the lookup
  ignores case).
- `d` = facing digit `0`–`7` (next section). For `P`, `I` and `E` it is
  always `0`.
- `c` = color variant, derived from the unit's color value:
  1. For `P`, `I`, `E`: `c = 0` if the color value is 0, else `c = 1`.
     These clips exist in `P1`–`P4` but only `P0` and `P1` are ever used.
  2. Otherwise, if the unit type's `Race` is 0 (human), the color value is
     0, and `isConvoy` is false, then `c = 1`. Most human units have no
     `P0` clips.
  3. Otherwise `c` = the color value.

  The color value is the same per-unit color that selects the static
  `UB.DBI` sprite. How it is obtained from `playerOwner` is described in
  [map-objects.md](map-objects.md) *(?: same table)*.
- The facing digit is the unit's current facing, or an explicit facing
  given by the action (move steps, shots). A facing computed toward a
  target is always 0 for unit types with `Facings` = false.

Examples: an AI-owned Shubblar drone facing 3 that is hit → `BDN\P0\BDN0I0`;
a human trooper of color 2 aiming toward facing 6 → `HTRO\P2\HTRO2S6`.

## 2. Facing numbering

### Direction table

Facings are numbered clockwise on screen, starting at down-left. Each
facing belongs to one grid step `(dx, dy)` (`+x` = `PosX + 1`):

| Facing | Grid step (dx, dy) | Screen direction | Neighbor |
|---|---|---|---|
| 0 | (0, +1) | down-left | SW |
| 1 | (−1, +1) | left | W (diagonal) |
| 2 | (−1, 0) | up-left | NW |
| 3 | (−1, −1) | up | N (diagonal) |
| 4 | (0, −1) | up-right | NE |
| 5 | (+1, −1) | right | E (diagonal) |
| 6 | (+1, 0) | down-right | SE |
| 7 | (+1, +1) | down | S (diagonal) |

The **same numbering is used by the static `UB.DBI` sprites** (the facing
digit in `<code><c><f>00`) and by the `facing` column of `Units On Map`.
Checked visually (a truck faces down-left in `…0…`, shows its rear in
`…3…` and its front in `…7…`) and by pixel-matching static sprites against
animation frames of the same facing.

### Facing of a move step

Movement is 8-connected. A path step from tile A to tile B gets the facing
whose grid step in the table equals `B − A`. Every step of a path stores
its own facing.

### Facing toward a target (shots)

The facing toward a target tile is computed from the angle of the grid
vector:

1. `(dx, dy)` = shooter position − target position (note the order).
2. `a` = `atan2(dy, dx)` in degrees, moved into `[0, 360)`.
3. `s = (int(a) + 22) mod 360`.
4. Facing by sector: `s < 45` → 2, `< 90` → 3, `< 135` → 4, `< 180` → 5,
   `< 225` → 6, `< 270` → 7, `< 315` → 0, otherwise 1.

For the eight neighbor directions this gives exactly the table above.
Units whose type has `Facings` = false keep facing 0.

## 3. Placement on screen

### Rule

Every animation that belongs to one tile is drawn **with the center of the
frame on the tile center**:

```
frame_left = tile_origin_x + 27 − frame_width  / 2   (integer division)
frame_top  = tile_origin_y + 42 − frame_height / 2
```

| Frame size | Offset of the frame's top-left from the tile origin |
|---|---|
| 216 × 224 (normal units, `MINE\*.SMK`) | (−81, −70) |
| 432 × 448 | (−189, −182) |
| 208 × 164 | (−77, −40) |

There is no per-clip hotspot. This applies to all unit actions (`L M N S T
U`), the impact `I`, the unit explosion `E`, mine explosions and the
automated-unit clips. The tile is the unit's `PosX/PosY` (for multi-tile
units the footprint corner with the smallest x and y *(?)*). The −5 px
bridge offset of static units is **not** applied to animations *(?)*.

### Check against the static sprites

The static 54 × 56 sprite matches the 216 × 224 frames of the same unit,
color and facing best when its top-left corner is at **frame pixel
(80, 70)** (tested with `HTRK0`, `HTRO1`, `HMTK1`, `TLTK0`, facings 0, 1,
2, 3, 5, 6; the 432 × 448 artillery frames give (189, 183)). The program
places the frame at (−81, −70), so an animated unit appears 1 px to the
left of its static sprite. This is how the original behaves; a
reimplementation can use −80 to avoid the jump.

### Building explosions (`BUILDING\<ImageID>.SMK`)

Name: `Building\` + `Building Types.ImageID`, empty (no clip) if the
type's `ExplosionAnimation_Begin` is −1. The frame is centered on the
**center of the screen bounding box of all footprint tile sprites**:

- left = left edge of tile `(x0, y1)`, right = left edge of tile `(x1, y0)` + 54,
- top = top edge of tile `(x0, y0)`, bottom = top edge of tile `(x1, y1)` + 56,

with `x0 = PosX`, `x1 = PosX + SizeX − 1`, `y0 = PosY`,
`y1 = PosY + SizeY − 1` *(?: the building's stored rectangle is assumed
to be its footprint)*. Relative to the origin of the corner tile
`(PosX, PosY)` the center is

```
cx = 27 + 14 × (SizeX − SizeY)
cy = 28 +  7 × (SizeX + SizeY − 2)
```

e.g. (27, 42) for 2 × 2 and (27, 56) for 3 × 3. `ExplosionAnimation_Qty`
(25 for all buildings, 8 for mines) is presumably the last frame index of
the 26- or 9-frame clip *(?)*.

### Mine explosions

Name: `Mine\` + `Mine Types.ImageID` (`Mine`, `Gmine`), centered on the
mine's tile like a unit clip.

## 4. Timing

### The clock

All map animations share one counter of **ticks**. The counter advances
by one each time a dedicated clock clip, `INTERF\TICK.SMK` (216 × 224,
9 frames, 15 fps), advances a frame *(?: the link between the clock clip
and the counter is strongly suggested but not traced in full)*. So one
tick = 1/15 s = 66.7 ms. All unit clips also run at 15 fps.

Each clip additionally waits for its own Smacker frame timer before it
shows its next frame. With equal frame rates this means **one frame per
tick**.

### Animation objects

Every scheduled animation has:

- a **start tick** and an **end tick** = start + duration,
- a **duration**: for a clip, its number of frames − 1; for a move, the
  number of slide positions − 1; for a projectile, the number of path
  points − 1; `0` if there is no clip.

On every tick, for every animation in insertion order:

1. When the tick equals the start tick: run its *start action* (below) and
   compute its screen position.
2. While start ≤ tick: draw the current frame (advancing it if due).
3. When the tick equals the end tick: run its *end action*, then remove it.

A clip that is not looped stops drawing after its last frame. Moves loop
their clip.

### Static sprite while animating

A unit has a hide counter. Animations that begin an action decrease it at
their start, and the matching end animations increase it again at their
end. While it is decreased, the unit's static `UB.DBI` sprite is not drawn,
so the clip replaces it. When idle, **only the static sprite is shown**;
there is no idle animation.

## 5. Movement

### Sequence

One move order creates three animations:

| Part | Clip | Facing | Start | Start action | End action |
|---|---|---|---|---|---|
| begin | `L` | unit's current facing | `3 × (k − 1)` *(?: k = index of the action within the phase)* | hide static sprite | – |
| translate | `M`, all 8 facings loaded | per step | end of begin | – | – |
| end | `N` | facing of the last step | end of translate | – | put the unit on the destination tile, set its facing to the last step's facing, show the static sprite |

Units without `L`/`N` clips simply get zero-length begin/end parts, so the
translate starts immediately and the unit appears on the destination with
its new facing when the move is done.

### Slide positions

The translate part precomputes one screen position per tick:

1. Take the path (start tile plus every step).
2. Leading steps whose tiles are off-screen are dropped. If fewer than two
   tiles remain, the move is not animated *(?)*.
3. For each consecutive pair of tiles A → B:
   - P = top-left frame position for A, Q = for B (using the placement
     rule above and the size of the `M` frame).
   - Rasterize the straight line P → Q into `N = max(|Qx − Px|, |Qy −
     Py|) + 1` points (index 0 = P, index N − 1 = Q).
   - Emit the points with index `s, 2s, 3s, …` while the index is `< N − 1`,
     where `s = MoveAnimation_PixSpeed`. Each emitted position also stores
     B (the tile being entered) and the step's facing.
4. Duration = number of positions − 1.

So **`MoveAnimation_PixSpeed` is the number of pixels per tick, measured
along the longer screen axis.** The tile centers themselves are never
emitted. An orthogonal step (28 px horizontal, 14 px vertical) takes
`floor(27 / s)` ticks, e.g. 3 ticks for `s = 7…9`. A diagonal step (56 px
horizontal) takes `floor(55 / s)` ticks, about twice as long. Shipped
values are 4–13.

Teleporting units (`Movement` = 7) get a single-step path, and the
destination facing is random.

### Per tick during the translate

- The position is the next slide position.
- The clip is the `M` clip of that position's facing. Changing the facing
  switches clips, and each facing keeps its own frame counter.
- The `M` clip loops, independent of the distance moved. Ring frames make
  the loop seamless.
- The `M` clips of the 8 facings are opened muted. For sound, a separate
  copy of the facing-0 `M` clip is opened and played, looped, without
  drawing, so the engine sound does not restart when the facing changes.
- A per-step flag marks trooper steps into forest tiles (`Movement` = 2,
  terrain class 5) *(?: its effect during the move was not traced)*.

## 6. Shooting

### Sequence

One attack with `n` shots creates:

| Part | Clip | Start | Start action | End action |
|---|---|---|---|---|
| begin fire | `S` | as scheduled by the phase | set the unit's facing toward the target, hide static sprite | – |
| shot *i* (i = 1…n) | `T` | end of begin fire for i = 1, else end of shot *i−1* | – | – |
| projectile *i* | `P` | start of shot *i* + `ShootShootAnimation_ProjBegin` | – | – |
| impact *i* | `I` | end of projectile *i* | – | – |
| end fire | `U` | end of the last shot | – | show static sprite (with the new facing) |

The facing for `S`, `T`, `U` is computed toward the first shot's target
(see *Facing toward a target*). Shots do not wait for the impacts. **`ProjBegin`
is the number of ticks (= frames) after the start of the `T` clip at which
the projectile leaves** (shipped values 0–18, i.e. the muzzle frame).

### Projectile path

- Start: the tile at the center of the shooter's footprint. End: the target
  tile. Both are converted to the point **tile origin + (27, 14)**, which
  is 28 px above the tile center (a "muzzle height").
- The projectile is not shown if the unit has no `P` clip, or if the target
  is a diagonal neighbor (`|dx| = 1` and `|dy| = 1` in grid units).
- The line between the two points is rasterized into `N` points as for
  moves (one point per pixel along the longer axis).
- **Arc** (`ShootProjectileAnimation_Curve`, a percentage): if not 0, point
  `i` (1-based) is raised by `trunc(sin(π · i / N) × N × Curve / 100)`
  pixels. The peak height is `Curve`% of the path length in pixels.
  Shipped values: 0, 5–30.
- **Offset** (`…_PixOffset`, 10 for all units): the first `PixOffset`
  points are dropped, so the projectile appears that many pixels away from
  the shooter.
- Leading points that are off-screen are dropped.
- **Speed** (`…_PixSpeed`, 20 for all units): walking from the first point,
  a point is dropped when its distance to the last kept point is less than
  `PixSpeed` (larger of |dx|, |dy|). So **the projectile moves `PixSpeed`
  pixels per tick**.
- Each point is converted to a frame top-left position by subtracting half
  the `P` frame size. Duration = points − 1. One point is consumed per
  tick.
- For every point the heading toward a point up to 25 points further on is
  also computed *(?: no use found; the `P` clip has a single direction)*.
- The `P` clip plays once, frame by frame from the projectile's start.

### Impact

The `I` clip is centered on the target tile (placement rule above) and
starts when the projectile ends. With no projectile (zero duration), it
starts right at the projectile's start tick.

### Anti-aircraft fire

The program also plays a `T` clip for a unit shooting at an automated unit
(air strike). It computes the facing toward a screen point converted back
to a tile, hides the static sprite at its start, and at its end shows it
again with that facing.

## 7. Destruction

### Units

- Clip `E` (`<code>\P0|P1\<code><0|1>E0`), centered on the unit's tile.
- Start action: hide the unit's static sprite.
- End action: remove the unit from the map. If the destruction message
  carries a ruin name (8 characters), a ruin object with that sprite
  (`RUINS.DBI`) is placed on the unit's tile. The name comes from the
  server *(?: presumably `Unit Types.ruinImageID`, perhaps empty on water)*.

### Buildings

- Clip `BUILDING\<ImageID>.SMK`, placed as in section 3.
- Start action: hide the building *(?)*.
- End action: remove the building and place one ruin object on each tile
  of a list sent with the message (tile + ruin name per entry).

### Mines

`MINE\<ImageID>.SMK` on the mine's tile; at its start the mine object is
removed from the map *(?)*.

## 8. Drawing, sound and options

### Draw order and occlusion

Animations are **not** part of the depth-sorted cell pass of
[map-objects.md](map-objects.md). After the map is drawn, every running
animation is drawn on top of it in insertion order, with the same
color-key transparency as in [FF.md](../formats/FF.md).

To hide parts of the clip that should be behind objects in front of it,
the program then **redraws parts of the cells in front of the animated
tile**, clipped to fixed sub-rectangles of those cells. For each of these
cells it redraws the terrain sprite only if the terrain is a tall
(overlapping) one *(?: flag identity)*, and then all of the cell's
features (roads, walls, buildings, units, …).

For a unit animation at tile (x, y) the redrawn parts are (cell: sub-rectangle
x, y, width, height in the cell's sprite):

| Cell | Part |
|---|---|
| (x, y + 1) | 28, 0, 27, 42 (right half, upper part) |
| (x + 1, y) | 0, 0, 27, 42 (left half, upper part) |
| (x + 1, y + 1) | 0, 0, 54, 28 (upper half) |
| (x + 1, y + 2) | 28, 0, 27, 14 |
| (x + 2, y + 1) | 0, 0, 27, 14 |

During a move a similar set of seven parts is used around the tile being
entered, chosen by facing pair (0/4, 1/5, 2/6, 3/7) *(?: exact tables not
transcribed)*. A simpler approach for a reimplementation is to draw
animations inside the depth-sorted pass, on the cell of the tile being
entered.

### Palette

Each clip is opened with a color remap to the game palette *(?)*. Since the
clip palettes match `PAL.BMP` almost exactly, the frame indices can be used
as they are.

### Sound

The audio track embedded in a clip plays with the clip (through Miles in
the original) whenever game sound is on. The move sound comes from the
extra facing-0 `M` copy described above. Begin, shot, end, impact and
explosion clips play their own audio once.

### INI options

| Option | Section | Effect |
|---|---|---|
| `NoIsoAnim=1` | `[Fallen Haven II]` | No map animation is opened at all. Every animation has zero length and draws nothing, but the start and end actions still run (units jump, explosions just remove the unit and place the ruin). Any value other than 1 leaves animations on. |
| `ShowMoveAnim=0` | `[User]` | The move clips (`M`) are not used: the translate part has zero length and the unit appears on the destination at the end of the move. *(?: whether `L`/`N` are also suppressed)* |

## Validation against the shipped data

Every unit type of `GLOBAL.MDB`, every color value 0–4, every action whose
column is not −1, and every facing (27 535 names) was resolved with the
naming rule and looked up in `ANMSUNIT.FF`. All names exist except these
groups:

- Human units with color value 0 have no `P0` clips for the single-direction
  actions (`E`, `I`, `P`), since rule 1 picks `P0` for them.
- Units whose clips exist only in `P0` (for example `BCAN`, `BLAV`, `MCRY`,
  `TBP`, `TLAS`, `TLRT`, `TOUT`, `TTAA`, `MSWR`, `MTAA`) have no clips for
  color values 1–4. Some (`BTRK`, `HTRK`, `HTRB`, `TGTP`, `TWOR`) lack
  `M`/`E` for colors 1–4.

Either the color values these units can have are restricted, or the
original simply shows no picture. Every `BUILDING\<ImageID>.SMK` that the
building table refers to exists.

## Notes for OpenDay

Deliberate differences of the OpenDay implementation (`game/anim/`):

- When the computed clip does not exist, the variants `P1` and then `P0`
  are tried, so the gaps above still show a picture.
- Animations are drawn inside the depth-sorted cell pass, on the cell of
  the tile being entered (moves) or of the clip's tile, instead of being
  drawn on top and repaired with the occlusion tables. Projectiles are
  drawn on top of the map.
- The parts of a sequence follow each other one tick apart when the
  earlier part showed frames, so no two parts of one unit are visible in
  the same tick.
- The move sound plays once at the start of the move instead of looping.

## Open questions

- Exact link between `TICK.SMK` and the tick counter, and the start tick
  of each action within an animation phase (`3 × (k − 1)` for moves).
- Whether a `…_Begin` value other than 1 would select a start frame.
- Effect of the forest flag on move steps.
- Exact occlusion tables for moving units.
- What `ExplosionAnimation_Qty` controls.
- Exact color value → `P<c>` mapping for player slots 1–4 (shared with
  the static sprites).
