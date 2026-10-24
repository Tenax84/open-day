# Tactical interface: battle HUD, input, deployment, briefing, aftermath

How the original program presents a tactical battle: the screen layout, the
side panel and its controls, the minimap, the information shown for units,
the mouse and keyboard rules, the deployment screen, the objectives
(briefing) dialog, the waiting screens and the battle report.

Prerequisites and shared facts:

- Art: frame numbers refer to the interface atlases `INTERF\640X480.SMK` /
  `1024X768.SMK`, see [`../formats/INTERFACE-SMK.md`](../formats/INTERFACE-SMK.md)
  (element anchored at (0,0), opaque copy, size supplied by the caller).
- Generic widgets (push buttons with 4 images, toggles with 6 images, hot
  keys, fonts, colours, hardware mouse pointer): [`interface.md`](interface.md).
  "Standard font" below means Arial 14 from that document.
- Rules (not repeated here): deployment and battle results in
  [`battle-flow.md`](battle-flow.md), movement and the path preview in
  [`tactical-movement.md`](tactical-movement.md), shot legality in
  [`tactical-combat.md`](tactical-combat.md), map layers and symbols in
  [`map-objects.md`](map-objects.md), tables and columns in
  [`data-model.md`](data-model.md).

Conventions: coordinates are pixels, `(x, y)` from the top-left corner of the
element's owner (the side panel, the message strip or a dialog), sizes are
w×h. "Frames a–d" means the four (buttons) or six (toggles) consecutive atlas
frames starting at a. Hot keys written as a letter come from the language
DLL string table (string id in parentheses; the English letter is given);
`F1`…`F6`, `Enter`, `Esc`, `Space`-like keys are fixed virtual keys.

Status: derived from studying the program; items marked *(?)* are not fully
confirmed. Statements about what is drawn were cross-checked against the
atlas art where possible.

## 1. Screen layout

### 1.1 Regions

The battle screen is always the full game resolution (unlike the front-end
menus, which stay 640×480). It is split into three regions:

| Region | 640×480 | 1024×768 | Art |
|---|---|---|---|
| map viewport (isometric map) | (0, 0), 504×448 | (0, 0), 840×728 | — |
| side panel (right) | (504, 0), 136×480 | (840, 0), 184×768 | one 136/184-wide panel frame per screen (1.2) |
| message strip (bottom) | (0, 448), 504×32 | (0, 728), 840×40 | frame 498 |

The viewport size is computed from the screen size `W × H`:

    view_w = floor((W − 131) / 56) × 56
    view_h = floor((H − 24) / 28) × 28

(504×448 and 840×728). The side panel starts at `x = view_w`, the message
strip at `y = view_h`. The isometric renderer keeps an off-screen buffer one
tile (56 px) wider and three half-tiles (84 px) taller than the viewport for
scrolling.

**1024×768:** the panel art of the large atlas is the same 136×480 panel in
its top-left corner plus decorative filler (48 px to the right, 288 px
below). All controls keep **the same offsets** relative to the panel origin;
nothing is moved or scaled. The message strip is 840×40 and its controls are
placed relative to its right edge (2.8), so they move with it.

### 1.2 Screens used during a battle

Each battle screen is a "view" that owns a side panel, an emblem button, a
resource indicator, the minimap box and the message strip. They differ in
the panel art and the controls:

| View | When | Panel | Emblem | MAP | GOAL |
|---|---|---|---|---|---|
| `CTacticView` | your turn | 226 | 294 | 231 | 235 |
| `CTacticWaitView` | another player's (or the AI's) turn | 310 | 323 | 315 | 319 |
| `CFirstDeployView` / `CDeployView` | deployment phase | 0 | 25 | 5 | 9 |
| reinforcement view (a `CDeployView`) | buying reinforcements in your turn | 0 | 25 | 5 | 9 |
| `CAutomatView` | calling automated units in your turn | 507 | 516 | 520 | 524 |
| `CMapScreenTactic` | MAP button | 41 | 88 | — | — |

(Correction to the attribution in `INTERFACE-SMK.md`: panel frame 0 is the
deployment view; 41, 310 and 507 are confirmed as listed there.)

Common to all views (positions relative to the panel):

| Element | Position, size | Notes |
|---|---|---|
| emblem button | (5, 0), 44×43 | `F1`: opens the encyclopedia (`CEncyclopInterface`). If a unit (tactical) or a list entry (deployment) is selected, it opens at that unit type's entry. |
| resource indicator | (64, 26)–(104, 40) | number, see 2.4 |
| minimap box | (12, 49)–(124, 161), 112×112 | minimap (2.6) or info card (2.7) |
| MAP button | (10, 169), 53×17 | `F4`: tactical map screen (7). Deployment view: (9, 172). |
| GOAL button | (71, 169), 53×17 | `F5`: objectives dialog (8). Deployment view: (68, 172). |

## 2. Tactical view (your turn)

### 2.1 Side panel controls (`CTacticView`, panel frame 226)

| Control | Position, size | Frames | Key | Kind |
|---|---|---|---|---|
| emblem (encyclopedia) | (5, 0), 44×43 | 294–297 | `F1` | button |
| resource indicator | (64, 26)–(104, 40) | — | — | number |
| minimap / info card | (12, 49)–(124, 161) | — | — | 2.6, 2.7 |
| MAP | (10, 169), 53×17 | 231–234 | `F4` | button |
| GOAL | (71, 169), 53×17 | 235–238 | `F5` | button |
| unit information | (8, 200)–(120, 238), 112×38 | — | — | 2.3 |
| GROUP | (10, 252), 53×17 | 241–246 | `G` (104) | toggle |
| CENTER | (71, 252), 53×17 | 298–301 | `F6` | button |
| MOVE ALL | (12, 273), 110×17 | 247–250 | `M` (105) | button |
| overwatch lamp | (15, 295), 19×19 | 251–256 | `O` (106) | toggle |
| FIRE | (12, 325), 110×17 | 258–263 | `F` (107) | toggle |
| auto-fire lamp | (15, 344), 19×19 | 264–269 | `X` (100) | toggle |
| previous unit (◀) | (12, 374), 19×17 | 270–273 | `P` (101) | button |
| GUARD | (41, 374), 53×17 | 278–281 | none | button |
| next unit (▶) | (103, 374), 19×17 | 274–277 | `Space` (102) | button |
| AUTOMATED UNITS | (12, 402), 110×17 | 286–289 | `A` (109) | button |
| REINFORCEMENT | (12, 428), 110×17 | 282–285 | `R` (108) | button |
| END TURN | (11, 456), 53×17 | 290–293 | `Enter` | button |
| turn counter | (95, 456)–(129, 475) | — | — | 2.5 |

The words "OVERWATCH" and "AUTO FIRE" next to the two lamps are part of the
panel art; the lamps are 19×19 toggles (red = off, green = on, frame order
as for any toggle: `+0` off, `+1` off-hover, `+2` pressed, `+3` disabled,
`+4` on, `+5` on-hover). Frames 227–230 (66×9 red bars) belong to this
block but no user was found *(?)*.

### 2.2 What the controls do

- **MAP** (`F4`): opens the tactical map screen (section 7).
- **GOAL** (`F5`): opens the objectives dialog (section 8).
- **GROUP** toggle: group selection mode. While it is on, clicking another
  own unit adds it to the selection; while off, a click replaces the
  selection. Switching it off while more than one unit is selected keeps
  only the lead unit selected. The selection holds at most 64 units; trying
  to add more shows string 580 (selection full) and plays
  `INVALID`.
- **CENTER** (`F6`): scrolls the map to the selected (lead) unit. If that
  unit has a remembered destination (3.3) that it can reach and that is not
  currently visible in the viewport, the map is centred on the destination
  instead *(?: visibility test inferred)*. Nothing happens without a
  selection.
- **MOVE ALL** (`M`): issues the pending move orders of all own units at
  once: every own unit that has more than 3 AP left and still has a
  remembered destination (from an earlier two-click move, 3.3, that it could
  not finish) is sent toward it, at most 64 units. If no unit qualifies, the
  message strip shows string 579 (nothing to move) for 5 s *(?: the exact
  qualification test was only partly traced)*.
- **Overwatch lamp** (`O`): switches the user option *Overwatch* (the
  `[User] Overwatch` INI key, see `interface.md`) and saves it; if a unit
  with a planned path is selected, its path preview is recomputed (the
  option changes how much AP a move keeps back for reaction fire,
  tactical-movement.md "Keeping AP for a shot" *(?)*).
- **FIRE** toggle (`F`): fire mode. While on, every left click on the map is
  a shot request of the selected unit at the clicked tile (3.2) and the tile
  cursor shows the shot evaluation (3.4). Switching it on hides the path
  preview; switching it off shows the lead unit's planned path again. `Esc`
  switches it off. Selecting a unit also switches it off.
- **Auto-fire lamp** (`X`): switches and saves the user option *Autofire*
  (`[User] Autofire`).
- **Previous / next unit** (`P` / `Space`): select the previous / next own
  unit in the cycle (3.8) and centre the map on it. If the unit that was
  selected has 0 AP left it is first marked *done* (skipped by later
  cycling).
- **GUARD** (no key): marks the selected unit *done* regardless of its AP
  and selects the next unit (3.8).
- **AUTOMATED UNITS** (`A`): if the player has no automated units available,
  a message box shows string 71 ("You don't have any automated units
  available"). Otherwise it leaves fire mode and switches to the automated
  units view (6.2).
- **REINFORCEMENT** (`R`): switches to the reinforcement view (6.1). The
  button is created disabled when the battle mode does not allow bought
  reinforcements (a flag the client receives when the battle starts).
- **END TURN** (`Enter`): only while it is your turn: closes the tactical
  view, shows waiting screen 14 ("next player's turn", section 4) and sends
  the end-turn request (`CRequestEndTactTurnMsg`). There is **no
  confirmation**, even if units still have AP.
- **Emblem** (`F1`): encyclopedia, see 1.2.

### 2.3 Unit information box

Rectangle (8, 200)–(120, 238) of the panel. It is cleared to black and
redrawn whenever the selection changes or a selected unit changes:

- **No unit selected:** string 84 (no selection).
- **Several units selected (group):** string 83 (number of selected units).
- **One unit (or the lead unit when the box is asked for a single unit):**
  - its list icon at (0, 0): `LISTBOX.DBI` sprite `<facingsImageID>999`
    (38×38, opaque);
  - text at x = icon width + 5, standard font, white, transparent
    background, line height = font height − 2:
    1. the unit type's name in the current language;
    2. string 673 "AP %1 / %2": AP left (`max(0, current AP − used AP)`, the
       value tactical-movement.md calls available AP) / `actionPoints` of the
       type;
    3. string 672 "AR %1 / %2": current armor / `Armor` of the type.

(The tech level is not shown here; the right-click info card, 2.7, has more
detail.)

### 2.4 Resource indicator

Box (64, 26)–(104, 40) next to the emblem (the small coin/box icon to its
right is part of the panel art). It shows one integer, clamped to
0…99 999, centred horizontally, 2 px below the top edge, standard font,
white on opaque black:

- tactical view and its sub-views: the player's **tactical resource** (the
  reinforcement credits of battle-flow.md 4.2);
- deployment view: the **remaining deployment budget** (budget − cost of the
  units placed so far, 5.3).

### 2.5 Turn counter

Box (95, 456)–(129, 475) in the bottom-right corner of the panel:

- a white number, right-aligned, vertically centred: the **turns left** as
  sent in the "your turn" message (battle-flow.md 1.3; the server asks the
  objective for it);
- if a second value of the same message is greater than 0, it is drawn
  left-aligned in the same box in red (RGB 200, 0, 0), and the `OBJECTIVE`
  sound plays the first time it becomes non-zero in this view. The server
  also takes it from the objective *(?: presumably the hold counter of Hold
  Position or a similar progress value)*.
- When the turns left drop below 3, the `TWOTURNS` sound plays once per
  tactical view.

### 2.6 Minimap

Drawn in the minimap box (`CIsoPalMap`), a square image of side
`min(box w, box h)` rounded down to a multiple of 4 = **112 px**, centred
horizontally in the box (top-aligned). It is a **top-down** map: `PosX`
runs to the right, `PosY` downward (the same orientation as the scenario's
`mapScreen` overview image, which the minimap does **not** use).

**Terrain layer** (built once and cached):

1. Scale `s = 112 / n` for an `n × n` map (the whole map is shown; a centre
   of `(n/2, n/2)` and half-size `n/2` are used). Block size
   `b = ceil(s)`.
2. For every tile `(x, y)`: fill the square of `(b + 1) × (b + 1)` pixels
   whose top-left corner is `(trunc(x·s), trunc(y·s))` with the tile's
   minimap colour, a palette index derived from the terrain type's
   `Color_Red/Green/Blue` *(?: nearest palette entry; the conversion was not
   traced)*. Later tiles overwrite earlier ones where blocks overlap.
3. Smooth the image once: every pixel becomes the palette entry nearest to
   the average RGB of itself and its (up to 8) in-image neighbours (all
   weights 1).

**Object layers** (drawn on top, in this order): buildings, roads, walls,
units. Each object is a filled square of side `b + 1 + k` at its tile's
scaled position, with `k = +1` for buildings, `0` for roads and walls,
`−1` for units. Colours come from the `Colors` table (`GLOBAL.MDB`) via the
nearest palette entry:

| Object | Colour row |
|---|---|
| unit of a human player | `Type 1`, `Enum` = the player's colour number 1–4 (`units_colorplayer_1…4`: blue, yellow, purple, green) |
| unit of the AI (owner 0) | `Type 1` by the unit type's race: human → 0 `units_human`, Tauran → 5, Shubblar (basic) → 7, Shadamar (mental) → 6 |
| building | `Type 0`: player colours 1–4 as for units; AI-owned buildings by race (0/5/7/6); neutral tech buildings 8 `buildings_tech`, ruins 9 `buildings_ruin` *(?: the two neutral cases)* |
| road | `Type 2` |
| wall | `Type 3` |
| mine | `Type 4` exists but no mine layer was found on the tactical minimap *(?)* |

There is no fog of war (tactical-movement.md 6), so all units are shown.

**Viewport frame:** a square outline marking the part of the map visible in
the viewport: from the viewport's top-left tile, `max(view_w / 54,
view_h / 28)` tiles wide and high (16 at 640×480), scaled like the map and
clamped into the minimap image. It is drawn as a 1-px **black** rectangle
with a 1-px **white** rectangle just inside it.

**Click:** a left button press inside the minimap image converts the point
back to a tile (`tile = floor(pixel / s)`) and centres the viewport on it.

### 2.7 Info card (right mouse button)

Pressing the **right** mouse button over a map tile replaces the minimap by
an info card about the object on that tile, until the next mouse event of
any kind (which restores the minimap). Priority: unit, then building, then
wall, then road. Layout inside the 112×112 box (standard font, `h` = font
height):

- the object's `LISTBOX.DBI` icon at (3, 3);
- at x = 44: line 0 (y = 3) the type name, line 1 (y = 3 + h) an "AR"
  value;
- unit card: line 2 (x = 44) string 641 "AP %1" (AP left); at x = 3:
  line 3 string 642 "RANGE %1" (`weaponRange`), line 4 string 643
  "OVERWATCH %1" with 644 "YES" if AP left ≥ `actionPointsShoot`, else 645
  "NO"; line 5 the owner's name;
- building card: line 1 string 640 "AR %1" (current armor); at x = 3: line 3
  the owner's name, line 4 string 646 "TECH %1" if the building's
  `TechReward` > 0;
- wall and road cards: line 1 the AR value, line 3 the owner's name;
- owner name: the player's name, or string 65 (the computer player's name) for owner 0.
- **Ability icons** (unit card only): a row of 25×25 bitmaps along the bottom
  (top edge at box bottom − 28, starting at x = 3, one after another). They
  are bitmap resources of the executable, shown when:

| Bitmap id | Shown when (unit type) | Picture |
|---|---|---|
| 185 | `IsAA` | crossed-out aircraft |
| 184 | `MineDetectionRange` > 0 | detector |
| 182 | `Movement` = 2 (trooper) | tree (can enter forest) |
| 181 | `Jammer` ≤ 0 | figure |
| 183 | `weaponType` = 1 (indirect) | arc |
| 309 | 0 < `Jammer` < 100 | crossed tools |

(Pictures described loosely; OpenDay needs its own icons.)

### 2.8 Message strip

`CFH2MsgArea`, background frame 498, positions relative to the strip:

| Element | Rectangle (640×480) | General rule | Notes |
|---|---|---|---|
| clock | (8, 6)–(48, 25) | fixed | `MM:SS`, white, standard font (2.9) |
| message text | (64, 5)–(369, 25) | (64, 5)–(right − 135, 25) | one line, left-aligned, vertically centred, black background |
| CHAT button | (384, 8), 53×17 | (right − 120, 8) | frames 499–502, `F3`; opens the chat window; disabled with fewer than 2 players |
| MENU button | (444, 8), 53×17 | (right − 60, 8) | frames 693–696, `F2`; opens the in-game options menu (`CMenuInterface`, see `interface.md`); the tactical view enables all its options, the deployment, reinforcement and automated views disable two of them *(?: probably Save and Restart scenario)* |

A message is shown with a colour (almost always white) and a lifetime
(5 s for errors and hints, 2 s for turn notices, 10 s for automated-unit
instructions) and then cleared. Messages seen in the tactical screens:
576–580 (movement), 320–325 (shots), 448–452 (deployment), 71, 896 ("%1
reinforcements have arrived"), 897 (whose turn begins).

### 2.9 Clock and turn time limit

Every view has an optional time limit, the session's *turn delay* in
minutes (multiplayer setting, string 742) × 60 000 ms. Once per second the
view updates the clock in the message strip:

- **single player** (fewer than 2 players in the session): the clock shows
  the time elapsed since the view was opened (counting up);
- **multiplayer:** the clock shows the time left (counting down). During the
  last 10 s the `TIMER` sound plays every second; at 0 the tactical view
  ends the turn automatically (as END TURN, unless an animation is running)
  and the deployment view accepts the deployment automatically (5.6).

Minutes are capped at 99.

### 2.10 Map overlays used by the HUD

Drawn on the map with the layer mechanism of map-objects.md (layer numbers
from there):

| Layer | Sprite (`SYMBOLS.DBI`) | Shows |
|---|---|---|
| 2 | `OBJT000` | objective zone tiles (`Objectives Pos List`) |
| 14 | `OBJT001` or `OBJT002` | tiles of the objects in the objective's ID list (units at their tile, buildings at their position). `OBJT001` for objective kinds 0 (Destroy) and 4 (Intercept), `OBJT002` for all others (escort / protect / keep targets). |
| 3 | `DEPL<c><nn>` | your deployment zone (5.2) |
| 7 | `PATH0000`–`PATH0005` | path preview (tactical-movement.md 3.4) |
| 8 | tile cursor | 3.4 |
| 16 | `LILBULB` | tech buildings (placed at every tech building's position when the view opens) |
| 17 | `SU000`–`SU004` | selection triangles over every selected unit, in the owner's colour (0 red, 1 blue, 2 yellow, 3 purple, 4 green) |
| 18 | `SU000` | a red triangle at the lead unit's *marked tile* (3.4, `CSELT`) |

Layers 2 and 14 are shown only while the **objective markers** option is on
(the toggle in the objectives dialog, section 8; it starts on). Layers 16–18
use the special draw mode (map-objects.md "Draw mode", bit 16).

## 3. Mouse and keyboard in battle

### 3.1 When input is accepted

Mouse and keyboard input on the tactical view is ignored while an
animation sequence is playing (moves, shots, explosions) and while the
client is not in the "your turn" state. Scrolling (3.6) is allowed whenever
the view accepts input. Every mouse event first removes the info card (2.7).

The mouse pointer itself is the normal arrow (`interface.md` 6). The
`SYMBOLS.DBI` "cursors" (`CSELC`, `CTARG`, …) are **not** mouse pointers but
54×56 tile sprites drawn on the map at the hovered tile (3.4).

### 3.2 Left click on the map

The map view maps mouse messages to actions: left button **down** = click,
left button up = nothing, right button down = info card (2.7), right
button up = nothing. A click is converted to a tile (screen → isometric
tile inverse of isometric-map.md) and then:

1. **Fire mode on:** shot request of the selected unit at the tile
   (requires a selection).
2. Otherwise by what is on the tile (first match):
   - **a unit:**
     - it is the selected unit and it is selected alone → **turn it**: its
       facing becomes `facing + 1` (7 wraps to 0), sent to the server as a
       unit update;
     - it is an own unit that may be selected (owned by you, not a convoy
       unit) → **select it** (or add it, GROUP on), play `SELECT`, centre
       the map on it, leave fire mode, show the path preview to its
       remembered destination if it has one, refresh the info box;
     - otherwise, if a unit is selected and the clicked unit is an
       **enemy** (tactical-combat.md 1.1 relation) → shot request at it;
   - **a building** that is an enemy building of yours and a unit is
     selected → shot request;
   - **a mine visible to you** and a unit is selected → shot request;
   - **a wall** and a unit is selected → shot request;
   - **empty tile** (or anything else) and a unit is selected → movement,
     3.3.

A "shot request" from the HUD becomes either a direct shot or a "move to
fire" order (tactical-movement.md 3.7) according to the shot evaluation
shown by the cursor (3.4) *(?: the decision code itself was not traced)*.

### 3.3 Two-click movement

1. **First click** on a tile: for every selected unit a path to the tile is
   computed (group rules of tactical-movement.md 3.5) and the preview is
   shown (`PATH…` sprites, layer 7). The tile becomes the units' remembered
   destination. If no selected unit can reach it, string 577 ("Unit can't
   reach that position") is shown and the destination is cleared.
2. **Second click on the same tile** (while the lead unit's planned path
   still ends there): the move orders are sent (`CRequestUnitMoveMsg`),
   the preview is removed and fire mode is switched off.

Clicking another tile instead re-plans to the new tile. Remembered
destinations survive the end of the turn; MOVE ALL (2.2) resumes them.

### 3.4 Tile cursor

On every mouse move over the viewport (and not in the edge-scroll zone) the
view chooses a tile sprite for the hovered tile. If the lead selected unit
is larger than 1×1, the sprite is drawn on every tile of its footprint
placed at the hovered tile. Choice (first match):

| # | Situation | Sprite |
|---|---|---|
| 1 | a unit is selected and the tile is its marked tile *(?)* | `CSELT` |
| 2 | fire mode on, a unit selected | shot evaluation (below) |
| 3 | an own selectable unit on the tile | `CSELC` (red tile outline) |
| 4 | an enemy unit on the tile, a unit selected | shot evaluation |
| 5 | an enemy building on the tile, a unit selected | shot evaluation |
| 6 | a mine visible to you, a unit selected | shot evaluation |
| 7 | a wall, a unit selected | shot evaluation |
| 8 | a unit selected (empty tile) | `CMOVE` if the lead unit can get a path to the tile, otherwise none |
| 9 | nothing selected | none |

**Shot evaluation** (legality check of tactical-combat.md 1 for the lead
unit and the tile):

- legal → `CTARG`;
- "invalid target" → `CINVA`;
- any other failure (no weapon, not enough AP, out of range, too close, no
  line of fire) → `CMVSH` ("move and shoot"); in fire mode, if the lead
  unit is immobile (`Movement` 0 or 8) and selected alone, `CINVA` instead.

"None" means no tile sprite (just the arrow pointer). The sprite is drawn in
the special draw mode while one is active.

### 3.5 Right button

See 2.7. The right button does not deselect and does not cancel orders.

### 3.6 Scrolling

- **Edge scrolling:** the cursor position is tested against the **whole
  screen** (not the viewport): within 5 px of the left / right / top /
  bottom screen edge the map scrolls in that direction; corners combine two
  directions. This also works over the side panel and the message strip.
- **Keys:** arrow keys scroll in their direction; `Home` = up-left,
  `Page Up` = up-right, `End` = down-left, `Page Down` = down-right (the
  numeric-keypad layout).
- **Step:** one step moves the view by **56 px horizontally** or **28 px
  vertically** (one tile column / row of the isometric grid; tile-space
  `x + 1, y − 1` for a horizontal step, `x + 1, y + 1` for a vertical one).
- **Speed:** steps are repeated at most once per interval set by the
  `ScrollSpeed` option: 0 → 100 ms, 1 → 50 ms (default), 2 → no delay
  (every frame). Values outside 0–2 read from the INI become 1.
- Scrolling is refused while the view does not accept input (3.1).

### 3.7 Keys

| Key | Action |
|---|---|
| `F1` | encyclopedia (emblem) |
| `F2` | in-game menu (message strip) |
| `F3` | chat (multiplayer only) |
| `F4` | MAP |
| `F5` | GOAL |
| `F6` | CENTER |
| `G` | GROUP toggle |
| `M` | MOVE ALL |
| `O` | overwatch option |
| `F` | FIRE toggle |
| `X` | auto-fire option |
| `P` | previous unit |
| `Space` | next unit |
| `A` | AUTOMATED UNITS |
| `R` | REINFORCEMENT |
| `Enter` | END TURN |
| `Esc` | leave fire mode |
| arrows, `Home`, `End`, `Page Up`, `Page Down` | scroll (3.6) |
| `Tab` + `G` | show / hide the isometric grid (`ISOGRID`) |

Letters are the localised first characters of strings 100–109; both
upper- and lower-case work for the `Tab` combinations.

**Cheat keys** (only with `Cheats=1` in `LIBDAY.INI`), all with `Tab` held:

| Keys | Request |
|---|---|
| `Tab`+`P` | upgrade the selected unit's AP (`CCheatUpgradeAPMsg`) |
| `Tab`+`R` | upgrade the selected unit's armor (`CCheatUpgradeARMsg`) |
| `Tab`+`D` | add resources (`CCheatUpgradeResMsg`); also in the deployment view |
| `Tab`+`W` / `Tab`+`L` | win / lose the battle (battle-flow.md 3.1) |
| `Tab`+`T` | restart the turn clock |
| `Tab`+`O` | set the turn clock to 60 s before the limit |

### 3.8 Unit cycling (previous / next / GUARD)

Candidates: all units owned by you that are not marked *done* and are not
convoy units (`isConvoy`), sorted by unit `ID` ascending; the currently
selected unit is excluded. *Next* takes the first candidate with an `ID`
greater than the current one, wrapping to the smallest; *previous* the last
candidate with a smaller `ID`, wrapping to the largest. AP is not checked
(units with 0 AP become *done* only when you cycle away from them). The
*done* marks are cleared at the start of each of your turns *(?)*. If there
is no candidate, nothing happens.

## 4. Turns, waiting and transitions

### 4.1 Sequence

| Event (client) | Screen |
|---|---|
| battle phase starts (`CClientPhaseTactic` created) | waiting screen 13 "deployment phase" (`WAITING.SMK`, `INTERFACE-SMK.md`) |
| deployment begins (`CGoDeployMsg`) | deployment view (section 5) |
| you accept the deployment | waiting screen 14 "next player's turn" |
| your turn (`CGoTacticMsg`) | tactical view; the waiting screen is closed |
| another participant's turn (`CGoTacticWaitMsg`) | waiting view (4.2) |
| you press END TURN | waiting screen 14, then the waiting view when the next turn starts |
| battle over (`CGoAftermath…Msg`) | battle report (section 9) |
| counter-attack battle | waiting screen 6 (alien counter-attack) and the `VIDEO\COUNTERA.SMK` video *(?: order)* |

There is no separate "your turn" banner: the tactical view simply replaces
the waiting picture or the waiting view. The `SELECT` sound is not played at
turn start.

### 4.2 Waiting view (`CTacticWaitView`, panel frame 310)

Shows the live map while other participants play (their moves and shots are
animated). Panel controls: emblem 323, MAP 315, GOAL 319, minimap and
resource indicator as usual, and a **player list** instead of the action
buttons:

- one row per participant, at (6, 205 + 28·i), i = 0…4;
- each row: a 19×19 lamp, frames `46 + 6·c` (c = colour number: 0 red, 1
  blue, 2 yellow, 3 purple, 4 green; the six frames of each colour form a
  toggle), and the name at (lamp + 25, lamp + 1), 96×16, standard font;
- rows: the human players in session order, then "Alien" (string 65) for the
  computer player except in death match;
- the row of the participant whose turn it is is checked (lit).

When a new turn begins the view lights the new row, restarts the clock and
shows string 897 (whose turn begins) for 2 s; scripted reinforcements
show string 896. The mouse pointer is the hour-glass (`interface.md`).
Selection, orders and the action buttons are not available.

## 5. Deployment

### 5.1 Screen (`CFirstDeployView`, panel frame 0)

| Control | Position, size | Frames | Key | Notes |
|---|---|---|---|---|
| emblem | (5, 0), 44×43 | 25–28 | `F1` | encyclopedia at the selected list type |
| resource indicator | (64, 26)–(104, 40) | — | — | remaining budget |
| minimap | (12, 49)–(124, 161) | — | — | as 2.6 |
| MAP | (9, 172), 53×17 | 5–8 | `F4` | |
| GOAL | (68, 172), 53×17 | 9–12 | `F5` | |
| unit list | (9, 206), 5 rows of 120×39 | — | — | 5.2 |
| list scroll up / down | below the list, 53×17 each | 13–16 / 17–20 | — | *(?: exact x positions)* |
| ARMY | (9, 430), 110×17 | 29–32 | `A` (113) | army screen (5.5) |
| CONTINUE (done) | (9, 456), 110×17 | 21–24 | `Enter` | 5.6 |

The panel art has the minimap box at the top and the list box below it.

### 5.2 Unit list

- **Entries:** the unit types the player may deploy, each with the
  player's current tech level for that type (the list the client received
  for this battle: `One Off Units Available` in skirmish / death match, the
  player's army in the campaign *(?: the client-side source of the list was
  not traced)*). Cost per entry = the `Cost` of that type at that tech level
  (data-model.md).
- **Order:** by movement class, in this order: trooper (2), ground (1), boat
  (4), underwater (6), air (5), grav-fly (9), grav (3), teleport (7), none
  ground (0), none water (8); then by cost ascending.
- **Row drawing** (120×39): the type's list icon (38×38, `<facingsImageID>999`)
  at (2, 2); the name at (50, 2) and the cost (a plain number) at (50, 24),
  standard font, white on opaque black; the selected row gets a 1-px
  frame in palette index 255 just inside its rectangle.
- One entry is always selected (the first at start). The selected type is
  what a click on the map deploys.

### 5.3 Placing and removing units

Your **deployment zone** is drawn on the map with `DEPL<c><nn>` sprites on
layer 3 (map-objects.md "Deployment markers"): `c` is your colour number,
`nn` comes from the 4-neighbour mask of zone tiles. The mask tests the
neighbours `(x − 1, y)`, `(x + 1, y)`, `(x, y − 1)`, `(x, y + 1)` for
membership in the **same player's zone** (confirming the presumption in
map-objects.md). Only your own zone is shown; it is removed when the view
is left.

Left click on a map tile:

1. Tile **outside your zone** → string 452 ("You can only deploy units in
   the indicated zone") for 5 s and the `INVALID` sound.
2. Tile in the zone carrying a unit **you placed in this deployment** →
   **recall** it: the unit is removed, its cost is refunded, message 450
   (unit taken back).
3. Otherwise **deploy** the selected list type:
   - cost above the remaining budget → string 448 ("Need more tactical
     resource");
   - tile not valid for the unit (deployment tile check,
     tactical-movement.md 1.5) → string 451 (tile not valid for the unit);
   - else the unit appears on the tile **facing the map centre**
     `(n/2, n/2)`, its cost is subtracted from the displayed budget,
     message 449 (unit placed).

Units already on the map from the scenario cannot be recalled. Deployment
is only a local plan until it is accepted (5.6); the server repeats the
checks (battle-flow.md 1.2).

Tile cursor in the deployment view: over a zone tile without a unit
`CDEPL`; over a unit you placed `CRECA`; elsewhere none.

`Tab`+`G` toggles the grid, `Tab`+`D` is the resource cheat (3.7).

### 5.4 Objective markers

When the deployment view opens (and in the tactical view) the objective
markers of 2.10 (layers 2 and 14) are drawn if the objective-markers option
is on, so the player sees the zone and the targets while deploying.

### 5.5 ARMY button

Opens the army screen in its deployment variant (`CArmyDeployInterf`,
dialog frame 157, [army-tech.md](army-tech.md) 4): a **browsing** screen
without UPGRADE and without the information text. When it closes, the
deployment list selects the unit type that was selected on the army screen
(the list is not rebuilt).

### 5.6 Accepting

CONTINUE (`Enter`): if the view has a time limit that has not run out
(2.9) and more than 4 credits are left, a yes/no message box asks string
453 ("You still have some resources left. Do you want to continue
anyway?"). Without a time limit the question is never asked *(as coded;
whether single-player battles have a time limit was not checked)*.
Otherwise, or on *yes*, the view sends all placed units in one
`CReqDeployUnitsMsg` (type, tile per unit; facing is decided by the server)
and shows waiting screen 14. In multiplayer the deployment is accepted
automatically when the clock runs out (2.9).

## 6. Reinforcements and automated units

### 6.1 Reinforcement view

Same panel and list as the deployment view (frame 0; it is a `CDeployView`),
with these differences:

- the budget is the tactical resource (2.4);
- placing works as in 5.3 (zone, cost and tile checks, facing the centre);
- the bottom button at (9, 456), 110×17, frames 21–24, has the key `Esc`
  and returns to the tactical view; the placed units are sent as a
  reinforcement request (`CReqReinfUnitsMsg`) *(?: whether they are sent on
  every placement or when leaving)*.

### 6.2 Automated units view (`CAutomatView`, panel frame 507)

| Control | Position, size | Frames | Key |
|---|---|---|---|
| emblem | (5, 0), 44×43 | 516–519 | `F1` |
| MAP / GOAL | as 1.2 | 520–523 / 524–527 | `F4` / `F5` |
| list of available automated units | (9, 205), rows 118×39 | scroll 528–531 (up), 532–535 (down) | — |
| AUTOMATED UNITS ▶ (back) | (12, 428), 110×17 | 512–515 | `Esc` |

- Rows show the automated type's list icon, name and cost (as 5.2).
- After choosing a type, the player clicks target tiles on the map. Each
  click is checked: cost above the tactical resource → string 448; target
  not valid for the type → string 320 (invalid target). Types that need
  several points (e.g. transports: source then destination, shown with the
  `HTRANSPS`/`HTRANSPD`, `TTRANSPS`/`TTRANSPD` symbols) show an instruction
  text from the type's explanation columns for 10 s after each point.
- When all points are given the request (`CReqAutomatedUnit`) is sent and
  the view returns to the tactical view.
- Tile cursor: the type's `cursorID` sprite (`UA…`, data-model.md) over valid
  tiles.

## 7. Tactical map screen (MAP)

`CMapScreenTactic`, panel frame 41, emblem 88. It replaces the viewport by
a large top-down map of the battle:

- the map image is a square of side `min(view_w, view_h)` (448 at 640×480)
  centred in the viewport area, drawn like the minimap (2.6) at a larger
  scale;
- markers use small bitmaps of the executable: `SHADE` (213, 54×56), `OBJ00`
  (152, 8×20, objective), `DEPL` flags 240–243 (16×25, one per colour 1–4:
  blue, yellow, purple, green) for deployment zones, `LBULB` 305–308 (15×22)
  for tech buildings *(?: exact use of each)*;
- a player selector at (6, 53)–(127, 68) with one entry per player plus
  "Alien" (string 65);
- a list box at (12, 96)–(128, 426) with scroll buttons at (8, 431) and
  (75, 431) *(?: content, probably the selected player's units)*;
- a back button at (10, 458), 53×17, key `Esc`.

This screen was traced only superficially.

## 8. Objectives dialog (briefing)

`CIntelInterface`, opened by GOAL (`F5`) from the tactical, waiting and
deployment views; it plays the `OBJECTIVE` sound when it opens. There is no
separate pre-battle briefing screen: the scenario description is also shown
in the front-end scenario lists (`interface.md`) and the objectives are
available through GOAL during deployment.

- Dialog art: frame 633, 448×268, at screen rectangle **(40, 40)–(488, 308)**
  (fixed; not re-centred for 1024×768 *(?)*).
- Text area: the dialog rectangle inset by 20 px left, top and right and
  30 px at the bottom; standard font, white, transparent background:
  1. the scenario title (`Scenario Info.Title<language>`), centred, at the
     top;
  2. 30 px lower, left-aligned: the province name
     (`provincename<language>`);
  3. 30 px lower: the description (`description<language>`) word-wrapped.
     The description is split at the `##` separators into three parts;
     they are shown as `part1` + blank line + `part2` + line break +
     `part3`. In the shipped data part 1 is the story, part 2 the
     objective heading with the turn limit (the objective heading with the turn limit) and
     part 3 the objective itself.
- Controls (relative to the dialog): CONTINUE at (308, 205), 110×17,
  frames 634–637, key `Esc`, closes the dialog; an **objective markers**
  toggle at (298, 230), 19×19, frames 638–643, next to the word
  "OBJECTIVES" of the art. The toggle switches the map markers of 2.10
  (layers 2 and 14) on and off for the rest of the battle; it starts on.

`mapScreen640` / `mapScreen1024` (the scenario's overview pictures) were
not found in any tactical screen; they are probably used by the strategic
phase or the front-end scenario selection *(?)*.

## 9. Battle report (aftermath)

### 9.1 Sequence

When the client receives the end-of-battle message (battle-flow.md 5.1) it
leaves the tactical screens and opens the battle report dialog of the
mode: `CAftermathCampgnInterface` (campaign), `CAftermathCAttInterface`
(counter-attack), `CAftermathSkirInterface` (skirmish),
`CAftermathDeathInterface` (death match). After CONTINUE:

- skirmish and death match: back to the front end (waiting screen 1, black);
- campaign and counter-attack: the client confirms with
  `CEndAftermathCampaignMsg` and the strategic phase continues; when the
  result ends the campaign a closing video is played first
  (`VIDEO\FH2END.SMK` for a won campaign, `VIDEO\FH2LOSE.SMK` for a lost one,
  `CAftermathAnimInterf`) *(?: the exact condition)*.

### 9.2 Layout

Dialog art frame 365 (448×480) at screen rectangle **(45, 0)–(493, 478)**.
Text area = the rectangle inset by 20 px (left, top, right) and 30 px
(bottom); standard font, white, transparent background.

1. Title: string 513 (the report title), centred, single line, at the top of
   the text area.
2. Two lines lower, centred and word-wrapped, the **result text**:
   - battle lost (success = false): string 520 ("You have failed this
     scenario. You will need to try again.");
   - campaign / counter-attack won: the scenario's `Aftermath<language>`
     text;
   - skirmish won: string 519 ("Congratulations ! You have defeated the
     enemy.");
   - death match (success is always true there): loser code 5 (no loser) →
     string 521 (battle ended, ranking follows); you
     are the loser → 520; otherwise 519.
3. **Statistics table**, header at y = 185 (dialog-relative), columns at
   x = 25, 115, 250 and 360 from the left edge of the text area:

   | x | Header (string) | Row line 1 | Row line 2 |
   |---|---|---|---|
   | 25 | PLAYER (180) | player name | — |
   | 115 | DESTROYED (512) | "%1!d! Units" (516): enemy units destroyed | "%1!d! Buildings" (517): enemy buildings destroyed |
   | 250 | LOST (515) | "%1!d! Units": own units lost | "%1!d! Buildings": own buildings lost |
   | 360 | RANK (514) | the player's number from the result message (score or rank, battle-flow.md 5.1 *(?)*) | — |

   The first row starts two text lines below the header; each player takes
   two lines plus half a line of spacing (row step = 2.5 × font height).
   One row per human player of the result message (slots in order).
4. Won campaign province battles only (not counter-attacks or the first
   city attack): string 518 ("New
   technologies found : %1!d!") at y = 418, x = 25 from the text area's
   left edge.
5. CONTINUE button at (300, 419)–(410, 436) relative to the dialog, 110×17,
   frames 366–369, key `Enter`.

## 10. Summary of the 1024×768 differences

- Viewport 840×728 instead of 504×448; side panel at x = 840, 184×768
  (panel art with extra filler); message strip at y = 728, 840×40.
- Side-panel controls, the minimap (112×112) and all list boxes keep their
  640×480 offsets and sizes.
- Message-strip controls are anchored to the strip's right edge (2.8); the
  message text area grows to 64 … 705.
- The viewport frame on the minimap covers more tiles (`max(840/54,
  728/28)` = 26).
- Edge scrolling uses the 1024×768 screen edges.
- The objectives dialog and the battle report use fixed screen rectangles
  (8, 9.2); whether they are re-positioned at 1024×768 is open.
- Sprite sizes (buttons, icons, `SYMBOLS.DBI`, `LISTBOX.DBI`) are identical.

## 11. Notes for OpenDay

- Treat each battle screen as a view with a common frame (panel art, emblem,
  resource indicator, minimap box, message strip) and screen-specific
  controls; the tables above give everything needed for the 640×480 layout.
- The tile "cursors" are map sprites; keep the system/arrow pointer.
- The minimap can be generated from data alone (terrain colours, `Colors`
  table); no original picture is needed.
- Cheat keys are optional; if implemented, gate them behind an explicit
  option as the original does.

## Implementation notes (OpenDay)

Implemented in `app/battle_screen.cpp` (deployment, tactical and waiting
views, objectives dialog, in-game options, battle report) on top of
`game::BattleSession`.

- Skirmish, single player: the human plays slot 1. Units and buildings of
  the other human slots of the scenario join slot 1, and the unit list of
  the deployment view only offers human-race types (the scenario's list
  names both races) *(OpenDay choices)*.
- Deployment is sent to the server one placement at a time (place and
  recall are immediate) instead of in one request at CONTINUE.
- When the deployment view opens, the pre-battle overview map comes first
  (strategic-interface.md 4), then the objectives dialog; MAP opens the
  overview map again (with BACK).
- REINFORCEMENT switches to the deployment view during the player's own
  turn: units bought there are placed at once without AP (battle-flow.md
  4.2); CONTINUE returns to the tactical view.
- ARMY opens the army screen's deployment variant.
- Held tech buildings show the `LILBULB` symbol and play `TECH` when taken.
- Info card, minimap (terrain colours of `Terrain Types`, objects from
  `Colors`, nearest palette entries), unit information box, tile cursors,
  path preview, two-click movement, fire mode, GROUP, CENTER, MOVE ALL,
  unit cycling, GUARD, END TURN, the overwatch and auto-fire lamps (stored
  as options; the overwatch AP reserve is not applied yet) and the cheat
  keys Tab + W / L (only with `--cheats`) work as described.
- Not implemented yet: encyclopedia (emblem), automated units, turning a
  unit by clicking it, chat, the turn time
  limit, the ability icons of the info card, statistics beyond units and
  buildings lost and destroyed (the RANK column shows 1).

## 12. Open questions

- Frames 227–230 (66×9 red bars) of the tactical block: no user found.
- The exact meaning of the lead unit's *marked tile* (`CSELT`, red `SU000`
  triangle on layer 18) and how it relates to the remembered destination.
- How a shot request becomes a direct shot or a move-to-fire order (the
  `CMVSH` case), and whether auto-fire changes it.
- MOVE ALL: the precise unit test (AP > 3 and a remembered destination was
  inferred).
- Whether *done* marks (GUARD, 0-AP units) are cleared every turn.
- The second (red) number of the turn counter box.
- Scroll-button positions of the deployment and automated-unit list boxes;
  the first deployment view also creates a 450×250 surface from panel frame
  0 whose use was not found.
- Reinforcement view: when the request is sent; whether the deployment
  zone is used (battle-flow.md 4.2 leaves this open too).
- Tactical map screen (section 7): list box content, use of the `SHADE`,
  `OBJ00`, `DEPL` and `LBULB` bitmaps.
- Positions of the objectives dialog and the battle report at 1024×768.
- Terrain → minimap colour conversion (assumed nearest palette entry of
  `Color_Red/Green/Blue`) and whether mines appear on the minimap.
- (Answered in campaign.md 7 and strategic-interface.md 7: the closing
  videos follow the battle report when the campaign ends; the counter-attack
  video comes before waiting screen 6.)
