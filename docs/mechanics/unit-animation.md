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
the action runs without a picture. On load, every `…_Begin` value other
than `−1` is reduced by one (stored 0-based), but afterwards the value is
only ever tested for "−1 = no clip". Every clip is opened from its first
frame to its last and played from frame 0, so **a `…_Begin` value other
than 1 has no effect**; the column is purely an "exists" flag (it is 1 for
every shipped clip). The same holds for buildings and mines.

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
  `UB.DBI` sprite: `colorTable[playerOwner]`, see
  [map-objects.md](map-objects.md) *Color variant*.
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
`y1 = PosY + SizeY − 1` (the building's footprint, the same rectangle that
drives its per-tile sprites). The frame's top-left is
`((left + right) div 2 − w div 2, (top + bottom) div 2 − h div 2)`.
Relative to the origin of the corner tile
`(PosX, PosY)` the center is

```
cx = 27 + 14 × (SizeX − SizeY)
cy = 28 +  7 × (SizeX + SizeY − 2)
```

e.g. (27, 42) for 2 × 2 and (27, 56) for 3 × 3. The building clips have
26 frames (420–432 × 312–428 px), so they last 25 ticks.

`ExplosionAnimation_Qty` (25 for all buildings, 8 for mines) is **never
read** by the program. Its values equal "frame count − 1" of the clips; it
is documentation inside the database only. Durations always come from the
clip itself.

### Mine explosions

Name: `Mine\` + `Mine Types.ImageID` (`Mine`, `Gmine`) + `.smk`; no clip
if `Mine Types.ExplosionAnimation` is −1 (both shipped mines have 0, i.e. a
clip; the value is only an "exists" flag). Clips `MINE\MINE.SMK` and
`MINE\GMINE.SMK`: 216 × 224, **9 frames**, 15 fps, 22050 Hz 8-bit mono
audio; duration 8 ticks. Centered on the mine's tile like a unit clip
(frame top-left at tile origin + (−81, −70)).

## 4. Timing

### The clock

`INTERF\TICK.SMK` (216 × 224, 9 frames, 15 fps) is the **engine clock**.
The application opens it once at start-up (failure: string 274) and never
draws it. On every pump of the animation engine the clock clip is asked
whether its next frame is due (Smacker frame timer, 15 fps); if so it
advances, and the result "ticked: yes / no" is passed to every registered
listener. So one tick = 1/15 s = 66.7 ms.

The animation list of a battle's animation phase is one such listener (a
new list is created for each phase). Its tick counter starts at −1, so the
first tick of a phase is tick 0.

- **Not ticked**: every animation is called without a target surface;
  clips may advance their own frame timers, nothing is drawn.
- **Ticked**: the counter is increased by one, then in this order:
  1. if the list was already empty at the beginning of this tick, the
     animation phase ends;
  2. sort the animations (8, *Draw order and occlusion*);
  3. collect the screen rectangles of all animations and restore the map
     under them;
  4. for every animation: if tick = its start tick, run its start action
     and compute its position;
  5. on tick 0, request a full map redraw;
  6. rebuild the map layers requested by the start actions
     (map-objects.md, *Changes during an animation phase*);
  7. draw every started animation, each followed by its occlusion repair
     (8);
  8. copy the changed rectangles to the screen;
  9. for every animation: if tick = its end tick, run its end action and
     remove it;
  10. rebuild the layers requested by the end actions.

All clips run at 15 fps and advance by their own Smacker timer, so in
practice they show **one frame per tick**.

### Animation objects

Every scheduled animation has:

- a **start tick** and an **end tick** = start + duration,
- a **duration**: for a clip, its number of frames − 1; for a move, the
  number of slide positions − 1; for a projectile, the number of path
  points − 1; `0` if there is no clip.

Per tick (see *The clock* for the full order): at the start tick the
*start action* runs and the screen position is computed; while
start ≤ tick the current frame is drawn; at the end tick the *end action*
runs and the animation is removed.

A clip that is not looped stops drawing after its last frame. Moves loop
their clip.

### Start ticks within a phase

The server sends one animation phase as one message made of records. Each
record has a **record id** (1, 2, 3, … in the order the server added them),
the id of its **parent** record (or none) and an "extra" number. On the
client a record is applied, then every record whose parent is that record,
in turn (a tree).

- **Movement**: the begin part starts at tick **`3 × (record id − 1)`**,
  regardless of any parent. The translate starts at the end of the begin
  part, the end part at the end of the translate. So the units of a group
  order start sliding 3 ticks apart; the record ids also count the
  reaction-fire and mine records inserted between the moves.
- **Every other record** starts at the end tick of its parent (0 when it
  has no parent). The end tick a parent offers:
  - a movement: the end of its end part **+ 1**;
  - a shooting record: the end of its impact number *extra*;
  - damage, repair, bridge and similar records: the end of their single
    animation;
  - a mine explosion: its own end.
- A **mine explosion record itself** starts at its parent's end **+ 1**
  (the only record type with this +1).
- Inside a shooting record the parts follow section 6.

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
| begin | `L` | unit's current facing | `3 × (record id − 1)` (*Start ticks within a phase*) | hide static sprite | – |
| translate | `M`, all 8 facings loaded | per step | end of begin | – | – |
| end | `N` | facing of the last step | end of translate | – | put the unit on the last tile of the path (unchanged if the path is empty), set its AP to the value of the record, set its facing to the last step's facing (0 for an empty path; only for types with `Facings`), show the static sprite |

Units without `L`/`N` clips simply get zero-length begin/end parts, so the
translate starts immediately and the unit appears on the destination with
its new facing when the move is done.

### Slide positions

The translate part precomputes one screen position per tick:

1. Take the path (start tile plus every step).
2. Leading steps whose tiles are off-screen are dropped. If fewer than two
   tiles remain, there is no translate part.
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
- The clip is the `M` clip of that position's facing. When the facing
  differs from the previous tick's, the **previous facing's clip is rewound
  to its first frame**. Since every clip that is not current has been
  rewound, a new facing always starts its `M` clip **from frame 0**.
- The `M` clip loops, independent of the distance moved. Ring frames make
  the loop seamless.
- The `M` clips of the 8 facings are opened muted. For sound, a separate
  copy of the facing-0 `M` clip is opened and played, looped, without
  drawing, so the engine sound does not restart when the facing changes.
- Each slide position also stores a flag "this step is not a trooper step
  into forest" (`Movement` = 2, terrain class 5). It is never read: it has
  **no visible effect** during the slide (the `M` clips have no forest
  variant). The forest pose only appears in the static sprite after the
  move, when the end part shows the static sprite again (map-objects.md,
  letters `A`–`H`).
- The translate exists only if the `ShowMoveAnim` option is on **and** the
  unit type has an `M` clip.

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

- Clip `BUILDING\<ImageID>.SMK`, placed as in section 3; none if
  `ExplosionAnimation_Begin` is −1 (the four towers).
- Start action: if there is a clip, hide the building (its tiles disappear
  from the map, the clip replaces them); rebuild the buildings layer.
- End action: remove the building and place one ruin object on each tile
  of a list sent with the message (tile + ruin name per entry); rebuild
  the ruins and buildings layers.
- Without a clip (towers) nothing is hidden and the removal and the rubble
  happen at the start tick (duration 0).

### Mines

Three records concern mines on the client:

| Record | Animation | Effect |
|---|---|---|
| mine detected | none | at its start tick the mine is revealed for the given slot (map-objects.md, *Mines*), mines layer rebuilt |
| mine explosion | mine clip | starts at its parent's end **+ 1** |
| mine destroyed | mine clip + removal | the clip, and at the same start tick the mine is removed from the client's scenario, mines layer rebuilt |

Mine clip (section 3, *Mine explosions*):

- Start action: if there is a clip, remove the mine's sprite from its tile
  (layer 4); request a mines-layer rebuild. End action: mines-layer rebuild.
- Because a rebuild re-adds every mine still in the client's list, a mine
  that only received an *explosion* record (without a *destroyed* record)
  reappears at the end of the clip *(?)*.
- Drawn on top of everything (draw-order group 1, no occlusion repair).
- The clip's own audio track plays when game sound is on.

A triggered mine: the server adds an explosion record and then applies the
normal damage to the tile, which destroys the mine (a *destroyed* record).
Whether the client therefore plays the clip twice depends on how the two
records are linked *(?)*.

### Wrecks and other effects

- A unit's wreck appears at the **end** of its `E` clip (at its start tick
  if it has no clip), section *Units* above.
- Burned terrain, wall and road destruction, damage, repairs, new units,
  bridges and tech markers have **no clip**: they are instant changes at
  the record's start tick (map-objects.md, *Changes during an animation
  phase*). There are no fire, smoke or damage overlays anywhere.
- Automated units have their own clips (`AUTOUNIT`), not covered here.

## 8. Drawing, sound and options

### Draw order and occlusion

Animations are **not** part of the depth-sorted cell pass of
[map-objects.md](map-objects.md). After the map is drawn, the running
animations are drawn on top of it, with the same color-key transparency as
in [FF.md](../formats/FF.md).

**Sort order.** On every tick, before drawing, the animation list is
sorted by

1. the draw-order group: 0 ("ground") before 1 ("floating");
2. then the frame's top y (smaller first);
3. then the frame's left x (smaller first).

Group 0: the unit clips (begin / end move, begin fire, shots, end fire,
anti-aircraft fire), the translate (move), and the animations without a
clip. Group 1: projectiles, impacts, unit / building / mine explosions,
automated-unit clips and the other free-floating clips.

**Occlusion repair.** Each group-0 animation draws its frame and
immediately afterwards **redraws parts of the map cells in front of it**
(its *refresh list*); then the next animation is drawn. Group-1 animations
have no refresh list, so they always appear on top of everything.

A refresh entry is a cell (x, y) and a sub-rectangle (rx, ry, w, h) inside
that cell's 54 × 56 sprite. For each entry whose cell is on the map:

1. the terrain sprite is redrawn, clipped to the sub-rectangle, **only if
   the cell's terrain is `Overlapping`** (`Terrain Types.Overlapping`);
2. then every feature of the cell in layer order, clipped to the
   sub-rectangle; features with draw-mode bit 16 are again deferred to
   the end.

Cells outside the map are skipped.

**Unit clips at rest** (begin / end move, begin fire, shots, end fire,
anti-aircraft fire). The list is built once, at the start tick, from the
unit's tile A = (x, y) (its `PosX/PosY`):

| Cell | rx, ry, w, h |
|---|---|
| (x, y + 1) | 28, 0, 27, 42 |
| (x + 1, y + 1) | 0, 0, 54, 28 |
| (x + 1, y + 2) | 28, 0, 27, 14 |
| (x + 1, y) | 0, 0, 27, 42 |
| (x + 2, y + 1) | 0, 0, 27, 14 |

(The original lists the (x + 1, y + 1) part twice; once is enough.)

**Translate (move).** Rebuilt every tick for the current slide position,
from the tile being entered B = (Bx, By) and the step's facing. Each facing
pair uses one table, expressed in terms of the two tiles of the step:

*Facings 0 and 4* (steps along y). A = the tile of the step with the
smaller y (facing 0: A = (Bx, By − 1); facing 4: A = B); A = (ax, ay):

| Cell | rx, ry, w, h |
|---|---|
| (ax + 1, ay) | 0, 0, 27, 56 |
| (ax, ay + 2) | 28, 0, 27, 42 |
| (ax + 1, ay + 1) | 0, 0, 54, 42 |
| (ax + 1, ay + 2) | 0, 0, 54, 28 |
| (ax + 2, ay + 1) | 0, 0, 27, 28 |
| (ax + 1, ay + 3) | 28, 0, 27, 14 |
| (ax + 2, ay + 2) | 0, 0, 54, 14 |

*Facings 2 and 6* (steps along x). F = the tile with the smaller x
(facing 2: F = B; facing 6: F = (Bx − 1, By)); F = (fx, fy):

| Cell | rx, ry, w, h |
|---|---|
| (fx, fy + 1) | 28, 0, 27, 56 |
| (fx + 2, fy) | 0, 0, 27, 42 |
| (fx + 1, fy + 1) | 0, 0, 54, 42 |
| (fx + 2, fy + 1) | 0, 0, 54, 28 |
| (fx + 1, fy + 2) | 28, 0, 27, 28 |
| (fx + 3, fy + 1) | 0, 0, 27, 14 |
| (fx + 2, fy + 2) | 0, 0, 54, 14 |

(the mirror image of the 0 / 4 table: x and y swapped, rx mirrored.)

*Facings 1 and 5* (screen-horizontal diagonal). C = the tile of the step
with the smaller x (facing 1: C = B; facing 5: C = (Bx − 1, By + 1)); the
other tile is (cx + 1, cy − 1). C = (cx, cy):

| Cell | rx, ry, w, h |
|---|---|
| (cx, cy + 1) | 28, 0, 27, 42 |
| (cx + 1, cy) | 0, 0, 54, 42 |
| (cx + 2, cy − 1) | 0, 0, 27, 42 |
| (cx + 1, cy + 1) | 0, 0, 54, 28 |
| (cx + 2, cy) | 0, 0, 54, 28 |
| (cx + 1, cy + 2) | 28, 0, 27, 14 |
| (cx + 2, cy + 1) | 0, 0, 54, 14 |
| (cx + 3, cy) | 0, 0, 27, 14 |

*Facings 3 and 7* (screen-vertical diagonal). E = the tile of the step
with the larger x and y (facing 3: E = (Bx + 1, By + 1); facing 7: E = B).
The table is the at-rest table above applied to E.

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
| `ShowMoveAnim=0` | `[User]` | The move clips (`M`) are not used: there is no translate part and the unit appears on the destination at the end of the move. The `L` / `N` clips still play *(?: follows from the program structure, not seen in play)*. |

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
  drawn on top of the map. (The original's sort order and refresh tables
  in section 8 are complete now, should exact fidelity be wanted.)
- The tick clock does not need `TICK.SMK`: a 15 Hz timer gives the same
  ticks. Movement begin parts should still start at `3 × (record id − 1)`
  so that group moves are staggered as in the original.
- On a facing change during a slide the new facing's `M` clip restarts at
  frame 0.
- The parts of a sequence follow each other one tick apart when the
  earlier part showed frames, so no two parts of one unit are visible in
  the same tick.
- The move sound plays once at the start of the move instead of looping.

## Open questions

- (Answered: `TICK.SMK` is the engine clock; movement start ticks are
  `3 × (record id − 1)`; `…_Begin` is only an exists flag;
  `ExplosionAnimation_Qty` is unused; the forest flag has no effect; the
  occlusion tables and the color table are given above and in
  map-objects.md.)
- Whether a triggered mine shows two explosion clips (explosion and
  destroyed records) or the record links make them coincide.
- Whether `L` / `N` really still play with `ShowMoveAnim` = 0.
- Root records (no parent) other than movements start at tick 0; the code
  that schedules the root records was not located.
- The full-redraw request on tick 0 and after burned terrain: presumably
  it only forces a redraw of the whole map.
