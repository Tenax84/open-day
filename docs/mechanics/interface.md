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
surface GDI draws plain, non-smoothed glyphs.

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
- The program window is scaled; the mouse pointer is the system arrow.
- The music starts (random track, as described) when a battle screen opens.
- The encyclopedia is implemented as specified in
  [encyclopedia.md](encyclopedia.md) (not reachable from the main menu, as
  in the original).
- Dialogs that leave the screen below visible (the encyclopedia, the map
  screens) are overlay screens: the screen below is drawn first.
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
- Whether the options dialog stays open after a successful save.
- The hot-key letters 100–121, 124, 125 are not yet matched to their buttons
  (most belong to the strategic and tactical HUDs).
