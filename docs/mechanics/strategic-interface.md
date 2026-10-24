# Strategic screens: continent screen, colony map, intelligence, overview maps, campaign reports, new campaign, videos

The campaign screens outside the battles and the city screen. The rules
behind them are in [campaign.md](campaign.md); the city screen is in
[city-building.md](city-building.md) 9, the army screen in
[army-tech.md](army-tech.md) 4. Conventions as in
[tactical-interface.md](tactical-interface.md): positions are pixels relative
to the owner, sizes w × h, "frames a–d" are consecutive atlas frames
(normal, hover, pressed, disabled; +4 checked for toggles).

The game views share one frame: map viewport on the left (504 × 448 at
640 × 480, 840 × 728 at 1024 × 768), side panel to its right (x = 504 / 840),
message strip below the viewport (with CHAT `F3` and MENU `F2`), and an
emblem button at (5, 0) 44 × 43 in the panel (`F1`, encyclopedia).

Status: derived from studying the original program and checked against the
extracted art. Items marked *(?)* are not fully confirmed.

## 1. Continent screen

### 1.1 Variants

| Opened | Mode | Hint text (no province selected) |
|---|---|---|
| CAMPAIGN on the city screen | normal: BACK enabled | 884 "Choose the province you wish to attack." |
| on arrival on colonies 2–4 (exactly one captured province) | continue: CONTINUE instead of BACK | 882 "Click the continue button." |
| after a won battle (win view, 1.6) | map not clickable | 883 "You are victorious. Hit the continue button." |

(In network games a non-host gets TO BATTLE instead of ATTACK and hint 885.)

### 1.2 Side panel (frame 192, emblem 214–217)

| Element | Position, size | Frames | Key | Notes |
|---|---|---|---|---|
| deployment points | (63, 26)–(101, 40) | — | — | number |
| credits | (63, 56)–(101, 70) | — | — | number |
| tech points | (63, 86)–(101, 100) | — | — | number |
| text box | (10, 150)–(128, 380) | — | — | hint or province text (1.3) |
| INTELLIGENCE | (11, 387) 110×17 | 198–201 | `I` | 2 |
| ATTACK | (11, 407) 110×17 | 202–205 | `A` | disabled until an attackable province is selected |
| BACK | (11, 456) 110×17 | 210–213 | Esc | back to the city screen |
| CONTINUE (continue mode) | (11, 456) 110×17 | 21–24 | Esc | same |

Numbers: clamped to 0 … 99999, standard font, white on an opaque black
background, centred horizontally, 2 px below the top.

### 1.3 Province text

Drawn word-wrapped into 118 × 230 at (10, 150), standard font, white on black:

1. the province name;
2. if the province is attackable ([campaign.md](campaign.md) 3): the first
   two `##`-separated fields of its `Description` (story, turn-limit heading)
   on separate lines, a blank line and string 865 "Credits: %1!d!" with
   `scale(Province List.Credits, C(d))`;
3. otherwise string 802 "Province is too far."

### 1.4 Selecting a province

Clicking a province that is not captured selects it (blue overlay, 2.4),
redraws the text, enables ATTACK if it is attackable (else disables it) and
plays `SELPROV`. Captured provinces cannot be selected.

### 1.5 Buttons

- INTELLIGENCE: the intelligence dialog for the selected province (3).
- ATTACK: loading picture 12, then the province battle.
- BACK / CONTINUE: close (city screen).
- MENU: in-game options with SAVE.

### 1.6 Win view

After a won battle: the same screen without province text, INTELLIGENCE and
ATTACK; one CONTINUE at **(11, 454)**, frames 21–24, Enter. The captured
province blinks (2.5, 10 alternations), sound `PROVINCE` on opening. CONTINUE
→ loading picture 11 ("preparing the city phase") → the city screen (or the
next colony).

## 2. Colony map

### 2.1 Frames of `COLANMS\C<n>.SMK`

840 × 728; with B = `SmackProvFrameBegin` (0) and Q = `SmackProvFrameQty`
(30) the background loop is frames B … B+Q−1 (0 … 29); for province p
(1 … N):

- **selection frame** S(p) = B + Q − 2 + 2p (30, 32, …): province p tinted blue;
- **owned frame** O(p) = S(p) + 1 (31, 33, …): province p green with the human
  marker.

Everything outside the province in these frames is palette index 0, the
colour key. The alien markers and capitals are part of the background art.
The file is shown with the **game palette** (its own palette is the same up
to rounding). Frame rate: the file's (15 fps; `C3.SMK` 10 fps).

### 2.2 Placement

The 840 × 728 picture is centred in the viewport:
origin = ((view_w − 840) / 2, (view_h − 728) / 2) — (−168, −140) at
640 × 480 (only the central part is visible), (0, 0) at 1024 × 768. No
scrolling.

### 2.3 Composition per frame

1. the next background frame (looping B … B+Q−1);
2. the **owned layer**: O(p) of every captured province, transparent;
3. the **selection layer**: S(p) of the selected province, transparent.

The ocean only animates while the continent screen is on top.

### 2.4 Hit test

For each province, the non-key pixels of S(p) form its mask. A click at
(mx, my) tests (mx − ox, my − oy) against the masks in province order,
skipping captured provinces; the first hit is selected.

### 2.5 Blinking

Set with a count (10 in the win view): every 4th composed frame the count
decreases and the selection layer shows O(p) for odd counts and S(p) for even
ones; at 0 it stays S(p).

## 3. Intelligence dialog

Panel frame **557** (513 × 311) at the screen rectangle (40, 40)–(553, 351).
BACK (364, 250) relative to the dialog, 110 × 17, frames 558–561, Esc.

- With a **Satellite Uplink** (`H_Center_Satellite`) owned in the city: the
  rectangle (50, 50)–(250, 250) (screen) is filled with black and the province
  picture `Province List.mapScreen<v>` (v = the province's current variant;
  8-bit BMP, 100–176 px square) is drawn centred in it.
- Text, standard font, white (screen coordinates): the province's `Title`
  centred at x = 396, y = 60; the province name at (260, 90); then
  - without an uplink: string 803 "You must build a Satellite Uplink to see
    more information." centred and wrapped in (60, 150)–(533, 321);
  - with an uplink, from y = 120 in (260 … 533): fields 2 and 3 of the
    description (turn heading and objective), 20 px gap, the `Objective` hint,
    20 px gap, the province's `Tech` value.

## 4. Overview maps

Two screens share one base: the battle's overview map (4.1) and the city
overview map (4.2). The common layout (player selector, list, map square,
selection blink, click) is specified in
[tactical-interface.md](tactical-interface.md) 7.1; the battle variant in
detail in tactical-interface.md 7.2.

### 4.1 Pre-battle overview map

Shown when the deployment view first opens (the manual's "Overview Map
screen"); the in-battle MAP button opens the same screen with BACK instead
of CONTINUE.

- Panel frame **41**, emblem 88. Indicator (64, 26)–(104, 40): deployment
  points. Player name box (6, 53)–(127, 68). Army selector: 19 × 19 radio
  items at (10, 73), (29, 73), … one per human player, then the aliens
  (string 65), frames by colour: alien 46, player 1 52, 2 58, 3 64, 4 70
  (6-frame toggles). List box (12, 96)–(128, 426) with entries
  `"  <type name> x<count>"` of the chosen army's units; up / down (8, 431) /
  (75, 431) 53 × 17, frames 13 / 17. Bottom: CONTINUE frames 700–703 53 × 17
  at (10, 458), Esc (BACK frames 84–87 in battle).
- Map square of side min(view_w, view_h) centred in the viewport (448 at
  (28, 0); 728 at (56, 0)). In it, centred: `Scenario Info.mapScreen640`
  (396 px) or `mapScreen1024` (656 px). Dots for buildings, units, roads and
  walls as on the minimap, scale = picture width / map size.
- Markers (bitmaps of the original program; OpenDay draws its own): shaded
  deployment zone with a flag, shaded objective tiles with an objective marker,
  a light bulb over tech buildings.
- Selecting a list entry makes that type's dots blink. A click on the map
  closes the screen and centres the view there.

### 4.2 City overview map (`CMapScreenStrat`)

Opened by MAP (`F4`) on the city screen (city-building.md 9). The city
screen's current build / recycle / move mode is reset first *(?)*. Layout
and behaviour as in tactical-interface.md 7.1, with these differences:

| Element | Position, size | Frames | Key | Notes |
|---|---|---|---|---|
| panel | (0, 0) | **578** | — | 136×480 / 184×768 |
| emblem | (5, 0), 44×43 | **627** (4 frames) | `F1` | building page of the selected row's type (no context); no row → the encyclopedia's default page ([encyclopedia.md](encyclopedia.md) 3) |
| resource indicator | (64, 26)–(104, 40) | — | — | the player's credits *(?)* |
| CONTINUE | (10, **457**), **110×17** | **623–626** | `Esc` | closes the screen, back to the city screen |

- Player items: one per player; **no "Alien" item**.
- Always clickable: a left click on the picture centres the city view on
  that tile and closes the screen.
- **List**: for the selected player, one row per **building type** that
  the player owns on the city map, with the number of buildings of that
  type: `"  <building name> x<count>"` (`Name<lang>` of the type). Every
  building of the city map counts, towers included; walls, roads and mines
  are not buildings and are not listed. Row order: the order in which the
  types are first met while walking the city's building list *(?)*.
- Selecting a row makes the selected player's buildings of that type blink
  (phase timer **250 ms**, tactical-interface.md 7.1).
- No markers (no deployment, objective or tech-building bitmaps).
- While open, the screen listens to the building, wall, road and mine
  update messages *(?: presumably to redraw the dots if the city changes;
  in single player the city cannot change while the screen is open)*.
- Keys: only `Esc` and `F1`.

## 5. Campaign battle reports

The battle report layout is that of tactical-interface.md 9.2.

- Province battle: on a win the scenario's `Aftermath` text, then string 518
  "New technologies found : %1!d!" at y = 418; on a defeat string 520.
- City battle (first attack, counter-attack): the same without the tech line.
- CONTINUE: if the campaign is over ([campaign.md](campaign.md) 7) the video
  `FH2END` (won) or `FH2LOSE` (lost), then loading picture 1 and the main menu;
  otherwise loading picture 1 and the campaign continues.

## 6. New campaign menu

Panel frame **939** (286 × 228), centred at 640 × 480 (the art has the
captions NEW GAME, ENTER YOUR NAME, DIFFICULTY, EASY, HARD).

| Element | Position, size | Frames | Key |
|---|---|---|---|
| name | (15, 55) 252×21 | — | up to 16 characters; default the user's name, or string 739 "Liberator" |
| ◀ | (17, 114) 19×17 | 180–183 | value − 1 (not below 0) |
| track | (39, 114) 208×17 | 1047 | not clickable |
| thumb | x = 39 + v × 208 / 11, y = 114, 19×17 | 677 | |
| ▶ | (250, 114) 19×17 | 176–179 | value + 1 (not above 10) |
| BACK | (10, 184) 100×24 | 940–943 | Esc |
| CONTINUE | (170, 184) 100×24 | 944–947 | Enter |

The slider value v (0 … 10, initially **4**) is the difficulty d. An empty
name (after trimming) refuses CONTINUE. CONTINUE shows loading picture 2 and
starts the campaign.

## 7. Videos

`VIDEO\<name>.SMK`, full screen, centred, black around, with the video's own
palette; music is paused. Skipped by a mouse button, Esc, Enter or Space.

| When | Video | Afterwards |
|---|---|---|
| new campaign | colony 1 `Intro` (`INTRO1`) | loading picture 7, first city attack |
| next colony | `Intro2` / `Intro3` / `Intro4` | loading picture 8 / 9 / 10, strategic phase |
| counter-attack | `COUNTERA` | loading picture 6, city battle |
| campaign won / lost | `FH2END` / `FH2LOSE` | loading picture 1, main menu |

## 8. Sequence

1. Main menu → NEW CAMPAIGN → new campaign menu → loading picture 2.
2. `INTRO1` → loading picture 7 → the first city attack: deployment (with the
   overview map first), battle, report.
3. Win view on the continent screen (the city province) → loading picture 11.
4. City screen. CAMPAIGN → continent screen; MAP → city overview; ARMY → army
   screen. On colonies 2–4 the continent screen opens first in continue mode.
5. Continent screen: select a province, INTELLIGENCE, ATTACK → loading
   picture 12 → deployment (overview map first) → battle → report.
6. Won: counter-attack (`COUNTERA`, loading picture 6, city battle, report) if
   the province has one; then the win view → city screen. Lost: back to the
   city screen.
7. Alien capital won: next colony intro → city screen; colony 4: `FH2END`.

## Implementation notes (OpenDay)

Implemented in `app/campaign_*.cpp`.

- The overview-map markers are drawn with OpenDay's own simple shapes (the
  original's bitmaps are resources of the program).
- The new-campaign name defaults to "Liberator" (string 739).
- The continent screen's deployment-point number shows the deployment
  points the city gives in a battle there (campaign.md 6.4) *(OpenDay: the
  original's source of this value was not identified)*.
- The city overview map (panel 578) is not implemented yet (MAP is disabled
  on the city screen); its specification is 4.2.
- The encyclopedia (emblem buttons) is not implemented yet; specification:
  [encyclopedia.md](encyclopedia.md).

## Open questions

- Whether the resource numbers on the continent screen update while it is
  open.
- How the deployment zones are filtered on the overview map (the
  `Turn` ≤ current-turn test and whose zone list is shaded,
  tactical-interface.md 7.2).
- City overview map: what the screen does with the building / wall / road /
  mine update messages it listens to, and the row order of its list.
