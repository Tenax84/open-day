# Saving and loading

Where the original program lets the player save, what a save slot
(`SAVEGAME\savegame.NN`) contains, how a saved game — including a battle in
progress — is resumed, and what is lost on the way. The last sections state
OpenDay's own approach.

Prerequisites: [campaign.md](campaign.md) (campaign states 0–9, terrain
change list), [battle-flow.md](battle-flow.md) (turn order, objectives,
statistics), [city-building.md](city-building.md) 6 ("built in this phase"
lists), [data-model.md](data-model.md) (tables and columns of
`GAMEINFO.MDB` and the scenario databases), [interface.md](interface.md)
(options, save and load dialogs).

Status: derived from studying the original program. Items marked *(?)* are
inferred and not fully confirmed.

## Terms

| Term | Meaning |
|---|---|
| slot | save slot number N = 1…10 |
| slot folder | `<savegame path>savegame.NN\` with NN = two digits 01…10 |
| savegame path, globals path, cache path | the `[Installation]` paths of `LIBDAY.INI` |
| host | the program instance that runs the server (in single player: always the local program) |
| phase record | the part of `GAMEINFO.MDB` that belongs to the running phase: `Tactic IO` for a battle, `Strategic IO …` for the strategic phase |

## 1. Where saving is offered

### 1.1 Conditions

- Saving exists **only in campaign games** (new or loaded campaign). Skirmish
  and death match never save: their servers have no save handling and the
  *Save* button is always disabled there.
- Only the **host** can save. In a network campaign the other players' *Save*
  buttons are disabled.
- Saving is started from the in-game options dialog (`interface.md`, "In-game
  options dialog"). Whether *Save* is enabled depends on the screen:

| Screen | Save | Surrender | Quit |
|---|---|---|---|
| city screen | yes | no | yes |
| continent screen (normal) | yes | no | yes |
| tactical view, your own turn | yes | yes | yes |
| tactical waiting view (another participant's turn, including the computer player's) | yes | no | no |
| every other screen: deployment, reinforcement purchase, automated units, continent win view, colony base view, tactical / strategic map screens | no | no | yes |

- A disabled *Save* button is only drawn greyed; there is no refusal message.

### 1.2 Consequences per campaign state

- **State 3** (strategic): save from the city screen or the continent screen.
- **States 1, 4, 6** (battles): save from the tactical view or the waiting
  view, i.e. at any moment of the turn cycle **after deployment**, also while
  the computer player is moving *(?: the request is queued on the server and
  handled between the AI's requests; whether it can fall in the middle of an
  AI action sequence was not verified)*.
- Not possible: during deployment, while buying reinforcements or calling
  automated units, during videos and loading pictures, in the battle report,
  in the continent win view (state 7) and in states 0, 2, 5 and 7 in general.
- Whether the options dialog can be opened over the army screen in state 3
  was not checked *(?)*.

### 1.3 Save request and feedback

1. In the save dialog (`interface.md`) the player picks a slot and types a
   name (at most 31 characters). Leading and trailing blanks are removed; an
   empty name does nothing.
2. The client closes the save dialog, shows a message box without buttons
   with string 816 ("...SAVING...") and sends `CCampaignSaveMsg` {slot
   (integer 1–10), name (32-byte text field)} to the server.
3. The server writes the slot (section 2.3) and answers with
   `CConfirmSaveMsg` {success flag}.
4. The client closes the "...SAVING..." box. On failure it shows string 817
   ("A problem occured during saving. Please try again.") in an OK box; on
   success nothing more is shown *(?: the options dialog stays open)*.

## 2. The slot folder

### 2.1 Location and files

Slot N is the folder `<savegame path>savegame.NN\`. The program **never
creates** these folders; the installation ships all ten (`savegame.01` …
`savegame.10`). If a folder is missing, copying into it fails and the save
fails *(?)*.

| File | Content | Written |
|---|---|---|
| `GAMEINFO.MDB` | a fresh copy of the template `<globals path>GAMEINFO.MDB`, then filled (2.4) | always |
| `CITY.MDB` | the player's city scenario: a copy of its source database, then the object tables rewritten from memory (2.5) | always |
| `COLONY.MDB` | the colony database: a copy, then `Province List Captured` rewritten (2.6) | always |
| `SCENARIO.MDB` | the province battle scenario, treated like the city (2.5) | only when a province scenario is loaded on the server, i.e. in state 4 |
| `<name>.dsc` | an **empty** file; its file name (without `.dsc`) is the slot's description | always, as the last step |

Notes:

- File-name case does not matter on Windows (the save writes upper-case
  names, the load opens lower-case ones). OpenDay need not reproduce this.
- An older `SCENARIO.MDB` in the slot is **not deleted** when a later save
  (state 3) has no province scenario; it is ignored on load because loading
  decides by the saved state.
- No files of the `CACHE` folder and no index file are involved.
- Description file: every `*.dsc` file in the slot folder is first made
  writable and deleted, then `<name>.dsc` is created empty. If the name
  contains characters that are not allowed in file names, the creation fails
  silently (the step still reports success) and the slot later shows an
  empty name *(?)*.

### 2.2 Copy rule

For each database the destination is `<slot folder><FILE>`. The source:

| Database | Source |
|---|---|
| `GAMEINFO.MDB` | `<globals path>GAMEINFO.MDB` (the empty template) |
| `COLONY.MDB`, `CITY.MDB`, `SCENARIO.MDB` | the file the object was loaded from. If its name begins with `SAVEGAME` (case-insensitive; the object itself came from a slot), the source is `<savegame path><name>`; otherwise `<cache path><name>` (the file extracted from the ZIP archives). |

If source and destination are the same file (saving into the slot the game
was loaded from) nothing is copied. Otherwise the destination is
overwritten and made a normal writable file. Then the destination database
is opened and updated.

### 2.3 Save sequence (server)

The server performs these steps in order, each as a request to the host's
client process, and waits for each answer. Each wait has a timeout of
**180 s** (an error). A step that answers *false* stops the sequence: the
remaining steps are skipped and the result is *false*.

1. Users: copy the template `GAMEINFO.MDB` into the slot, write `User List`.
2. Game info: write the campaign record into `GAMEINFO.MDB` (2.4).
3. City: copy and write `CITY.MDB`.
4. Colony: copy and write `COLONY.MDB`.
5. Scenario: only if a province scenario exists — copy and write
   `SCENARIO.MDB`.
6. Description: replace the `.dsc` file (always succeeds).
7. Send `CConfirmSaveMsg` {all steps succeeded} to the requesting client.

Quirk: the files are overwritten step by step and the description comes
last. A failure in the middle leaves the slot with the **old name** over a
mix of new and old files.

### 2.4 `GAMEINFO.MDB`

The template already contains one row in `Server Mode Info` and one in
`Tactic IO`; those rows are **edited**. Every other table listed here is
**emptied and refilled**.

| Table | Content |
|---|---|
| `User List` | one row per network user: `User Name`, `User Password` (an empty password is stored as a single space), `User ID`, `User Color` |
| `Server Mode Campaign` | one row: `Colony Number` (current colony index), `Campaign State` (0–9), `Tactical Province` (province of the current or last battle), `Load Terrain Change List` (a terrain change list exists), `Load Tactical Scenario` (a province scenario exists) |
| `Server Mode Info` | row edited: `Difficulty`, `Turn Time` (from the session) |
| `Terrain Change List` | only if the list exists: one row per entry (`PosX`, `PosY`, `Id` = terrain sprite name), the campaign's list of original tiles to restore after a city battle (campaign.md 6.1, 6.6) |
| `Player Campaign` | one row per player: `Player ID`, `Credits`, `Tech`, `Units Destroyed`, `Units Lost`, `Buildings Destroyed`, `Buildings Lost` |
| `Player Campaign Tech` | per player and unit type: `Player`, `Unit Type`, `Tech Level` |
| `Player Tactical` | one row per player: `Player ID`, `Deploy Res` (the player's current deployment / reinforcement resource). The row's own four total columns are **not** written. |
| `Player Tactical Units Destroyed`, `… Units Lost`, `… Buildings Destroyed`, `… Buildings Lost` | the battle statistics so far, per player and type (battle-flow.md 5.3): `Player`, `Unit Type` or `Building Type`, `Number` |
| phase record | depends on the running server phase: battle → `Tactic IO`; strategic phase → the `Strategic IO …` tables. (Any other phase cannot be saved through the interface.) |

`Tactic IO` (row edited):

| Column | Value |
|---|---|
| `Current Player` | the battle's current-player pointer: 0 = computer player, 1–4 = human slot, 5 = none (deployment) |
| `Current Turn` | the turn counter T |
| `Objective Turn` | the objective's progress counter as it would be reported at turn T: for **Hold Position** the hold counter, but **0** if a last held turn is set and T > last held turn + 1 (the counter is about to be lost); for the other kinds the raw counter (normally 0). This is the same value as the red number of the turn counter (tactical-interface.md 2.5). |
| `Last Turn` | the objective's "last held turn" (Hold Position; −1 = none) |

`Strategic IO Building`, `… Mine`, `… Road`, `… Wall`: the "built in this
strategic phase" lists of city-building.md 6 for player slots 0–4.
`Building` rows: `Player`, `Id`; `Mine` / `Road` / `Wall` rows: `Player`,
`PosX`, `PosY`.

### 2.5 Scenario databases (`CITY.MDB`, `SCENARIO.MDB`)

The copied file keeps every table of its source. Then, inside one
transaction, these tables are rewritten from the server's current scenario:

| Table | Written columns |
|---|---|
| `Objective` (+ `Objectives ID List`, `Objectives Pos List`) | emptied; one row `ObjectifId` (kind), `TurnNumber` (N); the ID list and the zone rewritten |
| `Buildings On Map` | emptied; per building: `id`, `buildingType`, `playerOwner`, `currentAR`, `PosX`, `PosY` (`IsAiTarget` left empty) |
| `Mines On Map` | emptied; per mine: `typeID`, `PosX`, `PosY`, `armed`, `hidden_AI`, `hidden_1` … `hidden_4`, `playerOwner` (armed state and per-player knowledge are kept) |
| `Roads On Map` | emptied; `roadtype`, `PosX`, `PosY`, `playerOwner` |
| `Walls On Map` | emptied; `roadtype` (wall type), `currentAR`, `posX`, `posY`, `playerOwner`, `isDamaged` |
| `Units On Map` (+ `Unit Order ID List`, `Unit Order Pos List`) | all three emptied; per unit of every owner (computer included): `ID`, `unitType`, `unitTL`, `playerOwner`, `facing`, `currentAR`, `currentAP`, `negAP`, `PosX`, `PosY`, `OrderID`, `Turn`, `Probability`, `EnemyUnitType`; if `OrderID` ≠ 0 also its id-list and position-list rows (keyed by `Unit ID`) |
| `Automated Units` | emptied; the still-pending scheduled automated units: `AutomatedType`, start, destination, `TurnArrival` |
| `Reinforcement` | emptied; the still-pending scripted reinforcements: `UnitID`, `type`, `posX`, `posY`, `turn`, and `order` **always 0** (quirk, section 4) |
| `Map` | only if any tile changed since the scenario was loaded (burned terrain, wrecked road, …): emptied and rewritten (`PosX`, `PosY`, `TerrainImg` = current sprite name; rows in y-outer, x-inner order) |
| `HQ On Map` | emptied; the current HQ list (owner, position) |

**Not written** (they keep whatever the source file had): `Ruins On Map`,
`Scenario Info` (including `NextID_Units` and `NextID_Buildings`),
`Deployment`, `Counter Attack`, `Attack Army`, `One Off Units Available`,
`One Off Automated Available`, `Taurans On Map`.

### 2.6 `COLONY.MDB`

`Province List Captured` is emptied and refilled with one row per province in
order: `NoProvince` (1-based), `Captured` (bool), `RandomChosen` (the
province's current variant). All other colony tables are unchanged copies.

## 3. Loading

### 3.1 Steps

1. Front end: the load dialog (`interface.md`) sets the session to "loaded
   campaign" with the slot number.
2. The server reads from `<slot folder>GAMEINFO.MDB`:
   - `User List`;
   - `Server Mode Info`: `Difficulty` and `Turn Time`. These **replace** the
     values of the front end.
3. The campaign server starts in "loaded" form and reads:
   - `Server Mode Campaign`: `Colony Number`, `Campaign State`,
     `Tactical Province`, `Load Terrain Change List` (`Load Tactical
     Scenario` is written but never read);
   - `Player Campaign` with the tech maps, `Player Tactical` with the battle
     statistics;
   - the `Terrain Change List` if its flag is set;
   - the phase record chosen by the **state**: states 1, 4, 6 → `Tactic IO`;
     state 3 → `Strategic IO`; other states → none.
4. Start:
   - the colony is loaded from the slot's `COLONY.MDB` (captured flags and
     variants come from the saved table; the provinces are **not**
     re-initialised as for a new colony);
   - the city is loaded from the slot's `CITY.MDB`;
   - the colony data are sent to the clients;
   - then, by the saved state:

| Saved state | Resumes with |
|---|---|
| 0 | the new-colony start: city initialisation and the intro video (not reachable by saving) |
| 1 | the first city attack (3.3) |
| 3 | the strategic phase (3.5) |
| 4 | the province battle on `Tactical Province` (3.2) |
| 6 | the counter-attack city battle (3.3) |
| 2, 5, 7, 8, 9 | **nothing is started** (the game would hang) — not reachable by saving |

5. When the first phase of the loaded game has ended, the slot number and the
   phase record are discarded; from then on everything is loaded from the
   normal sources (new province scenarios from the cache, etc.).

### 3.2 Resuming a province battle (state 4)

1. The province scenario is loaded from the slot's `SCENARIO.MDB` with the
   normal scenario loader (the rewritten tables of 2.5 and the unwritten ones:
   ruins, deployment zones, counter-attack rows, …).
2. The next free ids are corrected: the next unit id and the next building id
   become (largest id present + 1) if the stored value is **smaller** than
   the largest id present.
3. The Hold Position state is put back into the objective: `Objective Turn`
   → hold counter, `Last Turn` → last held turn.
4. The deployment zones of the human slots are merged by player count as for
   a new battle (campaign.md 4.1 step 2). The new-battle step that hands units
   of absent slots to slot 1 (campaign.md 4.1 step 3) is **skipped**; the
   saved owners are final.
5. The per-player deployable unit and automated-unit lists are recomputed
   from the city's buildings (campaign.md 4.1 step 4). The `Player Tactical`
   records are **not** re-created; they come from `GAMEINFO.MDB`.
6. The scenario is sent to the clients and the tactical phase starts (3.4).

### 3.3 Resuming a city battle (states 1 and 6)

- A **new** counter-attack objective (kind 7, N = 99) is created for the city,
  as for a fresh city battle. The saved `Objective` table of `CITY.MDB` is
  therefore irrelevant (kind 7 has no counters), and the `Tactic IO`
  objective counters are not applied.
- Skipped (only done for a new battle): turning the attacker rows into
  reinforcements, placing the attackers, placing the turn-0 reinforcements,
  turning towers into units, starting a fresh terrain change list and (state
  6) discarding the province scenario (campaign.md 6.1). The saved unit list
  already contains the tower units and the placed attackers; the pending
  attackers are in the saved `Reinforcement` table.
- Done also on load: the per-player deployment zones are recomputed from the
  city (campaign.md 6.2).

### 3.4 Tactical phase start: new versus loaded battle

| | New battle | Loaded battle |
|---|---|---|
| resources | per-player deployment budgets computed and sent | **not** recomputed; each player is sent his saved `Deploy Res` |
| turn state | current player 5 (none), T = 0, deployment starts | current player and T from `Tactic IO`; the battle is "playing"; **no deployment** |

In both cases the turn time limit and the objective description are sent to
all clients first.

Then the begin-of-turn routine (battle-flow.md 1.3) runs in **resume mode**:

- **skipped:** the objective check (step 1), moving the current-player
  pointer, the round counter step, and the start-of-turn effects of the
  current player (tech-building re-evaluation, reinforcement income, AP
  refill);
- **done:** if the saved current player is 0, the computer player is told to
  play its turn (from the first of its phases, tactical-ai.md); otherwise that
  human player receives the "your turn" message (with T, the turns left and
  the objective counter) and every other human player the "wait" message.

So a battle resumes **at the start of the saved current player's turn, with
the units' saved AP**: AP spent before saving stays spent, no income is
added, no AP is refilled. A save made during the computer player's turn
makes it replay its turn from its first phase with whatever AP its units
had left.

### 3.5 Resuming the strategic phase (state 3)

The city screen phase starts with the saved "built in this phase" lists
(city-building.md 6), so recycling a building placed before the save still
refunds its full cost.

### 3.6 Recomputed rather than stored

- next free object ids (3.2 step 2);
- unit pixel positions and all type-derived values (from type and tech
  level);
- deployment zones; deployable unit and automated-unit lists;
- held tech buildings: re-evaluated at each turn start, but **not** at the
  resumed turn's start (resume mode); the next turn start does it;
- mine visibility comes from the saved `hidden_*` flags; no other
  visibility state exists.

## 4. Not stored / lost on reload (quirks of the original)

- **Wrecks and rubble created during the battle**: `Ruins On Map` is never
  written, so the reloaded battle has only the ruins of the original
  scenario file. Burned or wrecked terrain sprites are kept (`Map`).
- **`order` flag of pending reinforcements**: saved as 0, so after reloading
  those units arrive without an order (battle-flow.md 4.1).
- **Computer player state** other than unit orders: the AI is created anew
  *(?: it starts again with its full automated-unit budget
  `AiAutoUnitRes`)*. Unit orders (`OrderID`, `Turn`, `Probability`,
  `EnemyUnitType`, id and position lists) are kept.
- **Objective counters** other than the two `Tactic IO` values; city battles
  get a fresh kind-7 objective.
- **Random number state** (no seed is stored).
- **Client state:** selection, view position, minimap, the *done* marks
  (GUARD / 0-AP units), remembered destinations and marked tiles, the
  message line, pending animations.
- **Other mid-turn server state** that exists only in memory *(?: not
  examined in detail)*.
- Never written: the four total columns of `Player Tactical`,
  `Buildings On Map.IsAiTarget`, the units' unnamed spare column.
- A stale `SCENARIO.MDB` stays in a slot after a later strategic save.
- A save that fails in the middle keeps the old slot name over partly new
  files (2.3).
- Dead ends the interface prevents: saving in a phase that is neither a
  battle nor the strategic phase would crash; loading a state other than 0,
  1, 3, 4, 6 starts nothing.

## 5. Network campaign

The slot files exist only on the host. How the other players rejoin a loaded
network campaign (the `User List` passwords and colours suggest a check of
the joining users) was not examined *(?)*.

## Notes for OpenDay

- OpenDay does **not** read or write the original `savegame.NN` slot folders
  or any MDB file. It uses its **own text save format** (one file per slot in
  the user's settings folder, a description line first so that the slot list
  can show names without loading the file).
- What has to be preserved is the **content and the resume behaviour**
  described above:
  - save points: strategic state (3) and battles (states 1, 4, 6) after
    deployment, during the player's own turn and during waiting turns;
    campaign only; host only;
  - campaign part: colony, state, tactical province, difficulty, turn time,
    per-player credits / tech / statistics / tech-level maps, the colony's
    captured flags and variants, the city scenario's object lists, the
    terrain change list, the "built in this phase" lists;
  - **battle in progress:** the object lists of 2.5 (buildings, mines with
    visibility, roads, walls, units with AP, `negAP`, facing and orders,
    pending automated units, pending reinforcements, changed terrain, HQ
    list), the objective (kind, N, ID list, zone, hold counter and last held
    turn), T, the current player, each player's tactical resource and the
    battle statistics (per type, battle-flow.md 5.3), the tech-building held
    set;
  - resume at the start of the saved current player's turn without the
    start-of-turn effects (no income, no AP refill, no objective check);
    the computer player restarts its turn from its first phase.
- **Deliberate deviations** (to be listed as such in the implementation
  notes):
  - also save the **wrecks and rubble** created during the battle and the
    `order` flag of pending reinforcements (the original loses them, which
    is clearly a defect);
  - also save the AI's remaining automated-unit budget and the random-number
    state, so that a reload continues the same battle;
  - write the save **atomically** (write a temporary file, then rename) so
    that a failed save never damages the previous content of the slot;
  - saving a skirmish in progress may be offered as an OpenDay extension
    (the original cannot); it must use the same format.
- Keep the restriction that saving is not possible during deployment or
  while buying reinforcements; the save format then never needs a
  "deployment in progress" state.

## Open questions

1. Whether a save request can be handled in the middle of the computer
   player's action sequence.
2. The computer player's automated-unit budget after a reload (full
   `AiAutoUnitRes` assumed).
3. Whether the options dialog (and thus *Save*) is reachable from the army
   screen in state 3.
4. Behaviour when the slot name contains characters that are invalid in file
   names (an empty slot name is assumed).
5. Whether a database error inside a save step leads to a negative
   confirmation or leaves the "...SAVING..." box up until the 180 s timeout.
6. Whether the options dialog stays open after a successful save.
7. Network campaign: how the other clients rejoin a loaded campaign.
