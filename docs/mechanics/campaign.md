# Campaign: flow, colonies, province battles, rewards, counter-attacks

How the campaign is driven: the campaign states, how a colony starts, how a
province is chosen and its battle set up, what is awarded afterwards, when
the aliens strike back at the player's city, and how a colony and the
campaign end. The city-building rules are in
[city-building.md](city-building.md), unit upgrades in
[army-tech.md](army-tech.md), the screens in
[strategic-interface.md](strategic-interface.md). Table and column names are
explained in [data-model.md](data-model.md).

Status: derived from studying the original program and cross-checked against
the shipped colony and scenario databases. Items marked *(?)* are not fully
confirmed.

There are **no armies on the strategic map**. The campaign is a sequence of
tactical battles: the player picks an enemy province next to one he holds,
fights its scenario, and on a win the province becomes his. Between battles
he builds his city (which unlocks unit types and earns deployment points for
the battles fought in the city) and upgrades unit types with tech points.

## Terms

| Term | Meaning |
|---|---|
| colony | one of the four continents, database `C1` … `C4`; colony number c = `No Continent` + 1 |
| city | the player's capital map, scenario `s<c>-<PlayerCity>-1` (`PlayerCity` = 1 in every colony) |
| province scenario | the tactical map of a province: `s<c>-<p>-<v>`, v = the province's current variant (1 or 2; variant 1 in `SCEN1.ZIP`, 2 in `SCEN2.ZIP`) |
| d | campaign difficulty, an integer 0 … 10 chosen in the new-campaign menu (0 = easiest) |
| interpolation | `E + ((H − E) × d) div 10`, integer division truncating toward zero (H − E may be negative) |
| M(d) | deployment multiplier: interpolation of the colony's `Continent Info.DeployEasy` / `DeployHard` (`DeployAvg` is not used) |
| C(d) | credit multiplier: interpolation of GLOBAL `Difficulty.CreditEasy` / `CreditHard` |
| D(d) | damage factor: interpolation of GLOBAL `Difficulty.DamageEasy` / `DamageHard` |
| scale(v, m) | `(v × m) div 100` (the original computes `trunc(m × 0.01 × v)`, which gives the same results for all shipped values) |
| share(v) | `(v × S) div 100` with S = 100 / 75 / 65 / 55 for 1 / 2 / 3 / 4 human players (0 for a negative v) |

Shipped values: M = 130→70 (C1), 125→75 (C2), 130→90 (C3), 140→90 (C4);
C = 125→75; D = 70→130. The GLOBAL `Difficulty.Deploy*` columns and every
`…Avg` column are not used.

## 1. Campaign state

The campaign keeps, besides the colony and city, per human player:
industrial **credits**, **tech points**, statistics (units destroyed / lost,
buildings destroyed / lost) and a map *unit type → tech level*. Colony state
is per province: **captured** and **variant**.

### 1.1 States

| State | Name | What happens |
|---|---|---|
| 0 | first colony | video of the colony's `Intro` (`INTRO1.SMK`), then loading picture 7 ("aliens attacking the base camp") |
| 1 | first city attack | a battle on the city (section 6) |
| 2 | new colony | video `Intro<c>`, then loading picture 8 / 9 / 10 for colony 2 / 3 / 4 |
| 3 | strategic | the city screen, continent screen and army screen; ends when the player attacks a province |
| 4 | tactic | the province battle (section 4) |
| 5 | pre counter-attack | video `COUNTERA.SMK`, then loading picture 6 ("alien counter-attack") |
| 6 | counter-attack | a battle on the city (section 6) |
| 7 | strategic results | the province is taken (section 5) |
| 8, 9 | game won / lost | listed in `Campaign State Lookup` but never entered; the end is decided when the battle report closes (section 7) |

### 1.2 Transitions

```
new campaign: state 0 (Intro1) → 1 (first city attack) → 7 (results for the city province)
state 3 (strategic) → player attacks province P → 4 (battle on P)
state 4: lost → P flips to its other variant → 3
         won, P's scenario has CounterAttack → 5 (CounterA video) → 6 (city battle)
         won, no CounterAttack → 7
state 6: won → 7;  lost → campaign lost
state 7 (results for P): P = AlienCity → next colony: 2 (Intro<c>) → 3;  else → 3
colony 4: the battle for AlienCity ends the campaign, won or lost
```

A lost province battle costs nothing and can be repeated at once; the next
attack uses the **other map variant** of the province (1 ↔ 2).

## 2. Starting a colony

### 2.1 New campaign

1. One player record per human player (single player: slot 1), all counters
   0, tech 0, empty tech map.
2. Colony 1 is loaded; every player's credits are **set** to the colony's
   `StartCash`.
3. The city is loaded (`s1-1-1`).
4. Provinces are initialised in ascending order: the `PlayerCity` province is
   captured with variant 1; every other province is not captured and gets
   variant `rand() % 2 + 1` (one random draw per province). The `Captured` /
   `RandomChosen` values in the colony file are only the template.
5. HQs of unused slots are removed from the city: for each slot s = 1 … 4
   beyond the number of human players, the Headquarters owned by s is deleted
   together with its entry in the city's HQ-site list (`HQ on Map`). In single
   player only slot 1's HQ and HQ site remain.
6. State 0.

### 2.2 Later colonies

After the alien capital of colony c < 4 has been taken (section 5):

1. Colony c + 1 is loaded; credits are **set** to its `StartCash` (credits do
   not carry over).
2. Its city is loaded fresh (in colonies 2–4 the city maps contain only the
   HQs), so buildings, roads, walls and mines of the old city are lost.
3. Provinces initialised and unused HQs removed as in 2.1.
4. State 2 (intro video), then directly state 3. There is no first city attack
   in colonies 2–4 (their cities have no `Counter Attack` rows).

Tech points, unit tech levels and the statistics carry over between colonies.

## 3. Choosing a province

- Only a province that is **not captured** can be selected.
- It can be attacked when it is **adjacent** to a captured province: some
  captured province r has an `Adjacent Provinces` row (ProvinceNo = selected,
  AdjacenTo = r). (The shipped tables are symmetric.) Otherwise the continent
  screen says "Province is too far" (string 802).
- There is no special rule for the alien capital.
- The attack starts the province battle at once (single player). In a network
  game the host chooses and the other players confirm.

## 4. Province battle

### 4.1 Setup

1. Load the province scenario `s<c>-<p>-<v>`.
2. Deployment zones are merged for the number of human players n: n = 1 →
   zone 1 also gets the tiles of zones 2, 3, 4; n = 2 → zone 1 += zone 3,
   zone 2 += zone 4.
3. Units owned by absent human slots change owner: n = 1 → every unit whose
   owner is not 0 or 1 becomes slot 1; n = 2 → slot 3 → 1, slot 4 → 2;
   otherwise slot-4 units are removed.
4. For every player the **deployable unit types** and **usable automated
   units** are computed from **his buildings in the city** (not from the
   battle map), see [city-building.md](city-building.md) section 8. The
   scenario's `One Off Units Available` lists are not used in the campaign.
   Each deployable type is used at the player's tech level for it.
5. The computer player gets its automated-unit budget `AiAutoUnitRes`, call
   chance `AiAutoUnitChanceCall` and the types of `AI automated available`
   (tactical-ai.md).
6. AI damage is scaled by D(d) in every campaign battle (and only there; not in
   skirmish).

### 4.2 Deployment budget and income

- Deployment budget per player: `share(scale(One Off Deployment, M(d)))`.
  Whatever is not spent during deployment is **lost** when deployment ends.
- Per-turn income (bought reinforcements, battle-flow.md 4.2): with
  R = `ReinfRes` and n_dep = number of deployment centres (Deploy Mk1 … Mk4,
  any level) the player owns in his city:
  `share(scale(R + (R × 2 × n_dep) div 100, M(d)))`, added at the start of each
  of his turns.

Example: colony 1, d = 4: M = 130 + (−60 × 4) div 10 = 106; `s1-2-1` has
One Off Deployment 150 → budget 159 for one player.

### 4.3 End of the battle

The result message carries success, the statistics and the **tech points
found** (sum of `TechReward` of the tech buildings held at the end,
battle-flow.md 5.2), plus an **end-of-campaign flag**, set when the colony is
colony 4 and the province is its `AlienCity` (won or lost).

- **Won:** every player's statistics grow by the battle's counters and every
  player's tech pool by the tech points found. Then the counter-attack check
  (section 6) or the results (section 5).
- **Lost** (including the turn limit): the province's variant flips (1 ↔ 2), the
  battle is discarded (no statistics, no tech) and the campaign returns to the
  strategic state.

## 5. Strategic results (taking a province)

For the province P just won (for the first city attack, P = `PlayerCity`):

1. P becomes captured.
2. Every player's credits grow by `scale(Province List.Credits(P), C(d))`.
   `Scenario Info.Credits` and `Province List.Tech` are not awarded (the
   continent screen shows the province credits scaled the same way).
3. The continent screen shows the win view (P blinks, sound `PROVINCE`).
4. When it is closed: if P is the colony's `AlienCity` → next colony
   (2.2); otherwise → strategic state.

## 6. Battles on the city

Two battle kinds are fought on the **city map itself** (with everything the
player has built there), with objective kind 7 (battle-flow.md 2.9: won when no
AI unit is left, lost when the human side has no building left):

- **First city attack** (state 1): once, right after the intro of colony 1;
  the attackers are the city scenario's own `Counter Attack` rows.
- **Counter-attack** (states 5 → 6): after **every** won province battle whose
  scenario has `Scenario Info.CounterAttack` set (no random chance). The
  attackers are the `Counter Attack` rows of the province scenario just won.

### 6.1 Preparing the city

1. Objective kind 7 with N = 99 replaces any objective.
2. The attacker rows become the reinforcement list.
3. Deployment zones are generated (6.2); the attackers get positions (6.3);
   every attacker row with `turn` = 0 is placed at once (deployment tile
   check); the others stay as scripted reinforcements (battle-flow.md 4.1).
4. **Towers become units:** every building whose type has a
   `TransformIntoUnit` (Guard, Rocket, Laser, AA Tower) is replaced by a unit
   of that type, same owner and tile.
5. A terrain change list is started: before a tile is burned its original
   terrain is recorded.

### 6.2 Deployment zones on the city

A flag grid over the city map, all 0; each step only writes tiles still 0:

1. Terrain: mountains (class 4), crack (6) and unknown terrain are blocked.
2. Buildings over their whole footprint: a tower → "tower of player k", any
   other building → "building of player k" (k = 1 … 4; owner 0 leaves 0).
3. Walls and mines → blocked.
4. Roads → "zone of player k" by owner (owner 0 leaves 0).
5. Growth, for k = 1, 2, 3, 4 in this order, scanning rows top to bottom and
   columns left to right: a free tile becomes "zone of player k" when the
   window of radius 3 around it contains a tile "building of player k".
6. AI ring: a free tile becomes "AI zone" when some tile in the radius-14
   window is a zone of any player **and** (no tile in the radius-11 window is
   a zone, **or** the tile is in a map corner: x ∈ {0, 1, W−2, W−1} and
   y ∈ {0, 1, H−2, H−1}).

The window of radius r around (x, y) covers columns max(0, x−r) …
min(x+r, W−2) and rows max(0, y−r) … min(y+r, H−2): the last column and row
of the map are never inspected (an original quirk). "Zone" in step 6 means
any "zone of player k" tile (not buildings or towers).

Player k's deployment zone = its zone tiles; the AI's = the AI-zone tiles
(a band 12–14 tiles away from the human zones). The scenario's `Deployment`
table is not used.

### 6.3 Placing the attackers

1. Pool = the AI-zone tiles, shuffled (for each index i in order, swap
   element i with element `rand() % count`).
2. Attackers are split, in row order, into ground units and water units
   (movement boat or underwater); ground units are handled first.
3. Each unit takes the first pool tile where the deployment tile check for its
   type passes; that tile and its 8 neighbours leave the pool.
4. A unit that found no tile copies the tile of the first unit of its group
   that found one (it then waits in the reinforcement list until the tile is
   free).

### 6.4 Budget and income

For a human player: n_dep = his deployment centres of the **current colony's
level** (Mk1 in colony 1, Mk2 in colony 2, …), n_hq = his Headquarters.
`raw = ResProduction(Deploy Mk<c>) × n_dep + (n_hq > 0 ? ResProduction(HQ) : 0)`
(shipped: Mk1 100, Mk2 110, Mk3 120, Mk4 130, HQ 50);
**budget = share(scale(raw, M(d)))**. The per-turn income is the same formula,
recomputed every turn from the buildings still standing. `One Off
Deployment` and `ReinfRes` of the city map are not used.

Example: start of the campaign, d = 4, only the HQ: share(scale(50, 106)) = 53.

When a human building is destroyed during a city battle, that player's
deployable and automated lists are recomputed (losing the barracks removes
grenadiers from the reinforcement list).

### 6.5 Turn start

As usual (lowest human slot first, T = 1), except that at the start only the
AI's units and the humans' tower units have AP; the humans' other units get
AP when their turn begins.

### 6.6 After a won city battle

1. All ruins on the city are removed.
2. Surviving tower units turn back into their tower buildings (the type whose
   `TransformIntoUnit` is the unit's type), same owner and tile, full armor.
3. All units and the reinforcement list are removed.
4. Every remaining building and wall is repaired to its type's armor.
5. The terrain change list is replayed backwards: burned tiles get their
   original terrain back.

Destroyed buildings, walls, roads and exploded mines stay gone *(?: nothing
restores roads and mines)*. Then the statistics are added (no tech points)
and the results phase follows (section 5) for the province that triggered the
battle.

### 6.7 Losing a city battle

The report's end-of-campaign flag is "not success": a lost first city attack
or counter-attack **ends the campaign as lost** (`FH2LOSE.SMK`). The manual
says "if your Headquarters is destroyed, you need to restart the campaign";
in the program the rule is that no human building is left.

## 7. End of the campaign

When the battle report is closed:

- end-of-campaign flag set (colony 4, battle for `AlienCity`): video
  `FH2END.SMK` if the battle was won, `FH2LOSE.SMK` if it was lost, then the
  main menu. Losing that last battle therefore ends the campaign instead of
  allowing a retry (an original quirk);
- a lost city battle: `FH2LOSE.SMK`, main menu;
- otherwise the campaign continues.

`Continent Info.End` (`End1`, `End`) is loaded but never played.

## 8. Saving

The full description is in [savegame.md](savegame.md). In short:

- Only the campaign can be saved, only by the host, and only from the city
  and continent screens (state 3) and from the tactical and waiting views of
  a battle after deployment (states 1, 4, 6). States 0, 2, 5 and 7,
  deployment, reinforcement purchase and the battle report cannot be saved.
- The original saves the campaign record (colony, state, tactical province,
  the players' credits / tech / statistics / tech maps, difficulty, turn
  time), the terrain change list, the city scenario, the colony state and,
  during a province battle, the battle scenario with the battle's turn,
  current player and objective counters. The strategic phase also saves the
  "built in this phase" lists ([city-building.md](city-building.md) 6) so
  that recycling after loading still refunds the full cost.
- A game is resumed in states 1, 3, 4 and 6 (0 would restart the colony
  intro but cannot be saved). A battle resumes at the start of the saved
  current player's turn with the units' saved AP. A city battle gets a fresh
  kind-7 objective.
- Wrecks and rubble made during the battle, the `order` flag of pending
  reinforcements, the AI's private state and the random-number state are
  lost on reload (savegame.md 4).

## Notes for OpenDay

- The server side of the campaign is a state machine with these states; the
  client screens follow the original sequence (strategic-interface.md 9).
- The original sends the tech-level map to a client only when its army screen
  opens; a client that never opened it after loading would deploy at level 0.
  OpenDay fills in the campaign level itself when a battle starts.
- OpenDay's save format is its own (not the original `SAVEGAME\savegame.NN`).

## Implementation notes (OpenDay)

Implemented in `game/campaign/` (colony data, campaign state, city rules,
battle preparation, save files) and the campaign screens of `app/`.

- Single player only (one human player, share 100 %).
- Saving and loading use OpenDay's own text format in the user's settings
  folder; a game can be saved in the strategic state.
- Automated units are listed but cannot be called yet (as in skirmish).

## Open questions

- The value 10 sent with the start of every strategic phase (probably a time
  limit for network games) and the {3, 6, 9} choice of the multiplayer
  new-campaign menu.
- Whether destroyed roads and exploded mines of the city are ever restored.
- (Answered in savegame.md 1.2: saving is not offered in states 0, 2, 5 and
  7; loading such a state would start nothing.)
