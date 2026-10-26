# User interface: text, strings, screen flow, menus, cursor

How the original program builds its front end and dialogs. The art comes from
the interface atlases described in
[`../formats/INTERFACE-SMK.md`](../formats/INTERFACE-SMK.md) (frame numbers
below refer to that document). The tactical HUD, the deployment screen and
the briefing/aftermath screens are in
[`tactical-interface.md`](tactical-interface.md).

Unless stated otherwise, coordinates are pixels relative to the top-left
corner of the panel that owns the control, and sizes are w×h.

## 1. Text rendering

The game has **no bitmap font of its own**. All run-time text is drawn with
Windows GDI (`DrawText`/`ExtTextOut`) onto the 8-bit DirectDraw surface, using
the TrueType face **Arial**. Every fixed caption (menu titles, button labels,
panel headings) is pre-rendered into the atlas art instead.

### Fonts

At start-up the application creates three fonts and keeps them for the whole
session; one more is created by the multiplayer lobby:

| Font | Face | Height | Weight | Used by |
|---|---|---|---|---|
| small | Arial | 12 | normal | created, but no user was found |
| **standard** | Arial | 14 | normal | nearly all in-game text: strategic and tactical views, intelligence screens, army/deployment lists, aftermath, message area and message boxes, chat, encyclopedia, load/save slot names, the CD-player track list of the options dialog |
| **large** | Arial | 17 | normal | text in the front-end menus: edit boxes (player name, phone number, chat line), multi-line text boxes (scenario descriptions, session/game information), the wait box |
| lobby | Arial | 17 | bold (700) | the multiplayer lobby (`CMenuLobby`) |

"Height" is the Windows logical font height as a positive number, i.e. the
**cell height in pixels including internal leading** (not the em size). For
Arial this gives a cap height of roughly 0.72 × height (about 10 px for the
standard font). The three session fonts are created with default width,
weight, quality and character set ("default charset"); the lobby font uses
family "swiss, variable pitch". No anti-aliasing is requested; on an 8-bit
surface GDI draws plain, non-smoothed glyphs. *(OpenDay: the same plain glyphs go into
the picture; the "Sharp text" option in section 4 draws the text again,
smoothed, at the window's resolution.)*

**Substitution for OpenDay:** any metric-compatible sans-serif works
(Liberation Sans or Arimo are metric-compatible with Arial). Render without
anti-aliasing (or quantise to the palette), at pixel sizes such that the line
height equals 12 / 14 / 17 px, and use a bold variant for the lobby.

### Colours

- Text colour is almost always **white** (RGB 255,255,255), mapped by GDI to
  the nearest palette entry (palette index 255 is white in `PAL.BMP`).
- Background mode is normally **transparent**; a few list/caption helpers
  draw opaque text on a **black** background (index 0).
- Exceptions found: one call uses pure red (RGB 200,0,0) and a few take a
  caller-supplied colour (e.g. player colours); those belong to in-game
  screens.
- Alignment is per control: centred captions (centre of the rectangle),
  left-aligned lists, word-wrapped multi-line text boxes.

## 2. Strings

### Where they live

| Place | Contents |
|---|---|
| Language DLL (`LIBDAYEN.DLL`; French/German: `LIBDAYFR.DLL`, `LIBDAYGR.DLL`) | **string table** (~310 strings incl. MFC defaults), the two game cursors, one MFC dialog and a few MFC bitmaps |
| `LIBDAY.EXE` | five strings of its own: 1 product name, 2048 version ("Gold 1.4"), 2049 expiry, 2050 a window-creation error, 2051 a language-DLL error |
| MDB databases | names and descriptions of units/buildings, scenario and briefing texts (see `data-model.md`) |
| Interface atlas art | all menu titles and button captions (baked into the pictures) |

The DLL is chosen by the integer key `Language` in section `[Fallen Haven II]`
of `LIBDAY.INI`: `0` = `LibDayEn.dll`, `1` = `LibDayGr.dll`,
`2` = `LibDayFr.dll`; any other value means 0. (The shipped INI contains the
text `LibDayEn.dll`, which reads as 0.) Strings are fetched with the ordinary
Windows "load string by numeric id" mechanism; a string-table block *b*
holds ids `16·(b−1) … 16·(b−1)+15`.

### Id ranges (English text summarised)

| Ids | Meaning |
|---|---|
| 1 | product name |
| 22–49 | encyclopedia field labels (type, level, armor, action points, armament, avg damage, ROF, range, prerequisite, grav/ground/water, cost, recycle value, production, terrain classes) |
| 64–84 | misc. game messages (no bridge, "Alien", no automated units, "%1 units selected", no-selection text) |
| **100–125** | **one-character hot keys** for interface buttons (localised letters). A button's hot key is the **first character** of its string. Known: 122 = *Save* ("S"), 123 = *Quit* ("Q") in the options dialog |
| 164–235 | short labels for unit/upgrade panels (AR, DAM, Ruin, armor kinds, PLAYER, AP, AREA, Tech, ACTUAL, MAXIMUM, DAM BASE, DAMAGE AA) |
| 256–282 | start-up and fatal errors (sound, window, global data, network, CD, DirectDraw 269, palette 270, smacker 274, image files 275, zips 276, cache dir 277, server 278/279, CPU affinity 280, interface file 281, animations 282) |
| 288–291 | login errors |
| 320–325 | shooting errors (tactical) |
| 384–408 | building errors and notices (strategic) |
| 448–453 | deployment messages |
| **512–521** | **aftermath / battle report**: DESTROYED, BATTLE REPORT, RANK, LOST, "%1 Units", "%1 Buildings", new technologies, victory, failure, final ranking |
| 576–580 | movement errors |
| 640–673 | unit stat captions with values (AR, AP, RANGE, OVERWATCH, YES/NO, TECH) |
| **737–747** | **front-end / session info**: Easy, Hard, default player name (the default player name), New campaign, difficulty %, turn delay, Skirmish, scenario name, Death match, Loaded campaign, game name |
| 800–803 | demo/intel restrictions |
| **816–819** | **in-game menu**: "...SAVING...", save failed, confirm surrender, confirm quit |
| 832 | player left |
| 848–865 | unit upgrade / laboratory |
| **880–885** | strategic-phase hints (continue, victorious, choose province, battle button) |
| 896–897 | turn notices |
| **912–915** | difficulty names: Easy, Average, Hard, Very hard |
| 61440+ | MFC framework defaults |

Placeholders use the Windows message-format syntax (`%1`, `%1!d!`, `%2!d!`,
`%%`); OpenDay must implement that subset.

Mission briefing texts are not in the DLL; they come from the scenario
databases (covered by `tactical-interface.md` / `data-model.md`).

## 3. Screen flow

### Architecture

Every screen is an *interface* object (base class `CInterface`, with the
derived families `CMenuBase` for front-end menus, `CFH2Dialog` for in-game
dialog boxes, and full-screen views such as `CStratView`, `CTacticView`).
The main window (`CFH2MainWndInterf`) owns a **stack** of interfaces
(`CInterfaceStack`); the top one receives input and is drawn last.
Controls are child interfaces: `CButtonInterf`/picture buttons,
`CRadioButtonInterf` (a group of toggle items), `CListBoxInterf`,
`CTextBoxInterf`, `CEditBoxInterf`, `CScrollBarInterf`/`CBmpScrollBarInterf`.

The front end is driven by a client "menu phase" (`CClientPhaseMenu`), a
small state machine that destroys the current menu and creates the next one.

### Start-up

1. Read `LIBDAY.INI`, load the language DLL, set up DirectDraw, Miles sound,
   Smacker.
2. Load `PAL.BMP` (error 270), `PALCYCLE.PAL`, `TICK.SMK` (error 274),
   `GLOBAL.MDB`, the scenario/colony zips, `ANMSUNIT.FF`, create the fonts,
   open the interface atlas for the configured resolution, load the sounds.
3. Enter the menu phase. **The menu phase always switches the display to
   640×480**, without saving it; the user's `Resolution` is applied only when
   a game is started.
4. If `Intro=1` (default 1) and this is the first entry after program start:
   play the intro video **`VIDEO\FH2INTRO.SMK`** (640×400) in an intro
   interface (`CAnimIntroInterf`). When it ends, show the main menu.
   Otherwise go straight to the main menu.

### Front-end states

| State | Screen (class) | Panel frame |
|---|---|---|
| 0 | intro video (`CAnimIntroInterf`, "FH2Intro") | — |
| 1 | **main menu** (`CPrincipalMenu`) | 710 |
| 2 | resolution (`CMenuGameResolution`) | 777 |
| 3 | new campaign, single player (`CMenuNewCampaignSingle`) | 939 |
| 4 | load campaign (`CMenuLoad`) | 739 |
| 5 | skirmish, single player (`CMenuSkirmishSingle`) | 1046 |
| 6 | network service selection (`CMenuServices`) | 823 |
| 7 | session list (`CMenuSessions`) | 917 |
| 8 | multiplayer game type (`CMenuGameType`) | 760 |
| 9 | new campaign, multiplayer (`CMenuNewCampaignMulti`) | 1045 |
| 10 | load campaign, multiplayer (`CMenuLoad`) | 739 |
| 11 | skirmish, multiplayer (`CMenuSkirmishMulti`) | 841 |
| 12 | death match (`CMenuDeath`, multiplayer only) | 879 |
| 13 | player set-up after choosing a game type (host) (`CMenuPlayerMulti`) | 970 |
| 14 | player set-up after *Join* (`CMenuPlayerMulti`) | 970 |
| 15 | lobby / waiting area (`CMenuLobby`) | 1013 |
| 16 | leaving the menus: loading screen, game starts | `WAITING.SMK` |

Main-menu choices (callback argument → next state):

| Button | Choice | Leads to |
|---|---|---|
| New campaign | 0 | state 3 |
| Load campaign | 1 | state 4 |
| Skirmish | 7 | state 5 |
| Multi player | 2 | state 6 → 7; *Host* → 8 → 9/10/11/12 by game type → 13 → 15; *Join* → 14 → 15 |
| Intro | 3 | state 0 (replays the intro, then back to the main menu) |
| Resolution | 4 | state 2 (confirm returns to the main menu) |
| Quit | 5 | closes the main window (program exit) |

Multiplayer game type choices: 0 *New campaign* → 9, 1 *Load campaign* → 10,
2 *Skirmish* → 11, 3 *Death match* → 12, *Back* → main menu.

The **encyclopedia**, **options** and **credits** are not reachable from the
main menu of this version: the encyclopedia (`CEncyclopInterface`) and the
options dialog (`CMenuInterface`) are opened from inside the game, and the
*Credits* button art (frame 727) is never used. Death match exists only
through the multiplayer path.

When a game is started, the menu phase applies the stored resolution
(re-opening the matching atlas) and shows the `WAITING.SMK` loading picture
for the mode (new campaign 2, load 3, skirmish 4, death match 5; see
`INTERFACE-SMK.md`).

### `LIBDAY.INI`

Section `[Fallen Haven II]` (read once at start-up):

| Key | Default | Meaning |
|---|---|---|
| `Language` | 0 | language DLL index (see §2) |
| `Cheats` | 0 | enables cheat messages |
| `InWindow` | 0 | 1 = run in a window, do not change the display mode |
| `DualCPU` | 0 | 1 = show the CPU-affinity warning (string 280) |
| `LogNetwork` | 0 | network message logging |
| `NoIsoAnim` | 0 | 1 = do not play isometric animations |
| `Intro` | 1 | 0 = skip the intro video at program start |
| `Menu` | 4 | debug start mode, 0–4 (4 = normal menu) |
| `Scenario` | `debug\s0-debug` | debug scenario |
| `Savegame` | 0 | debug: load save slot 1–10 directly |
| `audio` | 0 | 1 = CD/music player available |

Section `[User]` (read at start-up; each value is written back as soon as it is changed, except `Resolution`, see below):

| Key | Default | Range | Changed by |
|---|---|---|---|
| `ScrollSpeed` | 1 | 0 slow, 1 normal, 2 fast | options dialog |
| `SoundVolume` | 1 | 0/1 — effectively sound **on/off** | options dialog |
| `ShowMoveAnim` | 1 | 0/1 | options dialog (*Move animations*) |
| `Autofire` | 1 | 0/1 | tactical HUD toggle |
| `Overwatch` | 1 | 0/1 | tactical HUD toggle |
| `Resolution` | 0 | 0 = 640×480, 1 = 1024×768 | resolution menu (written when the game starts) |

Section `[CD Audio]`: `track1` … `trackN` = display names of the music
tracks (N = number of audio tracks, at most 12). Section `[Installation]`:
directory of each data folder.

## 4. Menu mechanics

### Panels

A front-end menu (`CMenuBase`) consists of one panel image and its controls.

- The panel image is atlas frame *F* cropped to the panel size given by the
  menu class.
- The panel is **centred on the screen**: origin = ((screen w − panel w) / 2,
  (screen h − panel h) / 2) with integer division. As menus run at 640×480,
  the main menu (286×281) sits at **(177, 99)**.
- Everything outside the panel is filled with palette **index 0** (black);
  there is no full-screen menu background picture.
- Control positions are relative to the panel origin.

In-game dialogs (`CFH2Dialog`: options, army, intelligence, aftermath, chat,
wait box, encyclopedia) work the same way but take their rectangle from the
calling screen.

### Push buttons

A button has four images (normal, hover, pressed, disabled; consecutive atlas
frames) and a state:

| State | Image | Transitions |
|---|---|---|
| disabled | `+3` | set by the owner; ignores input |
| normal | `+0` | mouse moves inside → hover |
| hover | `+1` | mouse leaves → normal; left button down → pressed |
| pressed | `+2` | mouse leaves → normal (no action); left button up → normal **and the action runs** |

The action therefore runs on **release inside the button**. A double-click
message received while hovering does not trigger the action (the second
click of a fast double-click is lost). Each button may have a **hot key**: a
character (or Enter = 13, Esc = 27) that activates it from the keyboard.
Localised letter hot keys are the first character of a DLL string (ids
100–125).

No sound is played by the generic button code; menu clicks are silent in the
code paths examined (see open questions).

### Toggle / radio items

A toggle item behaves like a button but also has a checked flag. Its images
come from six consecutive frames: while unchecked as a push button
(`+0..+3`); while checked, `+4` is shown instead of the normal image and
`+5` instead of the hover image. Clicking an item of a radio group checks it
and unchecks the others; the group reports the index of the checked item.

### Main menu (`CPrincipalMenu`) — 640×480

Panel: frame **710**, 286×281, at (177, 99). Title "MAIN MENU" is part of
the art. Seven 256×28 buttons, all at x = 15:

| y (panel) | Absolute rect | Frames | Caption | Hot key | Action |
|---|---|---|---|---|---|
| 40 | (192,139)–(448,167) | 711–714 | New campaign | — | choice 0 |
| 70 | (192,169)–(448,197) | 715–718 | Load campaign | — | choice 1 |
| 100 | (192,199)–(448,227) | 765–768 | Skirmish | — | choice 7 |
| 130 | (192,229)–(448,257) | 719–722 | Multi player | — | choice 2 |
| 160 | (192,259)–(448,287) | 723–726 | Intro | — | choice 3 |
| 190 | (192,289)–(448,317) | 731–734 | Resolution | — | choice 4 |
| 220 | (192,319)–(448,347) | 735–738 | Quit | Esc | choice 5 |

### Resolution (`CMenuGameResolution`)

Panel: frame **777**, 286×228. A radio group with two 256×28 items:
*Low (640×480)* at (15, 50), frames 778…; *High (1024×768)* at (15, 90),
frames 783…; the current choice is pre-checked. Buttons (100×24):
*Back* at (10, 180), frames 799–802, Esc → main menu; *Save* at
(176, 180), frames 755–758, Enter → remember the checked index as the
requested resolution and return to the main menu. The display mode changes,
and `[User] Resolution` is written, only when a game is started.

### Multiplayer game type (`CMenuGameType`)

Panel: frame **760**, 286×236. Buttons 256×28 at x = 15: *New campaign*
(y 40, frames 711), *Load campaign* (y 73, frames 715), *Skirmish* (y 106,
frames 765), *Death match* (y 139, frames 769), *Back* (y 190, frames 773,
Esc).

### Other front-end menus (summary)

| Menu | Panel | Controls (panel coordinates) |
|---|---|---|
| New campaign, single | 939 (286×228) | name edit box (15, ·) 252×21, max 16 chars, large font; difficulty slider (track frame 1047); *Back* (10, 184) frames 940 Esc; *Continue* (170, 184) frames 944 Enter |
| New campaign, multi | 1045 (367×188) | *Back* (15, 150) frames 940 Esc; *Continue* (252, 150) frames 944 Enter |
| Load / Save | 739 / 750 (286×419) | ten numbered slots (standard font), *Back* 740/751, *Load* 744 / *Save* 755 |
| Skirmish single / multi, Death match | 1046 / 841 / 879 (364×416) | scenario list, description text box (14, 218) 333×118 (large font); turn-time radio *3 min* (170, 350) frames 858, *6 min* (230, 350) frames 868, *9 min* (290, 350) frames 863 (60×24); *Back* (12, 375) Esc; *Continue* (252, 375) Enter |
| Services | 823 (286×228) | service list; *Back* (14, 194) frames 828 Esc; *Continue* (168, 194) frames 824 Enter |
| Sessions | 917 (364×322) | session list; *Back* (12, 286) frames 918 Esc; *Host* (132, 286) frames 934; *Join* (252, 286) frames 930 Enter |
| Player set-up | 970 (286×228) | name edit (16, 56) 252 wide, max 16 chars; password edit, max 8 chars; four colour toggles 19×19 at x = 35, 70, 105, 140 (frames 989, 995, 1001, 1007); *Back* (12, ·) frames 971 Esc; *Continue* (174, ·) frames 975 Enter |
| Lobby | 1013 (560×469) | player list, game information text box (288, 72) 254 wide, chat area, chat edit (16, 394) 420×20 max 64 chars; *Send* (447, 392) frames 1018 Enter; *Back* (13, 428) frames 1022 Esc; *Start* (447, 428) frames 1014 |

### In-game options dialog (`CMenuInterface`)

Panel: frame **648**, 403×371 (title "OPTIONS" in the art). Opened from the
game screens (MENU button, `F2`); three constructor flags enable or disable
*Save*, *Surrender* and *Quit*. A disabled button is only drawn greyed
(control state "disabled"); there is no refusal message.

Which screen passes which flags:

| Screen | Save | Surrender | Quit |
|---|---|---|---|
| tactical view, your turn (`CTacticView`) | yes | **yes** | yes |
| tactical waiting view, another participant's turn incl. the computer's (`CTacticWaitView`) | yes | no | **no** |
| city screen (`CStratView`), continent screen (`CColonyView`) | yes | no | yes |
| every other screen (deployment, reinforcement purchase, automated units, tactical and strategic map screens, continent win view, colony base view, …) | no | no | yes |

*Save* is additionally forced off unless (1) the session is a campaign
(game type 0 new campaign or 1 loaded campaign; skirmish and death match
never save) and (2) this program instance hosts the server (in a network
campaign only the host can save). See [savegame.md](savegame.md) 1.

| Control | Position (panel) | Size | Frames | Notes |
|---|---|---|---|---|
| Sounds *On* / *Off* | (36, 71) / (36, 92) | 19×19 | 665 | radio; `[User] SoundVolume` |
| Move animations *On* / *Off* | (36, 158) / (36, 179) | 19×19 | 665 | radio; `ShowMoveAnim` |
| Scroll speed *Slow* / *Normal* / *Fast* | (36, 252) / (36, 273) / (36, 294) | 19×19 | 665 | radio; `ScrollSpeed` 0/1/2 |
| CD player track list | (173, 69), 208 wide, 12 rows × 17 px | | | track names from `[CD Audio]`, standard font; the playing track is selected |
| list previous / next | (280, 297) / (299, 297) | 19×17 | 685 / 681 | scroll the track list |
| *Play* | (242, 297) | 19×17 | 671 | plays the selected track |
| *Stop* | (261, 297) | 19×17 | 677 | stops the music |
| *Surrender* | (10, 340) | 110×17 | 689 | no hot key; confirmation (see below) |
| *Save* | (132, 340) | 53×17 | 661 | hot key string 122; opens the save dialog |
| *Quit* | (197, 340) | 53×17 | 649 | hot key string 123; confirmation string 819 |
| *Back* (resume) | (336, 340) | 53×17 | 653 | Esc; closes the dialog |

| *OpenDay* *(OpenDay addition)* | centred between *Quit* and *Back*, y 340 | 53×17 | see below | `O`; opens the OpenDay options |

(Earlier versions of this document called the button at (10, 340)
*Restart scenario*; that was wrong — it is *Surrender*.)

The CD-player controls are disabled when `[Fallen Haven II] audio=0`.

**Surrender:** opens a two-button (yes / no) message box with string **818**
("Do you really want to surrender?"). On *yes*: the box closes, the client
sends the cheat-lose request (`CCheatLoseScenMsg`; there is no separate
surrender message and no cheat gate on this path) and the options dialog
closes. The result of the battle is described in
[battle-flow.md](battle-flow.md) 3.1 and 5.6.

**Quit:** yes / no box with string **819** ("Do you really want to quit the
game?"). On *yes* the main window receives an application command that ends
the game *(?: whether it exits the program or returns to the front end was
not traced; exiting is assumed)*.

**Save:** opens the save dialog (below). After a name has been chosen:

1. the save dialog closes and a message box **without buttons** shows string
   **816** ("...SAVING...");
2. the save request (`CCampaignSaveMsg`: slot 1–10, name up to 31
   characters in a 32-byte field) goes to the server;
3. when the confirmation (`CConfirmSaveMsg`, one success flag) arrives, the
   "...SAVING..." box is closed; on failure an OK box shows string **817**
   ("A problem occured during saving. Please try again."). On success
   nothing else is shown; the options dialog stays open *(?)*.

### OpenDay options *(OpenDay addition)*

OpenDay's own settings, which the original does not have, are kept out of
the original dialogs: the in-game options of the battle views and of the
strategic screens get one more button, *OpenDay*, centred between *Quit*
(ends at x 250) and *Back* (starts at x 336). The atlas has no such button,
so it is made from the four frames of *Back* (653–656): the 5 columns at
each end are kept, columns 5–12 (plain face) are repeated in between, and
the caption is drawn in bold capitals, 10 px, 1 px letter spacing, colour
nearest to RGB (206, 196, 148); the button is 53 px wide or wider if the
caption needs it.

The dialog opens over the options at the same place and is drawn from the
same panel (648):

- The title strip is re-lettered: panel rectangle (152, 12)–(252, 35)
  is covered with the lettering-free part (88, 12)–(152, 35) repeated, and
  "OPENDAY" is drawn centred in (84, 13, 231×19), bold 17 px, 3 px letter
  spacing, colour nearest to RGB (162, 154, 103).
- The controls of the options are covered by one black box (14, 41,
  373×289) made from the CD player's box (167, 67, 220×225): the 3-pixel
  edges and corners are kept, the rest is repeated.
- Inside: the section headings side by side ("CAMERA", "DISPLAY", bold
  12 px, 2 px letter spacing), each centred in an equal share of
  (box.x + 16, box.y + 10, box.w − 32, 16); the chosen section's heading is
  in the title's colour, the other grey. A click on a heading or `Tab`
  (next section) shows that section; the dialog opens on *Camera*. Under
  the headings, one row per setting of the section: the on/off lamp (665) at x + 16, the name in
  the standard font in white and a description of up to two lines in grey
  under it (the first row at y + 32 of the box, the description 18 px
  under the name; a row is 18 px plus its description's lines of 14 px
  tall, with 4 px between rows). A click on the lamp or anywhere on the row switches the
  setting at once (like the original options, there is no confirmation).
- *Back* (653) at (336, 340), `Esc`, closes the dialog.

| Setting | Section | `openday.cfg` key | Default | Effect |
|---|---|---|---|---|
| Edge scrolling | Camera | `edge_scroll` | **off** | tactical-interface.md 3.6 |
| Smooth edge scrolling | Camera | `smooth_edge_scroll` | on | tactical-interface.md 3.6 |
| Drag the map with the right button | Camera | `drag_scroll` | on | tactical-interface.md 3.6 |
| Keep the view when selecting | Camera | `keep_view_on_select` | on | tactical-interface.md 3.2 (click on an own unit) |
| Exact minimap | Camera | `exact_minimap` | on | tactical-interface.md 2.6 (click and viewport frame) |
| Drag on the minimap | Camera | `minimap_drag` | on | tactical-interface.md 2.6 (dragging with the left button) |
| Full screen | Display | `fullscreen` | off | the window covers the screen (below); F11 switches it anywhere in the game |
| Resizable window | Display | `resizable_window` | on | the map screens are laid out for the window's size (below) |
| Sharp text | Display | `sharp_text` | on | the text is drawn anti-aliased at the window's resolution (below) |
| HD interface | Display | `hd_interface` | on | the interface art is drawn again from high-resolution versions (below) |

#### Full screen

The window covers the whole screen in the desktop's own mode (borderless,
no change of the display mode); the picture is laid out and scaled for it
exactly as for a window of that size. **F11** switches between full screen
and the window anywhere in the game (the key is not passed to the screens),
and so does the *Full screen* row of the OpenDay options. Switching by the
system's own controls (the green button on macOS) also updates the setting.
The state is kept in `openday.cfg` and applied at the next start; the
window's size and place before full screen are restored when leaving it.
Not in the scripted test mode.

#### Resizable window

The original draws every screen at 640×480 or 1024×768. OpenDay's window
can always be resized; with this option off, the picture keeps its size and
is scaled to fit the window (letter-boxed, as before). With the option on:

- **Screens that follow the window:** the battle views, the city screen,
  the continent screen, the map screens over the battle and the city, and
  the OpenDay options over a battle. Every other screen (front-end menus,
  videos, loading pictures, army screen, report, message boxes, the in-game
  options of the strategic screens) keeps its size and is scaled to fit the
  window as before.
- **Size of the picture:** the standard size `W0 × H0` is the resolution's
  (640×480 or 1024×768). With the window's drawing area `w × h` in pixels,
  the scale is `k = min(w / W0, h / H0)` (not rounded), so the interface
  takes the same share of the window as at the standard size. The side
  that limits `k` keeps its standard length, the other gets the window's
  shape: `W0 × max(H0, round(h × W0 / w))` when `w × H0 ≤ h × W0`,
  otherwise `max(W0, round(w × H0 / h)) × H0`. The picture is scaled by
  `k` (as with the option off; see *Scaling*) and fills the window, up to
  a rounding pixel. A wider window shows more of the map sideways, a
  taller one more downwards; a window of the standard shape shows the
  standard layout, only larger or smaller.
- **Layout of the map screens:** the side panel (136 px wide with the
  640×480 art, 184 px with the 1024×768 art) stays at the right edge and
  the message strip (32 / 40 px high) under the view, the view takes the
  rest: `view_w = W − panel`, `view_h = H − strip`. At the standard sizes
  this is the original's layout (tactical-interface.md 1.1). All panel
  controls keep their offsets from the panel's top-left corner, the strip's
  from its right end, as at 1024×768.
- **Panel below its art:** where the screen is taller than the panel art,
  the rows 480–767 of the panel's 1024×768 art (its filler, the left
  columns as wide as the panel) are repeated downwards, under the small
  art too; without the 1024×768 atlas the rest is black.
- **Strip wider than its art:** the left part up to 140 px before the art's
  right end is drawn at the left, the last 140 px at the strip's right end,
  and the 60 px left of that piece are repeated in between (they lie under
  the message box at both sizes).
- **Contents of the view:** the isometric map shows more tiles; the colony
  map of the continent screen and the overview picture of the map screens
  keep their size and stay centred in the view (the map screen's square is
  `min(view_w, view_h)`, the picture as at the standard size). The
  minimap's viewport frame covers `max(view_w / 54, view_h / 28)` tiles.
- **Dialogs:** the objectives dialog keeps its place relative to the
  middle of the view (at (40, 40) of the standard view); the in-game
  options, its yes / no boxes and the OpenDay options are centred on the
  view as before. The encyclopedia, a dialog that leaves the screen below
  visible but does not follow the window, is drawn at its standard size in
  the middle of the picture, the input shifted to match.
- **On resizing:** the view keeps its centre (map screens) and the controls
  are placed again; a drag in progress ends. Switching the option off or on
  takes effect at once.
- **While the border is dragged:** the picture follows the window at once,
  not only when the drag ends. Windows and macOS hold the program in the
  system's own resize loop for the whole drag; the engine draws (and runs)
  a frame from there each time the system reports the window to be redrawn
  during a live resize (SDL: `SDL_EVENT_WINDOW_EXPOSED` with `data1 = 1`,
  caught by an event watch). Input that arrives during the drag is mapped
  with the window's size at that moment.

#### Sharp text

The picture is drawn at 640×480, 1024×768 or the resizable size and scaled
to the window by a factor that is rarely a whole number; the nearest-pixel
scaling then drops or doubles rows and columns, and the thin, unsmoothed
strokes of the text (section 1) break up. With *Sharp text* on, the text is
drawn a second time at the window's resolution, over the scaled picture.
Captions that are part of the art (button labels, titles in the atlas)
remain pixel art.

- **Recording:** while a frame is drawn, every line of text drawn into the
  frame (or into a dialog's part of it) is noted: font, cell height, the
  cell's top-left, the colour index, the width of the pixel line, and the
  pixels of a rectangle around it (the glyphs' extent plus 2 px) as they
  were before and after it was drawn. The frame itself is not changed:
  screenshots of the debug server and of the test mode show the pixel
  text.
- **Visibility:** after the frame is finished, the texts are looked at from
  the last drawn to the first. A pixel of a text's rectangle *matches* if
  the frame still holds the value it had after the text was drawn. A
  letter pixel (one the text changed) that does not match was covered by
  something drawn later, and every pixel within 2 px of it counts as
  covered too (the cover may have the background's colour). A pixel that
  matches and is not covered is *visible*; in the copy of the frame that is
  shown, it gets back the value it had before the text was drawn. Going
  from the last text to the first, a text drawn over another one (a
  shadow) is taken out before the one under it is checked.
- **Drawing:** for each text with a visible letter pixel, the same font is
  rasterized with anti-aliasing at `cell height × k` pixels (`k`: window
  pixels per picture pixel), with the same ascent-based baseline. The
  advances are spread evenly so that the line is `k` times as wide as the
  pixel line (alignment and wrapping stay as laid out). The glyph pixels
  are blended in the text's palette colour, only over window pixels whose
  picture pixel is visible (a cursor or a dialog over part of a text hides
  that part).
- Switching the option takes effect with the next frame.

#### Scaling

The picture is scaled to the window "sharp bilinear" (SDL's pixel-art
scale mode), not by nearest pixel (which at a factor `k` that is not a
whole number makes some picture pixels one window pixel wider or taller
than the others, and below 1 drops some): every picture pixel covers a box
of `k × k` window pixels; a window pixel inside one box takes that pixel's
colour, one on the border between boxes blends the neighbours by how much
of it each covers (the blend is one window pixel wide). No row or column is
lost or doubled, and at a whole `k` the result is the same as nearest
pixel. Below `k` = 1 every picture pixel is blended with its neighbours
(close to bilinear filtering). Renderers without the mode (software) fall
back to nearest pixel. The sharp text and the HD interface are drawn over
the result; the screenshots of the debug server and of the test mode are
not scaled. *(Earlier versions had options for nearest-pixel and
whole-number-only scaling; the HD interface made them unnecessary, and the
keys `smooth_scaling` and `pixel_perfect` of `openday.cfg` are ignored.)*

#### HD interface

The interface art (the elements of the interface atlas, docs/formats/INTERFACE-SMK.md:
panels, buttons, frames, the message strip) can be drawn a second time,
over the scaled picture, from versions with more pixels. They come from the
user's own *pack* or, where the pack has none, are enlarged by the engine.
The pack is made by the user from their own installation, on their own
computer; no enlarged art ships with OpenDay.

- **Recording:** while a frame is drawn, every draw of an atlas element
  into the frame (or into a dialog's part of it) is noted: the element
  (its frame number, and whether of the 640×480 or the 1024×768 atlas),
  the part of it drawn (a piece of a repeated filler or strip), and where
  that part landed, clipped to the surface. Draws with a draw mode other
  than the plain one are not noted. The frame is not changed.
- **Visibility:** after the frame is finished (and, with *Sharp text* on,
  after the sharp texts were taken out of the copy that is shown, so that
  the art under them counts), a pixel of a recorded part is *visible* if
  the frame holds the element's pixel there. Runs of visible pixels along
  each row are joined, rows with the same runs into one rectangle.
- **Drawing:** for each recorded part, the high-resolution version is
  drawn over its visible rectangles, the rectangle `(x, y, w, h)` of the
  picture mapped to window pixels by the picture's place and scale `k`,
  and to the same rectangle of the element in the version's texture
  coordinates (the version may have any size; it is filtered linearly).
  The sharp text is drawn over it. Nothing is drawn at `k` ≤ 1.01: the
  original pixels are as sharp as it gets.
- **Enlarging:** an element without a pack picture is enlarged with xBRZ
  (Zenju's "scale by rules" filter, GPL-3.0) from its colours in the game
  palette (or the screen's own palette), by `n = clamp(⌈k⌉, 2, m)` with
  `m = clamp(⌊4096 / longer side⌋, 2, 6)`; the window's linear filtering
  covers the rest. It runs in the background (at most four elements at a
  time, the rows of one element shared by the processor's threads); until
  it is ready the original pixels show. When `k` changes so that `n`
  changes, the element is enlarged again.
- **Pack:** PNG files (any colour type; alpha is used) in
  `<per-user dir>/hd-interface/640/NNNN.png` and `.../1024/NNNN.png`, where
  `NNNN` is the atlas frame number with four digits. The picture covers the
  whole element at any size, preferably a whole multiple of it. Elements
  without a file are enlarged as above. The folder is looked at once per
  element and run.
- **Export:** `openday --export-interface [DIR]` writes every element of
  both atlases (the frames the engine uses), in the game palette, with the
  pack's names into `DIR` (default `<per-user dir>/interface-export`): the
  starting point for enlarging them with any tool and copying the results
  into the pack folder.
- **Making a pack with Real-ESRGAN:** `openday --make-hd-interface
  [UPSCALER] [--upscale-model NAME]` makes the pack with the user's own
  `realesrgan-ncnn-vulkan` (Real-ESRGAN's portable Vulkan build, not shipped
  with OpenDay). `UPSCALER` is the program or its folder; without it the one
  used last (`upscaler` in `openday.cfg`), then the `PATH`, then
  `<per-user dir>/realesrgan` are tried. Steps:
  1. the elements are exported as above into `<per-user dir>/hd-interface.work/in`;
  2. the program enlarges each resolution's folder 4× (`-s 4 -f png`, the
     model given; default `realesr-animevideov3`: fast and keeps the most
     of the original texture; `realesrgan-x4plus-anime` gives cleaner, flatter
     surfaces, `realesrgan-x4plus` is much slower), its output hidden;
  3. a result is used only if it is a whole multiple (≥ 2×) of the element
     and not empty (every pixel black or transparent) where the element is
     not; a GPU short of memory gives such empty pictures. The failed ones
     are enlarged once more in 32-pixel tiles (`-t 32`);
  4. the usable results replace the pictures of the same name in the pack
     folder; the rest are left to xBRZ. The work folder is removed.

  The models are taken from the `models` folder next to the program
  (`-m`), whatever the working folder. On Windows the program runs without
  a console window and at below-normal priority (idle priority starves it
  while the game runs: the GPU work is fed by its CPU threads).
- **Automatic pack:** when the game starts (not in the test mode) with
  the option on and `hd_pack` missing from `openday.cfg`, the pack is made
  in the background while the game runs, xBRZ showing until then:
  1. a pack folder already holding PNG files counts as made (`hd_pack=1`);
  2. the upscaler is looked for as above; if there is none, Real-ESRGAN
     v0.2.5.0's portable build for the platform is downloaded from that
     project's GitHub release (`realesrgan-ncnn-vulkan-20220424-windows|macos|ubuntu.zip`,
     BSD-3-Clause; size and SHA-256 pinned in `app/hd_pack_auto.cpp`), and
     only the program, `vcomp140.dll` and the `realesr-animevideov3` model
     files are unpacked into `<per-user dir>/realesrgan.partial`, made
     executable outside Windows, and renamed to `realesrgan`. Fetched by the
     engine's own HTTP client, the macOS program carries no quarantine mark.
     The macOS build holds x86_64 and arm64 code; for Linux there is an
     x86_64 build only (elsewhere nothing is downloaded);
  3. the pack is made as in *Making a pack* with `realesr-animevideov3`;
  4. on success the pack's pictures replace the enlarged ones at once (the
     versions are forgotten and the folder looked at again), `hd_pack=1` and
     the program (`upscaler`) are stored; if the upscaler made nothing
     usable (no Vulkan device, say) `hd_pack=2`, and it is not tried again.
     A failed download, or closing the game first (the upscaler is stopped,
     the work folder removed), stores nothing: the next start tries again.
     Deleting `hd_pack` from `openday.cfg` makes the pack again.
- **Memory:** enlarged versions are kept up to 64 M texels (256 MB); above
  that, the ones used longest ago and not in the current frame are dropped.
- Screenshots of the debug server and of the test mode show the original
  art. Switching the option takes effect with the next frame.

### Setup window *(OpenDay addition)*

What OpenDay shows before the game runs, when it finds no game files or has
to copy or convert them (`app/setup_ui.*`, `app/setup.cpp`). The game's art
is not there yet, so the look of its menus is drawn by OpenDay itself, with
its own palette: one 640×480 screen in the program window, scaled and
centred like the game screens (sharp bilinear; the text drawn smooth when
*Sharp text* is on). The pages change in that window, messages open as
dialogs over it, and once the data is ready the same window goes on as the
game's (the main menu appears in it).

- **Look:** black screen, the line "OpenDay <version> - not affiliated with
  the original publisher" at (4, 462) in grey as on the main menu. Panels:
  a 1 px near-black line, a 2 px raised edge (light gold top and left, dark
  brown bottom and right), filled with a camouflage of green, olive, brown
  and khaki blotches with a fine grain (made from value noise, fixed seed).
  A title strip 24 px high, 7 px inside the edge: sunk, with a small raised
  bar at each end and the title in bold capitals, 16 px, 2 px letter
  spacing, gold with a dark shadow. Texts stand in sunk black wells.
  Buttons: olive face, raised 1 px edge, bold capitals 13 px with 1 px
  spacing; lighter face and gold letters under the pointer, sunk edge and
  the caption 1 px lower while held, the action on release over the same
  button.
- **Dialogs:** a panel 440 px wide in the middle of the screen, the title
  in its strip, the message in a well (long paths broken after a `\` or
  `/`), the buttons (120×24) at the bottom right; the screen below is
  shaded (every other pixel black, as in the game). Enter presses the first
  button, Escape (or closing the window) the last.
- **First page** ("OPENDAY", panel 440×408 at (100, 34)): a well with the
  explanation, the buttons *Game folder...*, *Installer or CD image...*,
  *Download from GOG...* and, set apart, *Quit* (256×28, centred), and a
  well below them telling what the button under the pointer does. The
  system's folder or file dialog opens over the window (shaded behind it).
  A folder without the game, or an installer that cannot be used, is told
  in a dialog and the page stays. Escape or closing the window quits
  without a further message.
- **Download from GOG** (same panel, [gog-download.md](../gog-download.md)):
  the sign-in panel opens over the first page, which stays unchanged under
  it (not shaded; its buttons take no pointer); in the web-browser mode the
  page explains the steps and has *Paste address*, *Open GOG again*, *GOG
  store page* and *Cancel*; while downloading a well with the phase and the
  megabytes, a bar and *Cancel*. A failure is a dialog with *Try again* and
  *Close*.
- **Copying / converting** (panel 440×164 at (100, 158), "COPYING THE GAME
  FILES" / "PREPARING THE GAME DATA"): a well with what happens and the
  current file, and a bar. Closing the window does not stop the work.
- An error before the game starts is a dialog in this window (opened for
  it when needed); a message box only when no window can be made.

### Save and load dialogs (`CInterfSave` / `CInterfLoad`)

Both use the same geometry (details of the slot folders in
[savegame.md](savegame.md)):

- Panel 286×419 (save: frame **750**, load: frame **739**), centred: screen
  (177, 30)–(463, 449) at 640×480.
- Ten slot rows, row i = 0…9 (slot i + 1): x 53…266, y = 42 + 32·i, height
  17 (panel coordinates). Standard font; the selected row is drawn black on
  white, the others white on black.
- Slot names are read when the dialog opens: the name of slot N is the file
  title (name without extension) of the `*.dsc` file in the slot folder
  `savegame.NN`; no such file → empty name.
- Buttons: *Back* (55, 370) 100×24, Esc (save: frames 751, load: 740);
  *Save* (155, 370) 100×24, frames 755, Enter / *Load* (155, 370) 100×24,
  frames 744, Enter.

Save dialog:

- every slot can be chosen, empty or not; slot 1 is selected initially;
- an edit box (max **31** characters) lies over the selected row, pre-filled
  with that slot's current name; clicking another row moves the edit box
  there and loads that row's name;
- *Save*: the text is trimmed of leading and trailing blanks; if it is then
  empty **nothing happens**; otherwise the save request above is made.

Load dialog (front end, *Load campaign*, states 4 and 10):

- only rows with a **non-empty** name can be selected; initially nothing is
  selected;
- *Load* with a selected slot: the session becomes "loaded campaign"
  (game type 1, the slot number, game name = slot name, at most 71
  characters; session info string 746 "Loaded campaign") and the front end
  proceeds to loading picture 3;
- there is no load from inside a running game.

## 5. Sounds and music

`SOUNDS\` effects are loaded at start-up from a fixed table and looked up by
an upper-case **name**:

| Name | File | Name | File |
|---|---|---|---|
| CHAT | `chat.wav` | OVER | `over.wav` (**missing** from the GOG install; silently skipped) |
| TIMER | `timer.wav` | PROVINCE | `province.wav` |
| SELECT | `select.wav` | RECYCLE | `recycle.wav` |
| BUILD | `build.wav` | SELPROV | `selprov.wav` |
| CASHIN | `cashin.wav` | TWOTURNS | `twoturns.wav` |
| CASHOUT | `cashout.wav` | DEPLOY | `deploy.wav` |
| CLICK | `click.wav` | RECALL | `recall.wav` |
| INVALID | `invalide.wav` | REINF | `reinf.wav` |
| OBJECTIVE | `objectif.wav` | TECH | `tech.wav` |

A missing file is skipped. In the code examined, `CLICK` is played by one
strategic-screen action only, not by menu buttons. `INVALID` accompanies the
error messages of building, deployment, movement and shooting; `CHAT` an
incoming chat line; `TWOTURNS`, `OBJECTIVE`, `TECH`, `DEPLOY`, `RECALL`,
`SELECT`, `SELPROV`, `PROVINCE`, `BUILD`, `CASHIN`, `CASHOUT` their game
events.

**Music** is Red Book CD audio driven through Miles (the GOG version replaces
it with `EXE\MUSIC\TrackNN.ogg`). Logical track *n* (1-based, as in
`[CD Audio] trackn`) is CD track *n + 1* (track 1 is the data track), i.e.
`Track(n+1).ogg`. The player supports "play one track once", "repeat one
track" and "play all from a track" modes. When a game is started the program
picks a **random** logical track between 2 and min(N, 12) and plays it;
afterwards the player can choose tracks in the options dialog. No music
start was found for the front-end menus (the intro video carries its own
sound).

## 6. Cursor

The mouse pointer is a **Windows hardware cursor**, not a sprite drawn by the
game. Each interface supplies a cursor handle; the main window sets it on
every `WM_SETCURSOR`, using the cursor of the top-most interface under the
mouse.

| Cursor | Source | Size | Hot spot | Used |
|---|---|---|---|---|
| arrow | language DLL cursor resource **104** | 32×32, monochrome | (1, 1) | default for every interface |
| hour-glass | language DLL cursor resource **105** | 32×32, monochrome | (15, 15) | loading screens (`CWaitInterface`) and the tactical waiting view |
| system wait cursor | Windows `IDC_WAIT` | — | — | wait interfaces created without an explicit cursor |

Both custom cursors are white with a black outline (the arrow is a classic
diagonal pointer, about 11×17 px of visible pixels). OpenDay should draw its
own equivalents (e.g. SDL system cursors or self-made sprites); game-specific
targeting cursors on the maps come from `SYMBOLS.DBI` (`data-model.md`).

## Implementation notes (OpenDay)

Implemented in `engine/render/font.*` (text), `engine/formats/pe_resources.*`
(string table), `engine/ui/` (atlas, buttons, toggles, radio groups, list and
text boxes) and the game program `app/` (screens).

- Text uses **Liberation Sans** (SIL Open Font License), embedded into the
  program, rasterized with stb_truetype at cell heights 14 and 17 without
  anti-aliasing (a pixel is set when the glyph covers half of it).
- A checked toggle shows its `+4` frame also while hovered, because many
  toggle sets have no `+5` frame (open question above).
- Main menu: *Multi player* is shown disabled. *New campaign* and *Load
  campaign* are enabled when the four colony databases are present. The
  resolution choice applies to the game screens (battle and campaign).
- Load / save: panels 739 / 750 with ten slots (black boxes at x 52, y 40 +
  32 × slot, 217 × 21); BACK at (10, 383) and LOAD / SAVE at (176, 383) *(OpenDay
  positions)*. OpenDay saves in its own format (campaign.md 8).
- The program window is scaled, or the map screens are laid out for it
  (resizable window, above); the mouse pointer is the system arrow.
- The music starts (random track, as described) when a battle screen opens.
- The encyclopedia is implemented as specified in
  [encyclopedia.md](encyclopedia.md) (not reachable from the main menu, as
  in the original).
- Dialogs that leave the screen below visible (the encyclopedia, the map
  screens) are overlay screens: the screen below is drawn first.
- In-game options of the battle views: Save / Surrender / Quit are enabled
  by the table above (Save also needs a campaign battle). *Surrender* asks
  with string 818 and sends the cheat-lose request; *Quit* asks with string
  819 and returns to the main menu *(OpenDay choice)*. *Save* opens the save
  dialog and saves the battle in progress (savegame.md, "Implementation
  notes"); it stays disabled after the battle has ended.
- Save dialog: clicking a slot puts its name (or "<player> - <colony>" for
  an empty slot) into an edit box of up to 31 characters over the slot;
  *Save* trims it (empty: nothing), shows string 816 while writing and
  string 817 under the panel on failure. The load dialog has an extra
  *Original game saves* toggle above the panel when the game folder has
  original save slots (import, savegame.md).
- Not implemented yet: CD-player track list, credits, the multiplayer
  menus.

## 7. Open questions

- Toggle sets with only five frames: when a checked item is hovered the code
  would show the `+5` frame, which is the first frame of the next control
  (e.g. *3 min* checked + hover → a *9 min* image). Either the radio group
  suppresses hover on the checked item, or this is a visible quirk of the
  original. Needs a runtime check.
- Exact rectangle of the in-game dialogs (options etc.): taken from a
  run-time rectangle, presumably centred like the menus.
- Which view uses side panels 0, 41, 100 and 507 is inferred, not proven.
- Whether the intro video can be skipped by key/mouse, and how 640×400
  videos are placed in 640×480 (probably centred with black bars).
- Menu click sound: none found in the button code; verify at run time.
- Slider of the new-campaign screen (frame 1047) and the side-selection
  frames 979–988 were not traced in detail.
- The small 12 px font is created but apparently unused.
- In-game *Quit*: whether the application command exits the program or
  returns to the front end.
- Whether the options dialog stays open after a successful save (OpenDay:
  it does).
- The hot-key letters 100–121, 124, 125 are not yet matched to their buttons
  (most belong to the strategic and tactical HUDs).
