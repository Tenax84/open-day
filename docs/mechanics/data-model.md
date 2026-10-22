# Game data model (databases)

What the tables in the game's Jet databases mean, how they relate to each
other, and how they refer to sprites (`IMGS\*.DBI`) and animations
(`ANMSUNIT\ANMSUNIT.FF`). The container format and the reader are in
[`../formats/MDB.md`](../formats/MDB.md).

Everything here comes from the schema, from value ranges and from
cross-checks between tables and asset name lists. Interpretations that are
not yet confirmed by observed game behavior are marked *(?)*. Exact rule
values are not reproduced here. They are read from the user's own
installation at run time.

## Conventions used throughout

- **`Counter`**: AutoNumber primary key of most tables. It has no meaning
  for the game, and its values have gaps. Game code uses the explicit id
  columns (`typeID`, `TypeID`, `Enum`, `id`, `ID`, …).
- **Lookup tables** (`… Lookup`, `… TYPE ID LOOKUP`): `Enum` (integer) →
  symbolic name such as `racetype_human`, `movetype_grav`, `H_Factory`.
  These names look like constants in the original source code *(?)*.
  OpenDay should define its own enums from them.
- **"None" sentinels:** `65535` in id columns (no prerequisite, no
  transformation, no enemy type; also the `MAX` / `Max` end markers of the
  type-id lookups), `-1` in animation `…_Begin` columns (animation not
  present), `-1` in `EncyclopediaFrame` (no encyclopedia picture).
- **Languages:** every player-visible text exists three times, with the
  suffixes `English`, `German`, `French` (column names sometimes start with
  a lower-case letter, and the order of the three varies). The `LIBDAY.INI`
  `Language=` key selects the language. Text is Windows-1252.
- **`Encyclopedia*`**: encyclopedia text. **`EncyclopediaFrame`**: frame
  number of the matching picture, presumably in `ENCYCL\ENCYCL.SMK` *(?)*.
- **`Color_Red/Green/Blue`** on terrain, ruin, road, wall and mine types:
  one RGB color per tile type, most likely for the overview minimap *(?)*.
- **Coordinates:** `PosX`/`PosY` are tile coordinates on the square
  tactical map, `0 … size-1`. Map sizes are 24, 48, 72 or 96 tiles.
- **Race / side** (`Race`, `Race Lookup`): `0` human, `1` Tauran,
  `2` "basic" = Shubblar, `3` "mental" = Shadamar. The 4-letter graphic
  codes start with the matching letter `H`, `T`, `B`, `M` (see *Asset
  naming*).
- **Player** (`playerOwner`, `player`, `Players Lookup`): `0` = the AI,
  `1`–`4` = human players (hot-seat/network slots).

## GLOBAL.MDB — static rules

### Lookup tables

| Table | Meaning |
|---|---|
| `Race Lookup` | Race enum (see above). Has `RaceName` (symbol) and display-name columns. |
| `Players Lookup` | Player slot enum (AI, 1–4). |
| `Color Player Lookup` | Color-variant enum: AI, players 1–4, neutral. |
| `UNIT TYPE ID LOOKUP` | Unit type id → symbol (`H_Trooper`, `T_ShockTrooper`, `B_Footman`, …) + `Race`. Symbol prefix = race letter. |
| `BUILDING TYPE ID LOOKUP` | Building type id → symbol (`H_Factory`, `H_Tower_Laser`, …). |
| `AUTOMATED TYPE ID LOOKUP` | Automated-unit type id → symbol (`H_Heli_Transport`, `H_Bomber_Gas`, …) + `Race`. |
| `Unit Category Lookup` | Unit category: grav, ground, water. |
| `Unit Movement Lookup` | Movement class: none (ground), ground, trooper, grav, boat, air, underwater, teleport, none (water), grav-fly. |
| `Weapon Type Lookup` | Direct or indirect fire. |
| `DamageType Lookup` | Damage reduces armor or action points. |
| `TerrainType Lookup` | Terrain class: shallow water, deep water, riverside, plain, mountains, forest, crack. |
| `Building Category Lookup` | Standard, ruin, tech (+ `MAX` marker). |
| `Automated Category Lookup` | What an automated unit does: destroy, repair, build, mine, drop, transport. |
| `Automated Units Properties` | Property-name enum used by `Automated Units Infos` (see below). |
| `AI Category Lookup` | AI target classes: trooper, light, heavy, fast, artillery, tower, wall, mine, building, attack building, … |
| `Unit Order Id Lookup` | Scripted unit orders: none, goto, goto fast, escort, defend, patrol, attack. |
| `Objectives Id Lookup` | Scenario objective kinds: destroy, hold position, escort, protect, intercept, keep, destroy all except, three death-match kinds. `SymbolID` names an objective icon (`OBJ000`…). This icon is **not** in any `.DBI` file, so it is probably drawn elsewhere *(?)*. |
| `MineTypes Lookup`, `RoadType Lookup` (low-tech / high-tech), `Wall Types Lookup` | Enums for the corresponding type tables. |

Declared relationships: `Unit Types.typeID` → `UNIT TYPE ID LOOKUP.Enum`,
`Building Types.TypeID` → `BUILDING TYPE ID LOOKUP.Enum`,
`Unit Types.Race` and `Building Types.Race` → `Race Lookup`,
`Unit Types.Category` → `Unit Category Lookup`, `Unit Types.Movement` →
`Unit Movement Lookup`, `Unit Types.weaponType` → `Weapon Type Lookup`,
`Terrain Types.type` → `TerrainType Lookup`.

### `Unit Types` — combat units (61 columns)

**One row per (unit type, tech level).** `typeID` identifies the unit and
`unitTechLevel` (0–10) the upgrade level. Some units have a single level,
others up to eleven. The `Unit Type Sorted Tech Level` and
`UnitTypes ordered by TypeID and TechLevel` queries only sort this table.

| Group | Columns | Meaning |
|---|---|---|
| Identity | `typeID`, `Race`, `Category`, `Name*` | |
| Tech | `unitTechLevel`, `CostToNextTL`, `Prerequisite` | `Prerequisite` = building `TypeID` needed to obtain the unit. `CostToNextTL` = cost of upgrading to the next level *(?)*. |
| Economy | `Cost`, `isConvoy` | `isConvoy`: transport/convoy unit *(?)*. |
| Footprint | `SizeX`, `SizeY`, `Facings` | Most units are 1×1 with 8 facings. A few 3×3 non-rotating units use building-style tile sprites. |
| Movement | `Movement`, `actionPoints` | Movement class and action points (AP) per turn. |
| Weapon | `UnitHasWeapon`, `weaponName*`, `weaponType`, `weaponRange`, `weaponMinimumRange`, `HitShortRange`, `HitMediumRange`, `HitLongRange`, `actionPointsShoot`, `damageType`, `damageBase`, `damageRandom`, `DamageArea`, `IsAA`, `DamageAA`, `canOverwatch` | Hit chance per range band (bands from the `Ranges` table), AP cost of a shot, damage = base + random part *(?)*, area of effect, anti-air capability and damage. |
| Defense / sensing | `Armor`, `Jammer`, `MineDetectionRange` | |
| AI | `isAITarget`, `AIcategory`, `DanyUnitValue` | `AIcategory` → `AI Category Lookup`. `DanyUnitValue` (signed) is an AI evaluation weight *(?)*. |
| Graphics | `facingsImageID`, `ruinImageID` | 4-letter sprite/animation code. `ruinImageID` = wreck sprite in `RUINS.DBI`. |
| Animation | `MoveAnimation_Begin`, `MoveAnimation_PixSpeed`, `MoveStartAnimation_Begin`, `MoveEndAnimation_Begin`, `ShootBeginAnimation_Begin`, `ShootShootAnimation_Begin`, `ShootShootAnimation_ProjBegin`, `ShootEndAnimation_Begin`, `ShootProjectileAnimation_Begin`, `…_PixOffset`, `…_PixSpeed`, `…_Curve`, `ShootImpactAnimation_Begin`, `ExplosionAnimation_Begin` | Each `…_Begin` is `1` if the unit has that animation and `-1` if not. This matches the existence of the animation files exactly (see *ANMSUNIT.FF*). `PixSpeed` = pixels per frame. `ProjBegin` = frame in which the projectile leaves. `Curve` = projectile arc height *(?)*. |
| Text | `Encyclopedia*`, `EncyclopediaFrame` | |

### `Building Types` — buildings and map objects (34 columns)

One row per building type. `TypeID` ranges by owner: `1xx` human, `2xx`
Tauran, `3xx` Shadamar, `4xx` Shubblar, `10xx` neutral objects (ruins,
containers). A few image codes are used by more than one row, so one
graphic can serve several building variants.

| Group | Columns | Meaning |
|---|---|---|
| Identity | `TypeID`, `Race`, `BuildingCat`, `Name*` | `BuildingCat` → `Building Category Lookup`. Ruins (category 1) are capturable/recyclable remains of earlier structures *(?)*. |
| Construction | `CanBuild`, `Prerequisite`, `UniqueOnMap`, `Cost`, `Recycle` | `Prerequisite` = another `TypeID` or 65535. `Recycle` = credits returned when recycled *(?)*. |
| Footprint | `SizeX`, `SizeY`, `Position` | Size in tiles (1–5). `Position` is `1` only for sea bases and similar, otherwise `3`. It is a placement mask, perhaps water/land bits *(?)*. |
| Production | `ResProduction`, `TechReward`, `Production*` | Resource output per turn *(?)*, tech points gained when captured/destroyed *(?)*, and a description of what the building produces. |
| Combat | `Armor`, `isDestructible`, `hasRuin`, `isAItarget`, `AIcategory` | |
| Towers | `TransformIntoUnit` | Defensive towers exist as both building and unit: this column gives the `Unit Types.typeID` that represents the tower in combat (65535 otherwise). |
| Graphics | `ImageID`, `DamagedImages`, `ExplosionAnimation_Begin`, `ExplosionAnimation_Qty` | `ImageID` = sprite/animation code (stored with mixed case, compare case-insensitively). `DamagedImages` = a damaged sprite set exists. |
| Text | `Encyclopedia*`, `EncyclopediaFrame` | |

### `Automated Units`, `Automated Units Infos`

"Automated units" are support actions that are called in (helicopter
transport, bridge builder, mine layer/detector, medic, bombers, nuke,
paratroopers, Zeppelin, fighters, …). They are not placed as normal units.

`Automated Units` (one row per `typeID`): `Race`, `Category` (→ automated
category), `Cost`, `Prerequisite` (building `TypeID`), `DeployOnce`,
`facingsImageID` (code of the animation in `ANMSUNIT.FF\AUTOUNIT`),
`cursorID` (targeting cursor in `SYMBOLS.DBI`, e.g. `UATRANSP`), `Explain*`
(the one-line instruction shown while targeting), the encyclopedia columns,
and animation timing: `AnimationOK_Begin/Qty`, `AnimationDamaged_Begin/Qty`,
`AnimationDestroyed_Begin/Qty` (frame ranges inside one animation for the
three outcomes *(?)*), `FrameAction` (frame in which the effect happens),
`FrameAA`, `AAPosX`, `AAPosY` (frame and screen position where anti-air fire
can hit it *(?)*).

`Automated Units Infos` is an **entity–attribute–value** table:
(`typeID`, `Property` → `Automated Units Properties.Enum`, `Value` as text).
The properties hold per-type parameters such as armor, damage, damage type
and area, mine detection range, the unit type / tech level / count / tile
offsets of dropped units (up to five), and a second animation set
(`Secondanim…`). Values must be parsed as integers by property.

### Other rule tables

| Table | Meaning |
|---|---|
| `Mine Types` | Standard and grav mine: which unit categories they affect, damage type/base/random, `deployTerrain` (terrain class where they can be laid), cost, recycle value, `ImageID` (`MINES.DBI` prefix), `listboxImageID` (`LISTBOX.DBI`), explosion animation. |
| `Road Types` | Low-tech and high-tech road: cost, recycle, `ar` (armor *(?)*), `img_id` (prefix in `ROADS.DBI`: `ROAL`, `ROAH`). |
| `Wall Types` | Concrete, laser fence, force field, ruined wall: cost, recycle, armor, `BlockShots`, `img_id` (prefix in `WALLS.DBI`: `WALLC`, `WALLL`, `WALLF`, `WALLR`). |
| `Terrain Types` | One row per terrain tile graphic (1071 rows): `id` (sprite name in `TERRAINS.DBI`), `type` (→ `TerrainType Lookup`), `can_fire_through`, `Overlapping` (tile is drawn over its neighbors, e.g. tall mountains/forest *(?)*), `burned_id` (sprite this tile turns into when burned, also in `TERRAINS.DBI`; empty if it cannot burn), `burned_min` *(?)*. `counter` is stored as Double. |
| `Ruin Types` | One row per ruin/wreck sprite (`id` in `RUINS.DBI`): `can_fire_through`, `can_move_through`. |
| `AI Priority` | A complete `AiSource` × `AiTarget` matrix over the 12 AI categories with a `Priority` weight: how much an AI unit of one category wants to attack a target of another. |
| `Colors` | Player/side colors as RGB. `Type` selects the object class (0 buildings, 1 units, 2 roads, 3 walls, 4 mines). `Enum` within the class: 0 = race default (human), 1–4 = player colors, 5 Tauran, 6 Shadamar, 7 Shubblar, and for buildings also 8 tech, 9 ruin. |
| `Difficulty` | One row: credit, damage and deployment multipliers (percent) for easy / average / hard. |
| `Ranges` | One row: tile distances that separate the short, medium and long range bands used by the `Hit…Range` columns. |
| `Tech Ratio` | Tech-point brackets (`MinRange`–`MaxRange`, `-1` = open-ended) with a `Ratio` *(?)*: probably a conversion rate of tech points per bracket. |

Saved queries in GLOBAL (`Building Types Query`, `Translation - …`,
`Units  Anims`, …) are sorted/translated views or bulk updates used while
editing, and the engine does not need them *(?)*.

## GAMEINFO.MDB — runtime state template

Nearly empty tables that describe the state the original program keeps in a
database while a game runs. The client–server architecture (see
`CLAUDE.md`) exchanges this state *(?)*. It is a good model for OpenDay's
save format, but it is not game content.

| Table | Meaning |
|---|---|
| `Campaign State Lookup` | Campaign phases: first colony, first city attack, new colony, strategic, tactic, pre-/counter-attack, … |
| `Server Mode Info` | Difficulty, turn time limit. |
| `Server Mode Campaign` | Current colony, campaign phase, province of the tactical battle, flags to load the tactical scenario / terrain change list. |
| `Player Campaign`, `Player Campaign Tech` | Per player: credits, tech points, kill/loss statistics; per (player, unit type): reached tech level. |
| `Player Tactical`, `Player Tactical Units destroyed / Units Lost / Buildings Destroyed / Buildings Lost` | Per-battle deploy resources and statistics per unit/building type. |
| `Strategic IO Building / Mine / Road / Wall` | Objects the player placed on the strategic layer that must be carried into the tactical map *(?)*. |
| `Tactic IO` | Current player, current turn, objective turn, last turn. |
| `Terrain Change List` | Tile positions and new terrain sprite ids (burned/destroyed terrain kept between battles *(?)*). |
| `User List` | Network users: name, password, id, color. |

## SKIRMISH.MDB and DEATHM.MDB — scenario catalogs

One table, `Scenario Info`: `FileName` (the scenario `.mdb` inside the ZIP),
title, province name and description (three languages), `One Off
Deployment` (deployment budget in this mode) and `Size` (map size in tiles).
The skirmish catalog lists 46 of the campaign variant-1 files. Skirmish
mode reuses the campaign maps.

## Colony databases (`COLONIES.ZIP`: `C1`…`C4`)

One per colony (continent) of the strategic campaign.

| Table | Meaning |
|---|---|
| `Continent Info` | One row: colony number, number of provinces, start cash, name, `PlayerCity` and `AlienCity` (province numbers of the two capitals: the first and the last province), AI automated-unit call chance and budget, `Intro`/`End` (video names, cf. `VIDEO\INTRO1.SMK` …), `SmackProvinces` + frame range (strategic map animation in `COLANMS\C<n>.SMK`), deployment multipliers per difficulty. |
| `Province List` | One row per province (`NoProvince` 1…n): name, mission title, description and objective text, `MapSize`, `TerrainType` *(?)*, `Credits` and `Tech` reward, four `mapScreen1…4` OLE images (8-bit BMPs whose size depends on the map size, perhaps for four players or zoom levels *(?)*), `Anim` (empty). |
| `Province List Captured` | Initial state per province: `Captured`, `RandomChosen` (which scenario variant, 1 or 2, is played there; see file naming). |
| `Adjacent Provinces` | Undirected adjacency pairs (`ProvinceNo`, `AdjacenTo`) of the strategic map. |
| `Buildings Available` | Building `TypeID`s the player may build in this colony. |
| `AI automated available` | Automated-unit types the AI may call in this colony. |

## Scenario databases (`SCEN1`, `SCEN2`, `DEATHM` ZIPs)

One file per tactical map. All have the same tables. Most are empty in a
given file, and three are empty in every shipped file (`Attack Army`,
`Taurans On Map`, `Ruins On Map`). Linked tables point back to GLOBAL lookups
(see MDB.md).

| Table | Meaning |
|---|---|
| `Scenario Info` | One row: title, province name, briefing (`description*`), debriefing (`Aftermath*`), `turn_qty` (turn limit), `necessary_unit` *(?)*, `One Off Deployment`, `Credits`, `ReinfRes` *(?)*, `CounterAttack` (scenario has a counter-attack phase), `NextID_Units` / `NextID_Buildings` (next free object ids: unit ids start at 100, building ids at 1000000), and two OLE images `mapScreen640` / `mapScreen1024` (8-bit BMP overview maps for the two UI resolutions, 340 and 564 pixels square). |
| `Map` | **The terrain:** one row per tile (`size × size` rows) with `PosX`, `PosY` (bytes) and `TerrainImg` (sprite name in `TERRAINS.DBI`, always also a `Terrain Types.id`). `Id` is a running counter. |
| `Units On Map` | Pre-placed units: object `ID`, `unitType` (→ `Unit Types.typeID`), `unitTL` (tech level), `playerOwner`, `facing` (0–7), `currentAR` (armor/hit points), `currentAP`, `negAP` *(?)*, position, `OrderID` (→ `Unit Order Id Lookup`), `Turn` and `Probability` (when / with what chance the unit acts or appears *(?)*), `EnemyUnitType` (preferred target type, 65535 = any *(?)*), and the dash-named Boolean (separator, always true *(?)*). |
| `Unit Order ID List` | (`Unit ID`, `Id`): target object ids of an order (whom to escort/defend/attack). |
| `Unit Order Pos List` | (`Unit ID`, `PosX`, `PosY`): waypoints of goto/patrol orders. |
| `Buildings On Map` | Pre-placed buildings: object `id`, `buildingType` (→ `Building Types.TypeID`), `playerOwner`, `currentAR`, top-left position *(?)*, `IsAiTarget`. |
| `HQ on Map` | Headquarters position per human player slot (1–4). |
| `Deployment` | Tiles where each player (1–4) may deploy units (`Turn` is always 0). |
| `Reinforcement` | Units arriving later: `UnitID`, `type`, position, `turn`, `order`. |
| `Counter Attack` | Units used in the counter-attack phase, same columns as `Reinforcement`. |
| `Attack Army` | Same shape, unused. |
| `Automated Units` | Automated units the AI calls in: `AutomatedType`, start and destination tile, `TurnArrival`. |
| `Roads On Map` | `roadtype`, `playerOwner`, position. |
| `Walls On Map` | Wall type (column named `roadtype`), `playerOwner`, `currentAR`, position, `isDamaged`. |
| `Mines On Map` | `typeID`, `playerOwner`, position, `armed`, and visibility flags `hidden_AI`, `hidden_1` … `hidden_4`. |
| `Ruins On Map` | Ruin sprite `id`, position (unused in shipped files). |
| `Objective` | One row: `ObjectifId` (→ `Objectives Id Lookup`), `TurnNumber` (turn limit or hold duration *(?)*). |
| `Objectives ID List` | Object ids (units or buildings) the objective refers to. |
| `Objectives Pos List` | Tiles the objective refers to (zone to hold/reach). |
| `One Off Units Available`, `One Off Automated Available` | (`Enum`, `Available`): unit types / automated types offered for purchase in one-off (skirmish/death-match) play. |
| `Taurans On Map` | Same shape as `Units On Map`, unused. |

### Scenario file names

- Campaign: `s<colony>-<province>-<variant>.mdb`. The colony number matches
  `C<colony>`, the province matches `Province List.NoProvince` (SCEN1
  contains provinces 1…7, 1…12, 1…15, 1…20 for the four colonies, matching
  their province counts). Variant 1 is in `SCEN1.ZIP`, variant 2 in
  `SCEN2.ZIP`. Province 1 (the player's capital) only has variant 1.
  `Province List Captured.RandomChosen` presumably picks the variant *(?)*.
- Death match: `dm<k><a>-<b>.mdb`, where `k` is `a`, `c` or `s`, matching
  the three death-match objective kinds ("city + alien", "2 cities", "slug
  fest") *(?)*. The meaning of the two numbers is open.

## Asset naming (sprites and animations)

All codes are case-insensitive (the database stores some in mixed case,
e.g. `Hhq`, `Tstr`). Every code the databases reference was found in the
asset name lists, with one exception (objective icons, see above).

### Object codes

A 3–4 letter code identifies the graphic of a unit, building or automated
unit (`Unit Types.facingsImageID`, `Building Types.ImageID`,
`Automated Units.facingsImageID`). The first letter is the side:
`H` human, `T` Tauran, `B` Shubblar, `M` Shadamar, `R` neutral ruins and
containers. The rest abbreviates the name (e.g. `HTRO` trooper, `HFAC`
factory, `BDN` Shubblar unit). `UB.DBI` holds 134 code families.

### `UB.DBI` — units and buildings: `<code><c><d><x><y>`

Four characters follow the code:

- **Units (1×1, with facings):** `<c>` = color variant, `<d>` = facing
  `0`–`7`, then `00`. `<c>` is `0` (race default color, `Colors` enum 0/5/6/7)
  or `1`–`4` (player colors, `Colors` enum 1–4). Some units have only
  variant `0`, e.g. convoy vehicles and units that only the AI uses *(?)*.
  Infantry codes also have facings `A`–`H`, a second pose per direction
  for some or all color variants. This is probably a firing or crouching
  pose *(?)*.
- **Buildings:** `<c>` = color variant as above (human buildings mostly
  `1`–`4`, other races `0`), `<d>` = state `0` intact, `1` damaged (only when
  `DamagedImages` is true; some neutral objects use `1` for a second look *(?)*),
  `<x>` = tile column `0 … SizeX-1`, `<y>` = tile row `0 … SizeY-1`. A
  building is drawn as `SizeX × SizeY` separate tile sprites.
- **Towers** (building + unit, `TransformIntoUnit`) use the unit scheme
  (8 facings), since their turret rotates.
- **3×3 units without facings** use the building scheme with `<d>` = `0`.

So a sprite name such as `BDN0600` reads as: code `BDN`, color variant 0,
facing 6.

### `ANMSUNIT.FF` — animations: `<code>\P<c>\<code><c><a><d>.SMK`

Every entry is a Smacker file. Folder `P<c>` = color variant (as in UB.DBI;
most human units only have `P1`–`P4`). `<a>` = action letter, `<d>` = facing
`0`–`7` (one-direction actions use only `0`):

| Letter | Animation | DB column (`Unit Types`) |
|---|---|---|
| `M` | move (8 dirs) | `MoveAnimation_Begin` |
| `L` | move start, e.g. take-off/diving (8 dirs) | `MoveStartAnimation_Begin` |
| `N` | move end, e.g. landing/surfacing (8 dirs) | `MoveEndAnimation_Begin` |
| `S` | shoot begin (8 dirs) | `ShootBeginAnimation_Begin` |
| `T` | shoot (8 dirs) | `ShootShootAnimation_Begin` |
| `U` | shoot end (8 dirs) | `ShootEndAnimation_Begin` |
| `P` | projectile (1) | `ShootProjectileAnimation_Begin` |
| `I` | impact (1) | `ShootImpactAnimation_Begin` |
| `E` | explosion / death (1) | `ExplosionAnimation_Begin` |

The `…_Begin = 1` flags agree with the presence of these files for every
unit row, except that two units have projectile files while their rows say
`-1`.

Special folders: `BUILDING\<ImageID>.SMK` (one animation per building type,
probably the destruction/explosion sequence, cf. `ExplosionAnimation_*`
*(?)*), `MINE\MINE.SMK` / `GMINE.SMK` (mine explosions), and
`AUTOUNIT\<code><s><n>.SMK` for automated units, with `<n>` = `1`–`3` and
`<s>` = `0` for the main animation and `1` for the second animation set
that some types define via `Secondanim…` properties *(?)*. The automated
units' `…_Begin/_Qty` frame ranges point into these files *(?)*. The meaning
of `1`–`3` is open: perhaps a resolution or zoom level.

### Other DBI files

| File | Name scheme |
|---|---|
| `TERRAINS.DBI` | `<family><nnn>`. Families are climates: `ARID`, `DESE` (desert), `NORD` (temperate), `POLE` (polar), `BORE` (boreal), `JUNG` (jungle), `EAU` (open water). A family with `X` in place of its last letter (`ARIX`, `BORX`, …) holds burned variants. `T` + two climate abbreviations (`TARDE`, `TBONO`, `TDEJU`, `TNOPO`, and their `…X` burned forms) are transition tiles between two climates *(?)*. The hundreds digit loosely groups features (forest, mountain/rock, water edge, …), but the authoritative class is `Terrain Types.type`. |
| `RUINS.DBI` | `B<r>RUI<nnn>` building rubble and `U<r>RUI<nnn>` unit wrecks, `<r>` = race letter (`H`, `T`, `B`, `M`). For unit wrecks, the first digit also encodes the race (1 Shubblar, 2 human, 3 Shadamar, 4 Tauran *(?)*). |
| `ROADS.DBI` | `ROA<t><k><nnn>`: `<t>` = `L` low-tech / `H` high-tech (`Road Types.img_id`), `<k>` = `G`, `R`, `B` sub-sets (plain ground, river crossing, bridge *(?)*), `<nnn>` = connection variant, 16 per set (a 4-neighbor bitmask *(?)*). |
| `WALLS.DBI` | `WALL<t><s><nn>`: `<t>` = `C`/`L`/`F`/`R` (`Wall Types.img_id`), `<s>` = `0` intact / `1` damaged (`Walls On Map.isDamaged` *(?)*), `<nn>` = connection variant. |
| `MINES.DBI` | `MINE<nnn>`, `GMINE<nnn>` (`Mine Types.ImageID` + number). |
| `LISTBOX.DBI` | `<code>999`: the 38×38 list/purchase icon of every unit, building, automated unit, mine, road and wall code. |
| `SYMBOLS.DBI` | UI symbols and cursors: `UA…` automated-unit target cursors (`Automated Units.cursorID`), `DEPL<p><nn>` deployment-zone markers per player *(?)*, `PATH…`, `SEL…`, `TARGET`, … |

## Open questions

- Which of the saved queries (if any) the original program executes.
- Exact semantics of `Units On Map.Turn` / `Probability` / `EnemyUnitType`,
  `negAP`, `necessary_unit`, `ReinfRes`, `Tech Ratio`, `Building Types.Position`,
  `burned_min`.
- Meaning of the numbers in death-match file names, of the `1`–`3` suffix of
  automated-unit animations, and of the four colony `mapScreen` images.
- Whether infantry facings `A`–`H` are a firing pose or another state.
- Where the objective icons (`OBJ000`…) are stored (not in any `.DBI`;
  `SYMBOLS.DBI` has similar `OBJT…` names).
- How `GAMEINFO.MDB` is copied/used at run time (`CACHE\` folder).
