# Encyclopedia

The in-game reference of unit, automated-unit, building, wall and mine
types (`CEncyclopInterface`): where it appears, its controls, the categories
and filters, the order in which entries are visited, and the exact content
of every page type.

Prerequisites: generic widgets, fonts and the string table in
[interface.md](interface.md); the type tables and their columns in
[data-model.md](data-model.md); the atlas frames in
[../formats/INTERFACE-SMK.md](../formats/INTERFACE-SMK.md); the list icons
(`LISTBOX.DBI`) in [../formats/DBI.md](../formats/DBI.md).

Conventions as in [tactical-interface.md](tactical-interface.md):
positions are pixels `(x, y)` relative to the dialog's top-left corner
unless stated otherwise, sizes are w×h, "frames a–d" are consecutive atlas
frames (4 for push buttons, 6 for toggles). String ids refer to the
language DLL string table; hot keys are the first character of the given
string. `<lang>` stands for the language suffix of a text column
(`English`, `German`, `French`, data-model.md).

Status: derived from studying the original program and checked against the
extracted pictures and the shipped databases. Items marked *(?)* are not
fully confirmed.

## 1. Placement and art

- The dialog is a **fixed screen rectangle (40, 40)–(580, 455)**, 540×415.
  It is neither centred nor moved at 1024×768: the same screen rectangle is
  used at every resolution.
- Background: atlas frame **382** (540×415), drawn opaque as the dialog
  panel. The captions HUMANS, ALIENS, UNITS, AUTOMATED UNITS, BUILDINGS and
  DEFENSES are part of this art (to the right of each lamp, section 2).
- The encyclopedia is a modal dialog pushed on top of the current screen;
  only one can be open at a time. While it is the active interface the
  program holds a global "suspend" counter of the application (decremented
  on activation, incremented on deactivation) *(?: most likely it pauses
  the animations / palette cycling of the screen below)*.
- **Pictures**: `ENCYCL\ENCYCL.SMK` (436×352, 126 frames), opened once when
  the dialog is created. The picture of the current entry is the Smacker
  frame whose 0-based index equals the entry's `EncyclopediaFrame` (checked:
  frame 40 is the Headquarters picture, whose `EncyclopediaFrame` is 40).
  It is drawn at dialog **(98, 8)** (screen (138, 48)), into the black
  window of the art.
- Each picture shows the 3D model on the left and dark / wire-frame
  artwork on the right with a coloured title band at the top. **All text
  and icons of a page are drawn over the picture** with a transparent
  background (no box is cleared behind them).

## 2. Controls

| Control | Position | Size | Frames | Key | Action |
|---|---|---|---|---|---|
| BACK | (22, 382) | 53×17 | 383–386 | `Esc` | close the encyclopedia |
| previous ◀ | (22, 350) | 19×17 | 387–390 | string 101 (`P`) | previous entry |
| next ▶ | (56, 350) | 19×17 | 391–394 | string 102 (a space in the English DLL) | next entry |
| HUMANS lamp | (6, 15) | 19×19 | 395–400 | string 118 (`H`) | race filter = human |
| ALIENS lamp | (6, 62) | 19×19 | 395–400 | string 119 (`A`) | race filter = alien |
| UNITS lamp | (104, 366) | 19×19 | 395–400 | string 120 (`U`) | category: units |
| AUTOMATED UNITS lamp | (104, 390) | 19×19 | 395–400 | string 121 (`X`) | category: automated units |
| BUILDINGS lamp | (355, 366) | 19×19 | 395–400 | string 114 (`B`) | category: buildings |
| DEFENSES lamp | (355, 390) | 19×19 | 395–400 | string 115 (`D`) | category: walls and mines |

All six lamps are six-frame toggles using the same frames 395–400 (red =
off, green = on). Frames 401–432 of the same atlas block are not used by
the encyclopedia *(?: no user found)*.

Lamp behaviour:

- The two race lamps form one exclusive group, the four category lamps
  another. Pressing a lamp switches every lamp of its group off and itself
  on; pressing a lamp that is already lit keeps it lit (and still performs
  its action).
- **Race lamp**: sets the race filter (human or alien) and then performs a
  **next** step (section 4) in the current category, i.e. moves to the
  first entry *after* the current one that passes the new filter.
- **Category lamp**: discards the current page and opens the page of that
  category at its **first** entry that passes the race filter. DEFENSES
  starts with the walls.

There is no scrolling list of entries; the only navigation is ◀ / ▶ and the
lamps. No sound of its own was found; the buttons behave like any push
button of interface.md *(?: generic click sound, if any)*.

## 3. Opening the encyclopedia

The encyclopedia is opened by the emblem button (`F1`) of the game screens
and by the encyclopedia button of the army screen. It is **not** reachable
from the main menu (interface.md). What it shows first depends on the
screen:

| Screen | Initial entry | Context (6) |
|---|---|---|
| tactical view (`CTacticView`) | unit page of the **lead selected unit's type**; nothing selected → default | none |
| deployment / reinforcement view (`CFirstDeployView`, `CDeployView`) | unit page of the **selected list entry's** type; none → default | none |
| waiting view (`CTacticWaitView`) and any other view without its own rule | default | none |
| automated-unit view (`CAutomatView`) | automated page of the selected automated type; none → default | none |
| tactical map screen (`CMapScreenTactic`, tactical-interface.md 7) | unit page of the selected list row's type; none → default | none |
| city map screen (`CMapScreenStrat`, strategic-interface.md 4.2) | building page of the selected list row's type; none → default | none |
| army screens (strategic, skirmish, deployment variants; army-tech.md 4) | unit page of the selected unit-list row; else automated page of the selected automated-list row; else default | strategic army screen: the colony list *(?)*; the other variants: none *(?)* |
| continent screen (normal, continue and win variants) | default | the current colony's building list |
| city screen (`CStratView`) | the selected structure list entry (3.1) | the current colony's building list |

**Default** = unit page, race filter human, the first unit that passes the
filters (section 4); lamps HUMANS and UNITS lit.

**Opening at a unit type**: the row (type, tech level 0) of `Unit Types`
is looked up; if it does not exist the default page is used. The race
filter becomes that row's `Race` (any non-zero value counts as alien), the
ALIENS lamp is lit if it is non-zero (otherwise HUMANS), and UNITS is lit.
The tech level shown is **not** the level of the selected unit but the
viewing player's level for the type (5.1).

**Opening at an automated type**: race filter = the type's `Race`;
category AUTOMATED UNITS. **At a building type**: race filter = the
building's `Race`; category BUILDINGS. **At a wall or mine type**: race
filter stays human; category DEFENSES.

An entry opened this way is shown **even if the filters of section 4 would
skip it** (the initial entry is not tested). Only ◀ / ▶ and the lamps apply
the filters.

### 3.1 City screen selection → page

The city screen's structure list has two modes (Buildings / Defenses radio,
city-building.md 9):

- Buildings mode: the selected row is a building type → building page.
- Defenses mode: the rows belong to four consecutive groups (city-building.md
  defines their order). By group:
  - the road group → default page (roads have no page) *(?: group identity
    inferred)*;
  - the tower group: if the building type turns into a unit
    (`TransformIntoUnit` ≠ 65535) → the unit page of that unit type,
    otherwise the building page;
  - the wall group → wall page;
  - the mine group → mine page.
- Nothing selected → default page.

## 4. Entry lists, filters and navigation

### 4.1 Lists

There are five page kinds. Each walks one type table **in load order**,
i.e. the physical row order of the MDB table (the `Counter` order of the
converted SQLite files; no sorting):

| Category | Page | Table | Race filter | Skipped entries |
|---|---|---|---|---|
| UNITS | unit page (5.1) | `Unit Types` | `Race` | `EncyclopediaFrame` = −1 (of the row at the displayed level) |
| AUTOMATED UNITS | automated page (5.2) | `Automated Units` | `Race` | `EncyclopediaFrame` = −1 |
| BUILDINGS | building page (5.3) | `Building Types` | `Race` | `EncyclopediaFrame` = −1, and every type with `TransformIntoUnit` ≠ 65535 (towers; they appear as units) |
| DEFENSES, part 1 | wall page (5.4) | `Wall Types` | none | `EncyclopediaFrame` = −1 |
| DEFENSES, part 2 | mine page (5.5) | `Mine Types` | none | `EncyclopediaFrame` = −1 |

Roads have **no** encyclopedia page.

**Race filter test**: an entry passes if its race is 4, or the filter is
4, or both are 0, or both are non-zero. So HUMANS shows race 0, ALIENS
shows races 1, 2 and 3 (Tauran, Shubblar, Shadamar) mixed in table order.
(No shipped row uses race 4.)

### 4.2 Steps

For automated units, buildings, walls and mines:

- **first**: from the head of the list, the first entry that passes;
- **next**: walk forward from the current entry to the next one that passes;
- **previous**: walk backward from the current entry;
- **last**: from the tail backward, the first entry that passes.

Running off the end with **next**: automated units and buildings wrap to
their own *first* entry; walls switch to the **mine** page at its *first*
entry; mines switch to the **wall** page at its *first* entry.
Running off the start with **previous**: automated units and buildings go to
their own *last* entry; walls switch to the mine page at its *last* entry;
mines to the wall page at its *last* entry.

So DEFENSES cycles Concrete, Laser Fence, Force Field, Mine, Grav Mine,
Concrete, … (Ruined Walls has frame −1 and is skipped).

Units: wrap to the first unit (next) or the last unit (previous); the step
itself has a defect, 4.3.

### 4.3 Unit steps (original defect)

`Unit Types` has one row per (`typeID`, tech level), and the rows of one
type are **not contiguous** in the shipped table (e.g. the Commando rows
are spread over the whole table). The unit page remembers only the
`typeID` of its current entry. A next / previous step in the original:

1. finds the row (current `typeID`, level 0) by searching from the head of
   the table;
2. from that row walks forward (next) or backward (previous) and stops at
   the first row whose `typeID` differs; that row's `typeID` is the new
   entry;
3. if the end (start) of the table is reached first, the step reports "no
   further entry" and the page wraps (4.2).

The filters (race of the displayed row, frame ≠ −1) are then tested on the
new type; if it fails, the step is repeated from there. **first** starts at
the `typeID` of the head row and steps if it fails; **last** starts at the
tail row.

Because step 2 starts from the level-0 row instead of the type's last row,
the walk can jump back into types it has already visited. On the shipped
`GLOBAL.MDB` (simulated from the table order) ▶ from the first human unit
visits Commando, Grenadier, LAV, Light Tank, Adv Launcher, Main Tank,
Patrol Boat, Attack Boat, Submarine, Freighter, Truck, Trooper, Buggy and
then returns to Commando: Heavy Tank, Artillery, Grav Bike, AA Vehicle, the
towers, Chopper and Elite Trooper are never reached with ▶ (◀ follows a
different cycle). The alien ▶ cycle is Skirmisher, Transport, Light Tank,
Cut Throat, Pyro Maniac, Footman, Mind Warrior, Pack Freak.

**OpenDay** does not reproduce this (see *Notes for OpenDay*): one entry per
`typeID`, in the order of the type's first row in the table, then the
filters. On the shipped data this gives, for humans: Commando, Trooper,
Buggy, LAV, Light Tank, Main Tank, Heavy Tank, Artillery, Adv Launcher,
Grav Bike, AA Vehicle, Attack Boat, Patrol Boat, Submarine, AA Tower, Laser
Tower, Rocket Tower, Guard Tower, Grenadier, Freighter, Truck, Chopper,
Elite Trooper. The extra convoy types 61–66 (Freighter, Truck, Junk Wagon
variants) have frame −1 and drop out. Convoys with a frame and tower units
are listed like any other unit.

## 5. Page content

Drawing order of the dialog: the picture (1), the controls (2), the page
text, then the entry's **list icon**: the 38×38 `LISTBOX.DBI` sprite
`<code>999` (the same icon as in the army, deployment and city lists;
`code` = `facingsImageID` for units and automated units, `ImageID` for
buildings, `img_id` for walls, `listboxImageID` for mines) at dialog
**(492, 37)** (= right edge − 48, top + 37), drawn opaque. It is updated
whenever the page changes entry.

Text, all pages: standard font (Arial 14), white, transparent background;
every line is clipped to the page's text rectangle.

- **Name**: right-aligned so that it ends at dialog x = **530**, top
  y = **20**, clipped to (210, 20)–(530, 40). Source: `Name<lang>` (units,
  automated units, buildings) or `name<lang>` (walls, mines).
- **Label column** x = **210**, **value column** x = **330**; the unit page
  has a second value column at x = **470**. Lines are 18 px apart, groups
  25 px.
- **Description**: `Encyclopedia<lang>`, word-wrapped at word boundaries
  (no `&` prefix processing), clipped to its box, **no scrolling** (long
  texts are cut off): units (300, 275)–(530, 355); all other pages
  (300, 275)–(530, 385).
- Clip rectangle of the stat lines: x 210 … 530, bottom 355 (units) or 385
  (other pages).

**PREREQUISITE** lines are drawn only when the encyclopedia was opened with
a context (section 6). On the unit and building pages the line's place
stays reserved when it is not drawn (the following lines do not move up);
on the automated page see 5.2.

### 5.1 Unit page

Rows of `Unit Types` used for the current type T:

- *cur* = the row (T, L), where L = the viewing player's tech level for T
  if the player's tech table has T, else 0;
- *max* = the row (T, n − 1), where n = the number of consecutive levels
  0, 1, 2, … present for T (counting stops at the first missing level, at
  most 11);
- *next* = the row (T, L + 1) if L < n − 1, else *cur*.

Lines (dialog y; label at x = 210 unless noted):

| y | Label (string) | Value at x = 330 | Value at x = 470 |
|---|---|---|---|
| 60 | TYPE (22) | string 31 + `Category`: GRAV (31), GROUND (32), WATER (33) | — |
| 78 | PREREQUISITE (30), context only | prerequisite text of *cur* (6) | — |
| 96 | DEPLOY COST (37) | `Cost` of *cur* | — |
| 121 | — | ACTUAL (223) | MAXIMUM (225) |
| 139 | LEVEL (23) | L + 1 | n |
| 157 | ARMOR (24) | `Armor` of *cur* | `Armor` of *max* |
| 175 | ACTION POINTS (25) | `actionPoints` of *cur* | of *max* |
| 200 | ARMAMENT (26) | `weaponName<lang>` of *cur* | — |
| 218 | at x = 240: AVG DAMAGE (27), or DAMAGE AA (235) if `IsAA` | `"%d - %d"`: `damageBase` and `damageBase + damageRandom`; for AA units `DamageAA` and `DamageAA` | the same for *max* |
| 236 | at x = 240: ROF (28) | `actionPoints div actionPointsShoot` of *cur* | of *max* |
| 254 | at x = 240: RANGE (29) | `weaponRange` of *cur* | of *max* |

Deploy cost: the program uses its general deploy-cost formula
`trunc(Cost × (1 + 0.2 × k))` with k = 0 here, i.e. simply `Cost`.

**Gauges**: six horizontal bars, 78×14, at x = **385** and y = 141
(level), 159 (armor), 177 (action points), 220 (damage), 238 (ROF), 256
(range), i.e. 2 px below their text line, between the two value columns.
Each gauge gets three numbers (M, A, B):

| Gauge | M (maximum) | A (current) | B (next) |
|---|---|---|---|
| level | n − 1 | L | level of *next* |
| armor | `Armor` of *max* | of *cur* | of *next* |
| action points | `actionPoints` of *max* | of *cur* | of *next* |
| damage | average damage of *max* | of *cur* | of *next* |
| ROF | `actionPoints div actionPointsShoot` of *max* | of *cur* | of *next* |
| range | `weaponRange` of *max* | of *cur* | of *next* |

Average damage = `damageBase + damageRandom div 2` (integer); it is used for
the damage gauge also for AA units.

Gauge drawing (inner area (2, 1)–(76, 13), 74×12, relative to the gauge):

1. Draw the gauge background bitmap over the whole 78×14 (a yellow bar
   with a border).
2. From the inner left edge draw the **red** bar, width
   `trunc(A / M × 74)` (floating point).
3. From the inner right edge leftwards draw the **dark red** bar, width
   `trunc((M − B) / M × 74)`.

Result: red = the current value, yellow = what the next level adds, dark
red = the rest up to the maximum. A = −1 is replaced by M, B = −1 by A.
M = 0 (e.g. the level gauge of a type with a single level, or a stat that
is 0 at every level) divides by zero in the original; OpenDay draws a full
red bar (or nothing when A = 0 as well).

The three bitmaps are resources of `LIBDAY.EXE`: background resource 252
(78×14), red 195 (148×12, only the needed width is copied), dark red 251
(148×12).

**Ability icons**: 25×25 bitmaps (resources of `LIBDAY.EXE`), drawn from
right to left: the first at dialog **(420, 18)**, each next one 30 px
further left. Each is drawn only if its condition holds for the *cur* row,
in this order:

| Order | Resource | Condition | Look | Meaning *(?: from the pictures)* |
|---|---|---|---|---|
| 1 | 185 | `IsAA` | crossed-out aircraft | anti-aircraft |
| 2 | 184 | `MineDetectionRange` > 0 | radar / detector | detects mines |
| 3 | 182 | `Movement` = 2 (foot units) | figure beside a tree | can hide in forest |
| 4 | 181 | `Jammer` ≤ 0 | running figure | stealth (shipped units with `Jammer` 0: Commando, Submarine, Chopper, Flyer, Mind Warrior, Pack Freak) |
| 5 | 183 | `weaponType` = 1 | ballistic arc | indirect fire |
| 6 | 309 | 0 < `Jammer` < 100 | yellow radiation sign | jammer (grav units have 10) |

### 5.2 Automated unit page

| y | Label | Value (x = 330) |
|---|---|---|
| 80 | PREREQUISITE (30), context only | prerequisite text (6) |
| 98 | DEPLOY COST (37) | `Cost` |
| 116 | ARMOR (24) | `Automated Units Infos` property 0 (armor) of the type, as a number (missing → 0) |

Here the PREREQUISITE line is at 80 and the next line at 98 regardless of
the context (same visual result as a reserved slot). Name `Name<lang>`,
description `Encyclopedia<lang>`; no gauges, no ability icons.

### 5.3 Building page

| y | Label | Value (x = 330) |
|---|---|---|
| 80 | PRODUCTION (40) | `Production<lang>` text |
| 98 | PREREQUISITE (30), context only | prerequisite text with the deployment-centre rule (6) |
| 116 | COST (38) | `Cost` |
| 134 | RECYCLE VALUE (39) | `Recycle` |
| 152 | ARMOR (24) | `Armor` |
| 170 | AREA (41) | `"%d x %d"` with `SizeX`, `SizeY` *(?: argument order)* |
| 188 | POSITION (42) | string 43 + `Position`: SHALLOW WATER (43), DEEP WATER (44), RIVER SIDE (45), PLAIN (46), MOUNTAINS (47), FOREST (48), CRACK (49) |

(Shipped data: `Position` 3 = PLAIN for most buildings, 1 = DEEP WATER for
docks and sea bases.) No gauges, no ability icons.

### 5.4 Wall page

| y | Label | Value (x = 330) |
|---|---|---|
| 80 | TYPE (22) | string 176 + `WallType`: Concrete (176), Laser (177), Force (178) |
| 98 | COST (38) | `Cost` |
| 116 | ARMOR (24) | `Armor` |
| 141 | PREREQUISITE (30), context only | prerequisite text (6) |

### 5.5 Mine page

| y | Label | Value (x = 330) |
|---|---|---|
| 80 | TYPE (22) | string 174 + the mine's `typeID`: Standard (174), Grav (175) |
| 98 | COST (38) | `Cost` |
| 116 | AVG DAMAGE (27) | `"%d - %d"`: `damageBase` and `damageBase + damageRandom` |
| 141 | PREREQUISITE (30), context only | prerequisite text (6) |

## 6. Context and the prerequisite text

The **context** is the current colony's list of building types (the
structures available on this continent, campaign.md). It is given by the
city screen, the continent screen and *(?)* the strategic army screen; every
battle screen and both map screens pass none.

Prerequisite text of a type whose `Prerequisite` is P:

- no context → the line is not drawn at all;
- P = 65535 (none) → string 881 "None";
- P is in the colony's building list → that building type's `Name<lang>`;
- otherwise → string 880 "Not available on this continent.".

Building page only: if the code name of P, ignoring its last character, is
`H_Center_Deployment` (one of the deployment-centre variants Mk1–Mk4), the
lookup instead takes the **first** entry of the colony list whose code
name, ignoring its last character, is `H_Center_Deployment`, and shows that
building's name. So any deployment centre present in the colony satisfies
and labels the prerequisite; if none is present → string 880.

## 7. Shipped data facts (for testing)

- Unit types with frame −1: the extra convoy types 61–66. Building types
  with frame −1: the towers 100–103 (also excluded as "transforming"), the
  ruins 1004–1016, the containers 1000–1003, the depots 484–486 and Deploy
  Mk2–Mk4. Wall "Ruined Walls": −1. Every automated unit and mine has a
  frame.
- Several alien automated units share frame 100 (the Tauran transport
  picture). There is no "no picture" handling; frame −1 entries are simply
  skipped.
- Frames in use: units 0–117, buildings 2–122, automated units 16–125,
  walls 119–121, mines 77–78.

## Notes for OpenDay

- **Unit list:** build one entry per `typeID`, ordered by the type's first
  row in `Unit Types`, and apply the filters (race of the displayed row,
  frame ≠ −1) to that list (4.3). This is the intended behaviour; the
  original's defect only hides entries and is not worth reproducing. The
  same one-entry-per-type list is used for first / last / next / previous
  and for the wrap-around.
- **Bitmaps of the executable** (gauge parts 195 / 251 / 252, ability icons
  181–185 and 309) must not be extracted into the repository. OpenDay either
  loads them from the user's `LIBDAY.EXE` at run time (PE resource reader,
  as for the language DLL) or draws substitutes: a 78×14 bar with a border
  in yellow / red / dark red, and simple 25×25 symbols.
- **Gauge with M = 0**: full red bar if A > 0, otherwise only the yellow
  background (no division).
- **Pictures**: decode `ENCYCL.SMK` frame by frame on demand (or once and
  keep the needed frames); frames are addressed by index, so the whole file
  has to be decoded sequentially up to the frame (Smacker frames are
  delta-coded). The palette question is open (Open questions); the frames'
  own palette is close enough to `PAL.BMP` for a first implementation
  *(?)*.
- The dialog stays at screen (40, 40) at 1024×768 as in the original (the
  rest of the screen keeps showing the view below); centring is a possible
  later improvement.

## Implementation notes (OpenDay)

Implemented in `game/info/encyclopedia.*` (lists, filters, navigation, unit
page rows, gauges, prerequisite text; tested in `tests/info_test.cpp`) and
`app/encyclopedia_screen.cpp` (the dialog).

- One entry per unit `typeID` as recommended above; on the shipped data ▶
  gives exactly the human list of 4.3.
- The dialog is an overlay screen at screen (40, 40): the screen below stays
  visible but is not updated while it is open (no animation, palette
  cycling or AI turn), which is OpenDay's reading of the "suspend" counter.
- The pictures are decoded once from `ENCYCL.SMK` and remapped from the
  file's own palette to the nearest game-palette entries.
- The gauge parts (252 / 195 / 251) and the ability icons are read at run
  time from the user's `LIBDAY.EXE` (RT_BITMAP resources, converted to the
  nearest game-palette colours). The dark red bar copies the leftmost
  columns of bitmap 251 *(OpenDay choice)*. When the program file or a
  bitmap is missing OpenDay draws a yellow / red / dark red bar and framed
  boxes with a two-letter label instead.
- Opening places: the emblem of the tactical view (lead selected unit), the
  deployment and reinforcement view (selected list type), the waiting view
  (default page), both map screens (selected row), the continent screen
  (default page, colony context), the city screen (3.1, colony context) and
  the army screens. The army screen takes the unit list's selection unless
  the automated list was clicked last; the strategic army screen passes the
  colony list as the context, the skirmish and deployment variants none.
  The viewing player's tech levels are those of the battle (tactical
  screens) or of the campaign player.
- An initial type that is not in its table opens the default page.

## Open questions

- The meaning of the application counter held while the encyclopedia is
  active (animation lock or palette-cycling pause).
- Whether the `ENCYCL.SMK` pictures are remapped to the game palette or
  shown with their own palette.
- The context given by the army screens (the colony list in the strategic
  variant, none in the skirmish / deployment variants *(?)*).
- The order of the four groups of the city screen's Defenses list against
  the selection rule of 3.1 (roads → default page assumed).
- The argument order of the building AREA line (`SizeX × SizeY` assumed).
- The meaning of ability icons 181, 182 and 309 (inferred from the pictures
  and the data only).
