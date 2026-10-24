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
  once. It ignores the selection and goes over **all units of the battle**
  in map list order, taking those owned by the local player whose available
  AP is **greater than 3**. Each is planned with the group planner of
  tactical-movement.md 3.5 toward its **remembered destination** (3.3),
  with the Overwatch budget, in **one** pass (no repeat passes). A unit
  gets no order if it has no remembered destination, the destination is
  not reachable, its cut path is empty, or 64 units are already in the
  order. If no unit got an order, the message strip shows string 579 ("No
  units to move"), white, for 5 s. Otherwise all path previews are removed,
  the view enters its waiting state and one `CRequestUnitMoveMsg` is sent.
  MOVE ALL does **not** clear the remembered destinations.
- **Overwatch lamp** (`O`): switches the user option *Overwatch* (the
  `[User] Overwatch` INI key, see `interface.md`) and saves it at once; if
  a lead unit with a planned path is selected, its path preview is removed
  and recomputed with the new budget. When on, every move planned in this
  view keeps one shot's worth of AP for units with `canOverwatch`
  (tactical-movement.md 2, "Keeping AP for a shot"). The lamp only affects
  planning on this client; reaction fire itself is automatic.
- **FIRE** toggle (`F`): fire mode. While on, every left click on the map is
  a shot request of the selected unit at the clicked tile (3.2) and the tile
  cursor shows the shot evaluation (3.4). Switching it on hides the path
  preview; switching it off shows the lead unit's planned path again. `Esc`
  switches it off. Selecting a unit also switches it off.
- **Auto-fire lamp** (`X`): switches and saves the user option *Autofire*
  (`[User] Autofire`). Its only effect is the autofire flag of the shot
  requests sent from this view (tactical-combat.md 2).
- **Enable states** (re-evaluated whenever the selection changes): with no
  lead unit, CENTER, FIRE, the overwatch lamp and GUARD are disabled. With
  a lead unit, CENTER and GUARD are enabled; FIRE and the overwatch lamp are
  disabled if fewer than 2 units are selected and the lead unit's
  available AP is below its shot cost, otherwise enabled. GROUP is always
  enabled. A FIRE or GROUP toggle that ends up disabled is switched off; the
  overwatch lamp keeps its state while disabled *(?: "enabled" inferred
  from usage)*.
- **Previous / next unit** (`P` / `Space`): select the previous / next own
  unit in the cycle (3.8) and centre the map on it. If the unit that was
  selected has 0 AP left it is first marked *done* (skipped by later
  cycling).
- **GUARD** (no key): marks the selected unit *done* regardless of its AP
  and selects the next unit (3.8). The *done* mark is never cleared during
  the battle (3.8).
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
  sound plays when the view's stored copy of it is still 0 (once per
  tactical view, i.e. once at the start of each of your turns while the
  value is non-zero). The value is the objective's **progress counter**:
  for **Hold Position** the number of rounds the zone has been held so far
  (battle-flow.md 2.3), sent only while T ≤ last held turn + 1 (or no last
  held turn is set), otherwise 0. All other objective kinds always send 0,
  so the red number appears only in Hold Position battles.
- The turns left are `turn_qty − T + 1` (the counter-attack objective uses
  its own count). When they drop below 3, the `TWOTURNS` sound plays once
  per tactical view.

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
  (top edge at box bottom − 28, the first at x = 3, each next one **30 px**
  further right: 25 px icon + 5 px gap). Only the icons that apply are
  drawn, in this order, without gaps. They are bitmap resources of the
  executable, shown when:

| Order | Bitmap id | Shown when (unit type) | Picture | Meaning (manual) |
|---|---|---|---|---|
| 1 | 185 | `IsAA` | crossed-out aircraft | has anti-aircraft defence |
| 2 | 184 | `MineDetectionRange` > 0 | detector | detects adjacent mines |
| 3 | 182 | `Movement` = 2 (trooper) | tree | can walk in forest / jungle |
| 4 | 181 | `Jammer` ≤ 0 | figure | does not trigger enemy overwatch fire |
| 5 | 183 | `weaponType` = 1 (indirect) | arc | can fire over obstacles |
| 6 | 309 | 0 < `Jammer` < 100 | crossed tools | jamming device: harder to hit by overwatch fire |

The encyclopedia's unit page shows the same icons with the same rules.

(Pictures described loosely; OpenDay needs its own icons.)

### 2.8 Message strip

`CFH2MsgArea`, background frame 498, positions relative to the strip:

| Element | Rectangle (640×480) | General rule | Notes |
|---|---|---|---|
| clock | (8, 6)–(48, 25) | fixed | `MM:SS`, white, standard font (2.9) |
| message text | (64, 5)–(369, 25) | (64, 5)–(right − 135, 25) | one line, left-aligned, vertically centred, black background |
| CHAT button | (384, 8), 53×17 | (right − 120, 8) | frames 499–502, `F3`; opens the chat window; disabled with fewer than 2 players |
| MENU button | (444, 8), 53×17 | (right − 60, 8) | frames 693–696, `F2`; opens the in-game options menu (`CMenuInterface`, see `interface.md`): the tactical view (your turn) enables Save, Surrender and Quit; the waiting view only Save; the deployment, reinforcement, automated-unit and map views only Quit (Save also needs a campaign and the host role) |

A message is shown with a colour (almost always white) and a lifetime
(5 s for errors and hints, 2 s for turn notices, 10 s for automated-unit
instructions) and then cleared. Messages seen in the tactical screens:
576–580 (movement), 320–325 (shots), 448–452 (deployment), 71, 896 ("%1
reinforcements have arrived"), 897 (whose turn begins).

### 2.9 Clock and turn time limit

**Source.** The limit is the session's *turn time* in minutes
(`Server Mode Info.Turn Time`, saved and loaded with `Difficulty`; string
742 "turn delay" in the session info). The skirmish menus set it: single
player **15** minutes, multiplayer 3 / 6 / 9 minutes by the radio buttons
(`interface.md`); death match and campaign set it on their own path *(?:
15 minutes was seen there too)*. When the battle starts the server sends
`CTacticPhaseMsg` with:

- the tactical limit = turn time,
- the deployment limit = **2 × turn time**,
- a "reinforcements allowed" flag: 1, except death match with objective
  kind 10 → 0 (this flag creates the REINFORCEMENT button disabled, 2.2).

The first-deployment view gets 2 × turn time, the tactical view and the
waiting view get the turn time, each × 60 000 ms.

**Clock.** Once per second the view updates the clock in the message strip:

- **single player** (fewer than 2 players in the session): the clock shows
  the time elapsed since the view was opened (counting up); nothing else
  happens — the limit has no effect in single player;
- **multiplayer:** the clock shows the time left (`limit − elapsed`, not
  below 0). Tactical view, after each tick: if the time left is ≤ 0, no
  animation is playing and the view is not already waiting for the server,
  it runs the END TURN action; while an animation plays it retries on the
  next tick. While 0 < time left < 10 s the `TIMER` sound plays every
  tick. The waiting view only shows the clock. The first-deployment view
  accepts the deployment at 0 (5.6).

Minutes are capped at 99. Cheat keys (3.7): `Tab`+`T` restarts the clock,
`Tab`+`O` sets it to one minute before the limit.

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
       facing becomes `facing + 1` (7 wraps to 0), only if its type's
       `Facings` flag is true (not for the Crystal Tower, OutPost and
       Platform). This changes **only the client's copy**: no message is
       sent, no AP is spent, the server keeps the old facing until the
       unit's next move. Facing has no game effect (tactical-combat.md
       3.2), so it is purely cosmetic. The sprite is redrawn and the path
       preview refreshed;
     - it is an own unit that may be selected (owned by you, not a convoy
       unit) → **select it** (or add it, GROUP on), play `SELECT`, centre
       the map on it, leave fire mode, show the path preview to its
       remembered destination if it has one, refresh the info box;
     - otherwise, if a unit is selected and the clicked unit is an
       **enemy** (tactical-combat.md 1.1 relation) → shot request at it;
   - **a building** that is an enemy building of yours and a unit is
     selected → shot request *(the building type must also pass a category
     test, probably "can be targeted" (?))*;
   - **a mine visible to you** and a unit is selected → shot request;
   - **a wall** and a unit is selected → shot request;
   - **empty tile** (or anything else) and a unit is selected → movement,
     3.3.

**Shot request = two clicks, "mark, then fire".** It needs at least one
selected unit; L = the lead unit, P = the clicked tile. Every unit has a
*marked tile* (the pending shot target) and a *remembered destination*;
both are "none" when unset.

1. If P is not L's marked tile, L's remembered destination is cleared (and
   its planned path dropped).
2. The path preview is removed.
3. The full shot legality check (tactical-combat.md 1, including AP and
   side) is run for L against P. Let D be L's remembered destination at
   this moment. If the check passed, L's remembered destination is set to
   L's own tile.
4. **First click** (D was none):
   - legal: L's marked tile := P; the red `SU000` marker is drawn on layer
     18 at P (2.10). Nothing is sent.
   - not legal: the client looks for a move-to-fire tile for L
     (tactical-movement.md 3.7). If there is none, the legality error is
     shown for 5 s — string 320 invalid target, 321 no weapon, 322 not
     enough AP, 323 out of range, 325 too close, 324 no line of fire — and
     nothing else happens. Otherwise L's marked tile := P, L's remembered
     destination := the firing tile, the red marker is drawn at P and the
     path preview to the firing tile is shown (with the Overwatch budget,
     tactical-movement.md 2).
5. **Second click on the same tile** (D was set, which only happens when P
   is L's marked tile after a successful first click): a
   `CRequestUnitShootMsg` is built with the target P and the auto-fire
   lamp state. For **each selected unit**, in selection order, a
   move-to-fire path is computed with the group claims of
   tactical-movement.md 3.5 and the unit is added with that path, which may
   be empty. The request is sent; then L's remembered destination and
   marked tile are cleared, FIRE is switched off, the view enters its
   waiting state and redraws.

The server then decides per unit: fire from the current tile if legal,
otherwise walk the given path and check again (tactical-combat.md 2). The
client never decides "direct shot or move-to-fire" for the other selected
units; it always sends a candidate path. A shot click therefore discards a
planned move of the lead unit. The marked tiles of all units are reset when
your turn starts.

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
| 1 | a unit is selected and the tile is the lead unit's marked tile (pending shot target, 3.2) | `CSELT` |
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
  line of fire) → `CMVSH` ("move and shoot": a click plans a move-to-fire,
  3.2); in fire mode, if the lead unit is immobile (`Movement` 0 or 8) and
  selected alone, `CINVA` instead.

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
(units with 0 AP become *done* only when you cycle away from them). If there
is no candidate, nothing happens.

The *done* mark is a client-side flag. It is **never cleared during a
battle** in the original: only GUARD and previous / next set it, the
turn-start handling resets the marked tiles but not this flag, and server
unit updates do not touch it. A GUARDed unit therefore stays out of the
cycle for the rest of the battle (it can still be clicked). *(Original
quirk; OpenDay may clear the marks at the start of each of your turns — to
be listed as a deliberate change.)*

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

A large top-down map of the whole battle that replaces the viewport while
it is open (the manual's "Overview Map"). The city screen of the campaign
has a sibling screen built on the same base (`CMapScreenStrat`, panel 578,
[strategic-interface.md](strategic-interface.md) 4.2); 7.1 describes what
both share.

### 7.1 Common layout of the map screens

Positions relative to the side panel (at (504, 0) / (840, 0)).

| Element | Position, size | Frames | Key | Notes |
|---|---|---|---|---|
| panel | (0, 0) | tactical 41, city 578 | — | |
| emblem | (5, 0), 44×43 | tactical 88, city 627 | `F1` | encyclopedia at the selected list row's type ([encyclopedia.md](encyclopedia.md) 3) |
| resource indicator | (64, 26)–(104, 40) | — | — | tactical: the player's tactical resource, the same number as in the battle views (2.4); during the first deployment the remaining deployment budget *(?)*. City: the player's credits *(?)* |
| name box | (6, 53)–(127, 68) | — | — | standard font: the selected item's name (player name, or "Alien") |
| player selector | 19×19 radio items at (10, 73), (29, 73), (48, 73), (67, 73), (86, 73) | by colour, see below | — | |
| list box | (12, 96)–(128, 426) | — | — | rows `"  <name> x<count>"` (two leading spaces) |
| list up / down | (8, 431) / (75, 431), 53×17 | 13–16 / 17–20 | — | scroll |
| close button | see 7.2 / strategic-interface.md 4.2 | | `Esc` | back to the view that opened the screen |

**Player selector.** One item per player of the game in player-list
order, then (tactical screen only, and not in a death match *(?)*) one
"Alien" item (string 65) for owner 0. Each item is a six-frame toggle whose
first frame depends on the item's colour number c (map-objects.md, *Color
variant*): c = 0 (Alien) → **46**, 1 → **52**, 2 → **58**, 3 → **64**,
4 → **70**. Each item remembers its colour, owner slot and name. When the
screen opens, the local player's item is selected. Choosing another item
refreshes the name box and the list.

**Map square.** Side `min(view_w, view_h)`, centred horizontally in the
viewport (448×448 at (28, 0) for 640×480; 728×728 at (56, 0) for 1024×768).
In it:

1. The scenario's overview picture `Scenario Info.mapScreen640` (396 px) or
   `mapScreen1024` (656 px), by resolution, centred in the square. **No
   terrain colours are drawn**; the picture replaces the minimap's terrain
   layer.
2. The whole map is shown: scale `s = picture width / map size` (floating
   point), block `b = ceil(s)`.
3. Object dots exactly as on the minimap (2.6, sizes `b + 1 + k`, colours
   from `Colors`), in the order buildings, roads, walls, units.
4. Markers (7.2, tactical only).
5. The owning view's viewport frame (1-px black rectangle with a 1-px white
   one inside), drawn over everything at every repaint.

**Selection blink.** Selecting a list row stores its type. A timer
(tactical **175 ms**, city **250 ms**) advances a phase 0 → 1 → 2 → 3 → 0.
On every timer tick the dots of the objects of the selected type owned by
the selected player are redrawn with a brightness offset: phase 0: −255
(black), phase 2: +255 (white), phases 1 and 3: 0 (normal colour). The
offset is added to R, G and B of the dot's palette colour, each clamped to
0 … 255, and the result is mapped to the nearest palette entry. The other
dots are drawn normally; the map is repainted after each tick.

**Click on the map.** Only if the screen is *clickable* (7.2; the city
screen always is), and only for a left-button press inside the picture: the
point is converted to a tile, `tile = trunc((point − picture origin) / s)`
for x and y, the owning view is centred on that tile and the map screen
closes.

### 7.2 Tactical map screen (`CMapScreenTactic`)

Opened by MAP (`F4`) in the tactical, waiting, automated-unit and deployment
views (close button BACK), and **once automatically** when the first
deployment view opens (close button CONTINUE; a once-per-battle flag).

- Panel frame **41**, emblem **88**.
- Close button at (10, **458**), 53×17, `Esc`: BACK frames **84–87**, or
  CONTINUE frames **700–703** when opened by the first deployment.
- Clickable only when the client's active player is the local player
  (your own turn or your deployment) *(?)*.
- Player items: all players plus "Alien" (owner 0), except in a death match
  *(?: game mode check)*.
- **List**: for the selected owner, one row per unit **type** (`typeID`)
  with the number of that owner's units of the type on the map, **convoys
  excluded** (`isConvoy`): `"  <type name> x<count>"`, the name taken from
  the (type, level 0) row. Order: first appearance in the scenario's unit
  list *(?)*.
- Selecting a row makes that owner's units of the type blink (175 ms).
  Emblem: unit page of the selected row's type (no context), else the
  encyclopedia's default page.

**Markers.** Drawn into the map picture once, when the screen is created,
using small bitmaps of `LIBDAY.EXE` (loaded with the game palette):

1. **Deployment zone**: every tile of the scenario's deployment-zone list
   whose `Turn` is ≤ the current turn value *(?: which value is compared;
   the shipped `Deployment.Turn` is always 0)* gets the **`SHADE`** bitmap
   (resource 213, 54×56, a red/black stipple with a colour key) **scaled
   down to one block** (b×b) at the tile's scaled position
   `(trunc(x·s), trunc(y·s))` relative to the picture. The shaded tiles are
   then grouped into rectangles *(?: grouping rule not traced; contiguous
   rectangular blocks)*, and at the centre tile of each rectangle a
   **`DEPL` flag** is drawn: resources 240–243, 16×25, one per colour 1–4
   (blue, yellow, purple, green); the flag of the **selected player item's
   colour** at creation, i.e. the local player's colour. The Alien item
   (colour 0) has no flag. The flag's top-left is at (scaled x − 10,
   scaled y − 24), clamped to ≥ 0. *(?: whose zones the list holds;
   probably the local player's.)*
2. **Objective zone**: if the battle has an objective zone, its tiles get
   `SHADE` the same way, and each rectangle centre gets **`OBJ00`**
   (resource 152, 8×20) at (x − 6, y − 19).
3. **Tech buildings**: the positions of all buildings on the map whose type
   has `TechReward` > 0 are collected and grouped; a light bulb **`LBULB`**
   (resources 305–308, 15×22) is drawn at each group centre at
   (x − 2, y − 8). The bulb image is 305 + the blink phase, so the bulbs
   animate with the 175 ms timer (redrawn on every tick when there is at
   least one).

Changing the selected player item does **not** redraw the deployment flags
(they keep the colour of the initial item) *(?: no redraw found)*.

**OpenDay:** the `SHADE`, `DEPL`, `OBJ00` and `LBULB` bitmaps are resources
of the executable and must not be copied into the repository. Load them
from the user's `LIBDAY.EXE` at run time or draw substitutes (a 50 %
stipple of the zone tiles, small flag / marker / bulb shapes in the player
colours).

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

Dialog art frame 365 (448×480) at screen rectangle **(45, 0)–(493, 478)**,
set once; the report code makes no resolution-dependent change, so at
1024×768 it appears at the same screen position *(?: unless the dialog
layer itself is offset)*.
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
   | 360 | RANK (514) | the player's **score** from the result message (battle-flow.md 5.4), printed as an unsigned number | — |

   The first row starts two text lines below the header; each player takes
   two lines plus half a line of spacing (row step = 2.5 × font height).
   One row per **human** player of the result message, in slot order 1…n;
   there is **no row for the computer player** (aliens), and the rows are
   not sorted — "RANK" is only the score, no place (1st / 2nd) is computed.
   The header is at absolute screen y = 185 (the dialog's top is at 0).
4. Tech line: only the **campaign** report (`CAftermathCampgnInterface`)
   and only on success: string 518 ("New technologies found : %1!d!") with
   the tech points found, at y = 418, x = 25 from the text area's left
   edge. The counter-attack, skirmish and death-match reports draw nothing
   there (the value is in every result message but unused).
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
- Tactical map screen (7, `app/map_screen.*`): opened once when the first
  deployment view opens (CONTINUE), then the objectives dialog; MAP / `F4`
  of the deployment, tactical and waiting views opens it with BACK. It is an
  overlay over the battle view, which keeps running below it (the AI's turn
  continues; the message strip stays visible). The list follows the order
  of the battle's unit list. The deployment zone shaded is the local
  player's (whatever its `Turn`). The markers `SHADE`, `DEPL`, `OBJ00` and
  `LBULB` are read at run time from the user's `LIBDAY.EXE`; their colour
  key is the colour of their top-left pixel *(OpenDay choice)*; without the
  program file OpenDay draws a red stipple, a small flag, a marker and a
  bulb. Shaded tiles are grouped greedily into rectangles from the top-left
  (row runs grown downwards) *(OpenDay choice)*. MENU / CHAT of the strip
  are not reachable while the map screen is open.
- Encyclopedia: the emblems (`F1`) of the deployment / reinforcement view
  (selected list type), the tactical view (lead selected unit) and the
  waiting view (default page), and the map screen (selected row's type).
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
- Automated units view (6.2): panel 507, the usable list (icon, name,
  cost; the first row selected on opening), the explanation for 10 s when
  the selection changes, the per-type cursor over valid tiles and `CINVA`
  elsewhere, the bridge-site markers, the transport's two points, strings
  448 and 320, and string 71 in the message strip (not a message box) when
  nothing is usable. The scroll buttons sit below the list at (9, 404) and
  (70, 404), 53×17 *(OpenDay choice)*. The resource indicator shows the
  tactical resource in the tactical views (2.4).
- Not implemented yet: turning a
  unit by clicking it, chat, the turn time
  limit, the ability icons of the info card, statistics beyond units and
  buildings lost and destroyed (the RANK column shows 1).

## 12. Open questions

- Frames 227–230 (66×9 red bars) of the tactical block: no user found.
- The building category test made before a shot request from a building
  click (3.2; probably "can be targeted").
- The display flag toggled by `Tab`+`G` (assumed to be the isometric grid).
- (Answered: the marked tile, `CSELT` and `CMVSH` — the two-click shot of
  3.2, auto-fire only sets the request flag; the MOVE ALL test, 2.2; *done*
  marks are never cleared, 3.8; the red number is the Hold Position
  counter, 2.5; turning a unit is client-only, 3.2; the battle report's
  RANK column is the score, 9.2.)
- Scroll-button positions of the deployment and automated-unit list boxes;
  the first deployment view also creates a 450×250 surface from panel frame
  0 whose use was not found.
- Reinforcement view: when the request is sent; whether the deployment
  zone is used (battle-flow.md 4.2 leaves this open too).
- Positions of the objectives dialog and the battle report at 1024×768
  (the report code itself uses fixed coordinates, 9.2).
- The turn time used by the campaign and death match (15 minutes seen).
- Terrain → minimap colour conversion (assumed nearest palette entry of
  `Color_Red/Green/Blue`) and whether mines appear on the minimap.
- (Answered in campaign.md 7 and strategic-interface.md 7: the closing
  videos follow the battle report when the campaign ends; the counter-attack
  video comes before waiting screen 6.)
