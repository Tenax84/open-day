# Interface graphics: `INTERF\*.SMK`

The original game keeps almost all of its user-interface art (panels,
buttons, toggles, side bars, loading screens) in a few Smacker 2 files under
`INTERF\`. They are not played as videos: each frame is a separate still
image, and the program asks for a frame by its **index**. This document
describes those files and how the program uses them. Behaviour of the screens
built from them (layouts, flow, fonts, cursor) is in
[`../mechanics/interface.md`](../mechanics/interface.md); the tactical HUD,
the deployment screen and the briefing/aftermath screens are covered by
[`../mechanics/tactical-interface.md`](../mechanics/tactical-interface.md).

The container itself is ordinary Smacker 2 (see the decoder notes in
[`FF.md`](FF.md)); nothing here is specific to the codec.

## Files

| File | Size | Frames | Frame time | Use |
|---|---|---|---|---|
| `640X480.SMK` | 640×480 | 1048 | 100 ms (unused) | UI element atlas for the 640×480 mode |
| `1024X768.SMK` | 1024×768 | 1048 | 100 ms (unused) | UI element atlas for the 1024×768 mode |
| `WAITING.SMK` | 640×480 | 14 | 66.66 ms (unused) | full-screen "loading…" pictures |
| `TICK.SMK` | 216×224 | 9 | 66.66 ms | small burst animation, loaded by the animation engine (see below) |

None of the four files carries audio. All of them use the game palette
(`INTERF\PAL.BMP`): the embedded Smacker palette equals it up to the usual
6-bit→8-bit rounding (max. difference 3 per channel; `TICK.SMK` differs only
in three unused entries). The program in fact ignores the Smacker palette and
uses the application palette when converting the frames.

## Selecting the atlas

The application keeps a two-entry table `{ "640x480.smk", "1024x768.smk" }`
and opens the entry indexed by the current resolution (`0` = 640×480,
`1` = 1024×768, the `Resolution` key of `[User]` in `LIBDAY.INI`). The file is
re-opened whenever the resolution changes (it is closed and opened again when
a game starts after the menus). Failure to open it shows string 281
(interface file missing).

**Both atlases use the same frame indices for the same element.** Frames that
do not depend on the screen size (menus, dialog panels, buttons) are pixel
identical in the two files (checked on panels 157, 382, 648, 710, 1013 and on
buttons). Only the elements that span the screen differ in size, e.g. the
right-hand side panels (136×480 in the small atlas, 184×768 in the large one)
and the message strip at frame 498 (504×32 vs. 840×40). The menus are always
shown at 640×480 (see `interface.md`), so they always come from the content
that is identical in both files.

## Frame layout and addressing

Each atlas frame holds **one element anchored at the top-left corner (0,0)**.
The program never searches the frame for the element: every caller supplies
the frame index **and the element size (w, h)**, and the program copies the
rectangle `(0,0)-(w,h)` of that frame into an off-screen surface of exactly
that size.

The rest of the frame is filler and must be ignored:

- In most frames the area outside the element is palette index **253**
  (magenta, 255/0/255); in the full-size menu frames it is index **249**
  (red, 255/0/0).
- Because the files are delta-coded, many frames still contain stale pixels
  of earlier frames outside the element rectangle (in the 1024×768 atlas most
  small buttons are followed by the remains of the previous side panel).
  Bounding-box detection on the filler colours therefore **over-estimates**
  some sizes. The authoritative size is the one given by the code; the sizes
  listed below come from the code where a caller was found, otherwise from
  the bounding box (marked "≈").

To read frame *n* the program seeks the Smacker stream to that frame and
decodes it; a reimplementation can simply decode the whole file once and keep
the frames (the 640×480 atlas is ~320 MB of 8-bit pixels when fully
expanded, so cropping each frame to its element on load is advisable, but the
crop size must come from the caller, not from the filler colours).

**No colour key.** UI elements are copied with a plain opaque blit. Indices
249/253 that happen to lie inside an element are drawn as they are.

### Multi-state controls

Controls are built from **consecutive frames**:

| Control | Frames used | Meaning of `first + k` |
|---|---|---|
| push button | 4 | `+0` normal, `+1` hover (lit), `+2` pressed, `+3` disabled (dark) |
| toggle / radio item | 6 requested | `+0..+3` as for a push button while unchecked; when checked, `+4` replaces the normal image and `+5` replaces the hover image (pressed and disabled images are unchanged) |

Most toggle sets in the atlas have only **5** frames (`+0..+4`); the frame at
`+5` is then the first frame of the next control. See the open questions in
`interface.md`.

## Frame catalogue (`640X480.SMK` / `1024X768.SMK`)

Sizes are w×h in the 640×480 atlas; a second size is given where the
1024×768 atlas differs. "Users" are RTTI class names of the screens that
request the frame; "likely" marks an attribution derived from code proximity
rather than a direct reference. Empty frames (pure filler) are listed as
gaps.

### Game-screen side panels and their HUD pieces (0–356)

Each block starts with a right-hand side panel and is followed by the small
controls of the same screen (round 44×43 buttons, 66×9 tabs, 53×17 and
110×17 buttons, 19×19 / 19×17 icon buttons, all in groups of four states).

| Panel frame | Size | User |
|---|---|---|
| 0 | 136×480 / 184×768 | the deployment and reinforcement views (`CFirstDeployView` / `CDeployView`, see `tactical-interface.md` 1.2) |
| 41 | 136×480 / 184×768 | the overview map of a battle (`CMapScreenTactic`, strategic-interface.md 4) |
| 100 | 136×480 / 184×768 | the city view (`CStratView`, city-building.md 9) |
| 192 | 136×480 / 184×768 | the continent screen (`CColonyBaseView`, strategic-interface.md 1) |
| 226 | 136×480 / 184×768 | tactical view (`CTacticView`; has *Overwatch* / *Auto fire*) |
| 310 | 136×480 / 184×768 | tactical waiting/deployment view (`CTacticWaitView`) |
| 507 | 136×480 / 184×768 | a tactical-family view (likely `CAutomatView`) |
| 578 | 136×480 / 184×768 | the city overview map (`CMapScreenStrat`) |

Gaps (empty): 33–40, 92–99, 153–156, 166–167, 197, 206–209, 218–225,
239–240, 257, 302–309, 357–364. The tactical pieces are detailed in
`tactical-interface.md`.

### Dialog panels (157–647)

| Frame | Size | User | Notes |
|---|---|---|---|
| 157 | 588×451 | `CArmyInterf` | army screen; 158–165, 168–171 110×17 buttons; 172–175 60×55; 176–191 19×17 |
| 365 | 448×480 | `CAftermathInterface` | 366–369 110×17 button |
| 382 | 540×415 | `CEncyclopInterface` | encyclopedia; 383–386 53×17; 387–394 19×17; 395–432 19×19 |
| 433 | 472×344 | `CChatInterface` | chat window |
| 453–468 | 53×17 | | four small buttons |
| 477 | 254×144 | `CWaitBox` and the message box | 478–481 *YES*, 482–485 *NO*, 486–489 *OK* (53×17) |
| 498 | 504×32 / 840×40 | `CFH2MsgArea` | message strip; 499–502 53×17 |
| 557 | 513×311 | `CIntelStratInterface` | strategic intelligence; 558–561 110×17 |
| 633 | ≈449 wide | `CIntelInterface` | tactical intelligence / objectives |
| 634–647 | | | 110×17 and 19×19 controls of the above |

### In-game options dialog (648–709)

| Frames | Size | Element |
|---|---|---|
| 648 | 403×371 | options panel (`CMenuInterface`): Sounds, Move animations, Scroll speed, CD player |
| 649–652 | 53×17 | *Quit* button |
| 653–656 | 53×17 | *Back* button |
| 657–660 | 53×17 | *Load* button (not used by the options dialog) |
| 661–664 | 53×17 | *Save* button |
| 665–670 | 19×19 | on/off toggle (red = unchecked, green = checked) |
| 671–674 | 19×17 | CD *Play* |
| 677–680 | 19×17 | CD *Stop* |
| 681–684 | 19×17 | list *next* (▶▶\|) |
| 685–688 | 19×17 | list *previous* (\|◀◀) |
| 689–692 | 110×17 | *Restart scenario* |
| 693–696, 700–703 | 53×17 | small buttons (not attributed) |

### Front-end menus (710–1047)

All menu panels and buttons are identical in both atlases.

| Frames | Size | Element | Used by |
|---|---|---|---|
| 710 | 286×281 | *Main menu* panel | `CPrincipalMenu` |
| 711 / 715 / 719 / 723 / 727 / 731 / 735 (+3 each) | 256×28 | *New campaign*, *Load campaign*, *Multi player*, *Intro*, *Credits*, *Resolution*, *Quit* | main menu (727 *Credits* is not used); 711/715 also by `CMenuGameType` |
| 739 | 286×419 | *Load game* panel (10 numbered slots) | `CMenuLoad` / `CInterfLoad` |
| 740 / 744 | 100×24 | *Back*, *Load* | load dialog |
| 750 | 286×419 | *Save game* panel | `CInterfSave` |
| 751 / 755 | 100×24 | *Back*, *Save* | save dialog; 755 also the confirm button of the resolution menu |
| 760 | 286×236 | game-type panel (multiplayer) | `CMenuGameType` |
| 761 / 765 / 769 / 773 | 256×28 | *Campaign* (unused), *Skirmish*, *Death match*, *Back* | game type; 765 also the main menu's *Skirmish* |
| 777 | 286×228 | *Resolution* panel | `CMenuGameResolution` |
| 778–782 / 783–787 | 256×28 | *Low (640×480)* / *High (1024×768)* radio items (5 frames each) | resolution |
| 788 | 367×188 | *Campaign difficulty* panel (Easy/Hard) | not attributed (see 1045) |
| 799 / 803 | 100×24 | *Back*, *Continue* | 799 = resolution *Back* |
| 813 | 286×228 | *Dial-up* panel (phone number) | modem connection |
| 814 / 818 | 100×24 | *Continue*, *Back* | |
| 823 | 286×228 | network service selection panel | `CMenuServices` |
| 824 / 828 | 100×24 | *Continue*, *Back* | services |
| 832–839 | 53×17 | two small buttons | |
| 841 | 364×416 | *Skirmish* panel (multiplayer) | `CMenuSkirmishMulti` |
| 842 / 846 | 100×24 | *Continue*, *Back* | skirmish |
| 850–857 | 53×17 | two small buttons | |
| 858–862 / 863–867 / 868–872 | 60×24 | turn time *3 min* / *9 min* / *6 min* radio items | skirmish, death match |
| 873–877 | 60×24 | a fourth *3 min* set (unused?) | |
| 879 | 364×416 | *Deathmatch* panel | `CMenuDeath` |
| 880 / 884 | 100×24 | *Continue*, *Back* | |
| 888–895, 896–915 | 53×17, 60×24 | small buttons, turn-time radio sets | death match |
| 917 | 364×322 | session list panel | `CMenuSessions` |
| 918 / 922–929 / 930 / 934 | 100×24, 53×17 | *Back*, small buttons, *Join*, *Host* | sessions |
| 939 | 286×228 | *New game* panel (player name, difficulty Easy/Hard) | `CMenuNewCampaignSingle` |
| 940 / 944 | 100×24 | *Back*, *Continue* | new campaign single and multi |
| 970 | 286×228 | player set-up panel | `CMenuPlayerMulti` |
| 971 / 975 | 100×24 | *Back*, *Continue* | |
| 979–988 | 100×24 | *Aliens* / *Humans* side selection | |
| 989–1012 | 19×19 | four 6-frame colour toggles | player colour |
| 1013 | 560×469 | *Waiting area* (lobby: players, game information, chat) | `CMenuLobby` |
| 1014 / 1018 / 1022 | 100×24 | *Start*, *Send*, *Back* | lobby |
| 1026–1033, 1035–1038, 1040–1043 | | small buttons, 110×17 button, *Boot* | lobby |
| 1045 | 367×188 | campaign difficulty panel | `CMenuNewCampaignMulti` |
| 1046 | 364×416 | *Skirmish* panel (single player) | `CMenuSkirmishSingle` |
| 1047 | 208×17 | slider track | new-campaign difficulty slider |

Gaps (empty): 748–749, 759, 789–798, 807–812, 822, 840, 878, 916, 938,
948–969, 1034, 1039, 1044.

The button captions and panel titles are **drawn into the art** (in
English in the English release). Localised releases therefore ship their own
atlases; the language DLL contributes only the hot-key letters (see
`interface.md`).

## `WAITING.SMK`

Fourteen full-screen 640×480 pictures shown while the game loads or waits.
Each has a caption baked into the bottom-right corner. The program asks for a
**screen id 1…14** and shows frame `id − 1`:

| Id | Frame | Caption / situation |
|---|---|---|
| 1 | 0 | plain black |
| 2 | 1 | starting a new campaign |
| 3 | 2 | loading a campaign |
| 4 | 3 | loading a skirmish |
| 5 | 4 | loading a death match |
| 6 | 5 | alien counter-attack |
| 7 | 6 | aliens attacking the base camp |
| 8–10 | 7–9 | loading one of the three colonies (Anstasia, Quaban, Nu Babel) |
| 11 | 10 | preparing the city phase |
| 12 | 11 | loading a scenario |
| 13 | 12 | deployment phase |
| 14 | 13 | next player's turn |

When leaving the menus the game mode selects the id: new campaign → 2, load
campaign → 3, skirmish → 4, death match → 5. The picture is shown by a
full-screen wait interface (`CWaitInterface`) that centres the 640×480 image
on the screen (so it is letter-boxed in 1024×768 mode; the surround is index
0) and shows the hour-glass cursor (see `interface.md`).

## `TICK.SMK`

216×224, 9 frames, 66.66 ms per frame (15 fps, the same rate as the unit
animation timeline in [`../mechanics/unit-animation.md`](../mechanics/unit-animation.md)).
Frame 0 is empty; frames 1–8 show a small fire/smoke puff growing and fading
around the centre of the frame (roughly x 90–134, y 101–134). The background
is index 148 (grey), i.e. the same "top-left pixel is the colour key"
convention as the unit animations in `FF.md`.

The application opens it at start-up and hands it to the animation engine
(`CAnimEngineImpl`); a failure shows string 274 ("Can't initialize
smacker"). Where its frames are drawn is not yet known (see the open
questions in `interface.md`).

## Related files

- `IMGS\LISTBOX.DBI`: 158 opaque 38×38 icons named `<code>999`, the
  list/purchase icons of units, buildings, automated units, mines, roads and
  walls (list boxes in the army, deployment and encyclopedia screens). See
  [`DBI.md`](DBI.md) and `data-model.md`.
- `IMGS\SYMBOLS.DBI`: 138 transparent 54×56 map symbols; it also holds the
  per-unit targeting cursor sprites referred to by the `cursorID` column
  (`data-model.md`).
- `ENCYCL\ENCYCL.SMK`: 436×352, 126 frames, the encyclopedia pictures,
  addressed by the `EncyclopediaFrame` column of the type tables.
- `LIBDAY.EXE` itself holds twenty small 8-bit bitmap resources (ids 147,
  152, 181–185, 195, 213, 240–243, 251, 252, 305–309; sizes 8×20 … 148×12,
  two of them 54×56). They look like widget parts (scroll arrows, bars,
  highlight tiles); their individual uses have not been mapped.
