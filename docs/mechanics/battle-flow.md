# Battle flow: setup, objectives, end of battle

How a tactical battle is set up, how its turns are counted, when and how the
objective decides the battle, how reinforcements arrive, and what is reported
when the battle ends.

Prerequisites: [data-model.md](data-model.md) (tables and columns),
[tactical-movement.md](tactical-movement.md) (player order, AP refill, move
checks) and [tactical-combat.md](tactical-combat.md) (shots, destruction).
The decisions of the computer player (which unit attacks what, scripted
orders, when automated units are called) are described in
`tactical-ai.md` and are only mentioned here where they touch the flow.

Status: the rules below come from studying the program's behavior and were
cross-checked against the shipped scenarios. Items marked *(?)* are not fully
confirmed.

## Terms

| Term | Meaning |
|---|---|
| T | the battle's turn counter (`Tactic IO.Current Turn`), see 1.2 |
| `turn_qty` | `Scenario Info.turn_qty`, the turn limit |
| N | `Objective.TurnNumber` |
| ID list | the object ids in `Objectives ID List` (unit `ID`s or building `id`s) |
| zone | the tiles in `Objectives Pos List` |
| human unit / building | owned by a player slot 1–4 (`playerOwner` ≠ 0) |
| AI unit | owned by slot 0 |
| "exists" | the object is still on the map (not destroyed) |
| decided / success / loser | the three outputs of the objective check (section 2) |

## 1. Battle setup and turn order

### 1.1 What is on the map at the start

- Everything in the scenario tables is placed when the battle is loaded:
  terrain, roads, walls, mines, ruins, buildings (`Buildings On Map`) and
  **all** rows of `Units On Map`. A unit row whose tile is not valid for it
  (deployment variant of the tile check, tactical-movement.md 1.5) is a load
  error.
- `Units On Map.Turn` and `.Probability` do **not** make units appear later
  or randomly. Every listed unit is present from the start. Both columns
  belong to the unit's scripted order (`OrderID`):
  - `Probability` (0–100): at the start of every AI turn, for every unit on
    the map, a random number `r` in 0…99 is drawn; if `Probability < r` the
    unit's order is cleared to "no order" (and stays cleared). With 100 the
    order is never dropped. In the shipped data only units with an order have
    values below 100.
  - `Turn`: the **last turn of the order**: at the start of an AI turn the
    order is cleared when `Turn` < T; 99 means "for the whole battle"
    (tactical-ai.md 2.1–2.2).
- `HQ on Map` gives one tile per human slot (1–4). Only the **campaign**
  loads it into the battle and sends it to the clients (skirmish and death
  match ignore the table); the clients only store it. No tactical rule reads
  it; its use is the campaign city (HQ sites, campaign.md 2.1)
  *(?: no other reader found)*. The death-match HQ rules (2.10) do not use
  this table; they look for buildings of type "Headquarters"
  (`H_Center_HQ`, type 130).
- `Scenario Info.necessary_unit` (shipped values 1, 2 and 0) is loaded with
  `turn_qty` by client and server, but no rule reading it was found
  *(?)*. OpenDay ignores it.
- `Automated Units` rows are handed to the AI; a row is called in during the
  AI turn of the round with **T = `TurnArrival`** (exact equality,
  tactical-ai.md 6.2).

### 1.2 Deployment

1. **Zones.** For each human slot p = 1…4 the deployment zone is the set of
   `Deployment` tiles of player p. A tile that is listed for several players
   belongs to the lowest slot only; it is removed from the zones of the later
   slots.
2. **Budget.** Every human player gets a deployment budget (credits):
   - skirmish / death match: `One Off Deployment` × share;
   - campaign province battle: `One Off Deployment` × M(d) / 100, then ×
     share, where M(d) interpolates the colony's `Continent Info.DeployEasy`
     and `DeployHard` (campaign.md 4.2); battles on the player's city use the
     HQ and deployment centres instead (campaign.md 6.4).

   Whatever is not spent during deployment is lost when the battle starts.

   **Share** depends on the number of human players: 1 → 100 %, 2 → 75 %,
   3 → 65 %, 4 → 55 % (integer arithmetic, `value × share / 100`).
3. Each player sends a deployment request (a list of unit type, tech level and
   tile). The server processes the entries in order: an entry is placed only
   if its `Cost` is not above the player's remaining budget and the tile is
   valid (inside the zone, tile check of tactical-movement.md 1.5). A placed
   unit is paid for (budget − cost) and faces the map center
   (`size / 2`, `size / 2`). Entries that fail are skipped.
4. When every human player has accepted his deployment, the battle starts
   (1.3). Placed units and changes are broadcast to the other players.

During deployment the turn counter is 0, so no turn-based objective rule
(which all require T > 0) can fire.

### 1.3 Start of the battle and turn order

At the start every unit of every player gets its AP (tactical-movement.md 4),
the turn counter is set to **T = 1** and a "current player" pointer is set,
then the normal "begin next turn" step runs.

- **Participants:** the connected human slots plus slot 0, sorted
  (0, 1, 2, 3, 4). Slot 0 really plays only if the mode has a computer
  player: **campaign, counter-attack and skirmish have one; death match has
  none.** When slot 0 has no computer player, its place in the cycle is
  skipped.
- **Begin next turn** (also used after every end of turn):
  1. If the objective check says the battle is decided, the battle ends
     (section 3) and nothing else happens.
  2. The current player pointer moves to the next participant in the sorted
     cycle (after the last one it wraps to 0). If it lands on 0 and there is
     no computer player, the round counter step below is applied and the
     pointer moves on once more.
  3. The new current player starts his turn: tech-building ownership is
     re-evaluated (5.2), a human player's reinforcement resource grows by his
     per-turn income (4.2), his units get fresh AP, and he receives his
     "your turn" message (with T and the number of turns left,
     `turn_qty − T + 1`) while the others get the "wait" message. For slot 0
     the computer player is told to play.
- **Round counter step:** if the current player is slot 0, T increases by 1.
  It is applied at the end of slot 0's turn (and in step 2 above when slot 0
  is skipped). So one value of T covers one full round, and slot 0 always
  moves **last** in a round.
- **Who moves first:**
  - campaign, counter-attack, skirmish: the pointer starts at 0, so the
    **lowest human slot moves first** in turn 1; the computer player moves
    after all humans, then T becomes 2.
  - death match: the pointer starts at a random slot, 1 or 2 (50 % each),
    and the turn passes to the *next* participant. Starting pointer 1 →
    player 2 moves first in turn 1. Starting pointer 2 → the pointer wraps
    over the skipped slot 0, which already increases T, so player 1 moves
    first and the battle effectively starts in turn 2 (one round less before
    the turn limit; a quirk of the original).
- A human ends his turn with the end-turn request; the computer player ends
  its turn by sending the same request internally (after placing
  reinforcements, 4.1). Both lead to: round counter step, then "begin next
  turn".
- A battle restored from a save resumes at the saved current player and turn,
  without moving the pointer and without the objective check in step 1, and
  without the start-of-turn effects of step 3 (no income, no AP refill, no
  tech-building re-evaluation); see [savegame.md](savegame.md) 3.4.

## 2. Objectives

### 2.1 The objective check

The scenario's single `Objective` row selects one rule. The check takes the
current T and yields:

- **decided** (the battle is over),
- **success**: true if the human side (the scenario's "player" side) has won,
- **loser**: a player slot, only used by the death-match kinds; 5 = none.

**When it runs:** at the beginning of every turn change (1.3 step 1), after
every processed move order (including group orders and the reaction fire
they trigger) and after shots *(?: also after automated-unit actions)*, after the cheat win/lose requests, and whenever the
computer player reports. As soon as it returns "decided" the battle ends.
Because the check also runs at the start of a turn, a condition that is
already true when a turn begins ends the battle before that player can act.

All kinds except the counter-attack kind end with these **common rules**,
applied only if the kind's own rule did not decide yet:

1. "cheat win" flag set → decided, success, loser = 5;
2. "cheat lose" flag set (also used for **surrender**, see 3.1) → decided,
   failure, loser = 5;
3. **turn limit:** if T > 0 and **T > `turn_qty`** → decided, failure (the
   loser code is left as computed).

With T starting at 1 and counting rounds, the human side gets exactly
`turn_qty` full rounds; the limit fires at the first check in round
`turn_qty + 1`.

In the rules below "all listed objects" means every id in the ID list; an id
counts as gone when neither a unit nor a building with that id exists.

### 2.2 Kind 0 — Destroy

- **Win** (decided, success): every object in the ID list is gone **and** no
  road and no mine is left on any zone tile.
- Otherwise not decided; the common rules apply (turn limit → loss).
- The zone is used for things that have no id: mine fields and road/bridge
  sections. Walls and other objects on zone tiles do not count.

Shipped examples: `s1-2-1` (two Shubblar units to destroy, two AI
units in the ID list), `s1-5-1` (a mine field to clear, 18 zone
tiles, all mines), `s4-17-1` (bridge sections to destroy, 18 zone
tiles, all road tiles). N (10–20) is not used.

### 2.3 Kind 1 — Hold Position

State kept by the objective: a **hold counter** (starts at 0) and the
**last held turn** (starts at "none").

1. The zone is **held** if at least one zone tile carries a human unit (any
   human slot).
2. If held and T differs from the last held turn: hold counter + 1, last held
   turn = T. (So the counter grows at most once per round, on the first check
   of that round that finds the zone held.)
3. If not held and T ≥ last held turn + 2: hold counter = 0, last held turn
   = none. (An empty zone during one round is tolerated; it must stay empty
   through a whole further round before the counter is lost.)
4. **Win:** hold counter > N.
5. **Loss (too late):** if not decided and T > 0 and
   `turn_qty − N + 1 < T − hold counter`.
6. Common rules.

Example: N = 4. A unit reaches the zone in round k (counter 1); at the start
of rounds k+1 … k+4 the counter becomes 2 … 5 > 4, so the battle is won at the
start of round k+4, i.e. after the zone has been held for 4 turn changes.

Shipped examples: `s2-6-1` ("Hold position beside the building for 4
turns", N = 4, 3 zone tiles, `turn_qty` 12), `s4-12-1` ("... for 5 turns",
N = 5).

### 2.4 Kind 2 — Escort

The ID list holds the escorted units, the zone their destination.

- **Win:** one of the listed units exists and stands on a zone tile (its
  position tile). One arrival is enough.
- **Loss:** none of the listed units exists any more.
- Otherwise common rules (turn limit → loss).

The escorted units are owned by slot 0 in the shipped data (`s2-3-1`: three
Freighters, `s4-10-1`: three Trucks "into the city"); they are moved by the
computer player's scripted orders, and the human side has to keep them
alive. The owner does not matter for the rule.

### 2.5 Kind 3 — Protect (zone)

The zone has to be free of enemies when the mission ends.

- **Win:** T > `turn_qty` (checked first).
- **Loss:** only while **T = `turn_qty`** (the last round): if at any check
  an AI unit stands on a zone tile → decided, failure.
- Otherwise not decided.

Since the check also runs at the beginning of the last round, the zone has
to be clear when the last round starts and stay clear during it. N is not
used. Shipped examples: `s2-5-1` (81 zone tiles, `turn_qty` 10), `s4-16-1`
(812 zone tiles).

### 2.6 Kind 4 — Intercept

The ID list holds the enemy units to stop, the zone the area they must not
reach.

- **Loss:** a listed unit exists and stands on a zone tile.
- **Win:** none of the listed units exists any more.
- Otherwise common rules (turn limit → loss: the target was not destroyed in
  time).

Shipped examples: `s2-4-1` ("Destroy the Junk Wagon before it reaches the
red zone", one AI unit, 7 zone tiles at the map edge), `s1-4-1` (the same id
listed three times; duplicates are harmless).

### 2.7 Kind 5 — Keep

- **Loss:** any listed object is gone.
- **Win:** nothing lost and T > `turn_qty`.

The turn limit is a *win* here. Shipped examples: `s2-9-1` ("Protect the
building from destruction for 10 turns", one building owned by slot 0),
`s4-7-1` (ruins to protect).

### 2.8 Kind 6 — Destroy All Except

The ID list holds the exempt objects.

- **Loss:** any exempt object is gone.
- **Win:** no exempt object is gone and there is no AI unit left except the
  exempt ones. Buildings do not count.
- Otherwise common rules (turn limit → loss).

So destroying an exempt object loses the battle. Shipped examples: `s1-6-1`
and the capital maps `s<n>-1-1` (no ids: destroy every enemy unit),
`s3-5-1` (destroy every unit but the towers: ten Crystal Towers exempt;
destroying one of them ends the battle as a defeat). N is not used.

### 2.9 Kind 7 — Counter-attack

Not stored in any shipped `Objective` row; the program installs it itself for
the counter-attack battle of the campaign (section 4.3), with N = 99. The
common rules are **not** used (no turn limit, no cheat flags).

- **Win:** there is no AI unit left.
- **Loss:** there is no human-owned building left (any building type).
- **Win:** T ≥ 1 and T ≥ N (with N = 99 practically never).

### 2.10 Kinds 8–10 — Death match

Two human players (slots 1 and 2), no computer player. "Loser" names the
slot that lost; "success" stays false for these rules (the death-match
result message ignores it).

| Kind | Name (lookup) | Files | Rule |
|---|---|---|---|
| 8 | 2 cities | `dmc*` | Player 1 has no "Headquarters" building → loser 1. Player 2 has none → loser 2 (if both, 2). Then the common rules: at the turn limit decided with loser 5 (the kind presets loser 5 before its own test), shown as a draw with the scores (5.7). |
| 9 | city + alien | `dma*` | Player 1 (the city, owner of all buildings) has no Headquarters → loser 1. Otherwise, if T > 0 and T > `turn_qty` → loser 2 (the city held out). |
| 10 | slug fest | `dms*` | Player 1 has no unit → loser 1; otherwise player 2 has no unit → loser 2. At the turn limit: decided, loser 5 (preset as in kind 8). There is no server-side winner by points: both players see string 521 and the RANK column (score, 5.4), and the reader compares the numbers (5.7). |

"Has no Headquarters" means no building of type 130 owned by that player.
N is not used. Briefings agree: `dma2-10` hold the HQ for 10 turns
(`turn_qty` 10), `dmc3-6` destroy the enemy HQ, `dms1-4`
"Destroy as many enemy units as you can. The winner will be the one with the
most points".

## 3. Other end conditions

### 3.1 Surrender and cheats

- **Surrender** (in-game options, string 818 confirmation; only in the
  tactical view during your own turn) sends the cheat-lose request: the
  battle ends at once as a failure (loser 5). Details and the effect per
  mode: 5.6.
- With cheats enabled, Tab+W / Tab+L send the win / lose requests (same
  flags). The requesting player's slot is ignored: the loser code is always
  5. After either request the server runs the objective check immediately
  and ends the battle if it is decided. Kind 7 ignores both flags.

### 3.2 No generic end rule

There is **no** rule outside the objective: losing all units, losing the HQ
(outside death match) or the AI losing all units does not end a battle by
itself unless the active kind says so. For example in a Destroy battle in
which the human side has lost everything, play continues until the turn
limit. *(OpenDay may add an early-out; the result would be the same.)*

### 3.3 A player leaving

If a human player leaves during the battle, his slot leaves the cycle; if it
was his turn, the turn passes on; the result is not changed. In a death
match a player leaving during the battle ends it at once with loser code
**1**, whoever left (5.7).

## 4. Reinforcements and counter-attack

### 4.1 Scripted reinforcements (`Reinforcement`)

- Only in modes with a computer player. Rows are owned by slot 0.
- **When:** at the end of the computer player's turn (after it has finished
  acting, before the round counter step) the server walks the pending rows:
  every row with `turn ≤ T` is placed.
- **Where:** at (`posX`, `posY`) with the deployment variant of the tile check
  (tactical-movement.md 1.5). If the tile is not usable (occupied, wrong
  terrain), the row **stays pending** and is tried again at the end of the
  next AI turn; it is not moved to another tile.
- A placed row is removed from the pending list. The new units appear for all
  clients in one update message.
- **Order:** if the row's `order` flag is true, the new unit gets an order
  derived from the objective kind (Destroy with an ID list → escort the
  listed ids; Destroy without ids, Hold Position, Protect → patrol the zone;
  Escort, Keep → attack the listed ids; Intercept → escort the listed ids;
  others → no order), with order turn 99 and probability 100. The order
  meaning is AI business *(?, see tactical-ai.md)*.
- So a row with `turn` = k is placed at the end of round k and first acts in
  round k+1. Shipped turns are 1–12.

### 4.2 Bought reinforcements (human players)

Each human player has a reinforcement resource. At the start of each of his
turns it grows by:

- skirmish: `ReinfRes` × share (share as in 1.2);
- campaign province battle: `ReinfRes` + `ReinfRes` × 2 × n / 100, where n
  is the number of deployment centres (`H_Center_Deployment1…4`, any level)
  the player owns **in his city**, then × M(d) / 100 and × share
  (campaign.md 4.2); city battles: campaign.md 6.4.

He can spend it with a reinforcement request in his own turn (same list
format and rules as deployment: cost ≤ remaining resource, zone tile,
facing the map center). Units bought this way have no AP until his next
turn.

### 4.3 Counter-attack

After **every** won campaign battle whose scenario has
`Scenario Info.CounterAttack` set, the aliens strike back at the player's
**city**: a separate battle on the city map with objective kind 7 (2.9). The
attackers are the won scenario's `Counter Attack` rows, placed by the rules of
campaign.md 6.3 in a band 12–14 tiles from the player's zones. Losing it ends
the campaign. The details are in [campaign.md](campaign.md) 6.

### 4.4 Automated units

Only the arrival timing belongs here: a scenario `Automated Units` row is
called by the computer player in phase 6 of its turn when `TurnArrival`
equals the turn counter `T` **exactly** (rows whose turn has passed are
never called), with p1 = (`StartX`, `StartY`) and p2 = (`DestX`, `DestY`),
no budget and no prerequisite (tactical-ai.md 6.2). Human players call
automated units from the tactical screen at any time in their own turn,
paid from the tactical resource. Usable lists, anti-air fire, effects and
the objective re-check after each call are described in
[automated-units.md](automated-units.md).

## 5. Battle result

### 5.1 Result message

**When:** the battle end runs only while the battle is in the "running"
state. It runs the objective check once more, stores success and loser
code, enters the "over" state (no further requests are accepted) and sends
**one** result message to all clients. The message is built once; the
statistics and score are taken at this moment.

**Common part** (`CGoAftermathMsg`), in this order; every field is an
unsigned 16-bit value unless noted (so counters and scores are taken modulo
65 536 and printed as unsigned decimals):

| # | Field | Value |
|---|---|---|
| 1 | success | 0 / 1; human side won. Death match: forced to 1. |
| 2 | n | number of human players in the battle (the server's player list) |
| 3 | n × units destroyed | total of the player's "units destroyed" counter (5.3), slots in order 1…n |
| 4 | n × buildings destroyed | total "buildings destroyed" |
| 5 | n × units lost | total "units lost" |
| 6 | n × buildings lost | total "buildings lost" |
| 7 | n × score | 5.4 |
| 8 | tech points found | sum of `TechReward` of the tech buildings held at the end (5.2; the held set is refreshed first). Computed in every mode; only the campaign report shows it. |

There is no row for the computer player (slot 0): it has no statistics
record.

**Mode-specific tail:**

| Message | Extra field after the common part |
|---|---|
| `CGoAftermathSkirmishMsg` | none |
| `CGoAftermathCounterAttMsg` | none |
| `CGoAftermathCampaignMsg` | 16-bit **end-of-campaign** flag: set for the battle for colony 4's `AlienCity` (campaign.md 4.3, 7) |
| `CGoAftermathDeathMatchMsg` | 32-bit **loser code** (5.7) |

**Client handling:**

- skirmish and death match: the battle report opens at once; afterwards the
  client returns to the front end;
- campaign: the client stores success and the end-of-campaign flag;
- counter-attack: the client sets the end-of-campaign flag to **not
  success** (a lost counter-attack ends the campaign);
- the common part is copied into the report (tactical-interface.md 9).

The report shows `Scenario Info.Aftermath*` (in the selected language) on a
campaign success, generic texts otherwise (tactical-interface.md 9.2), and
in the campaign the number of new technologies. The campaign then pays
`Province List.Credits` × C(d) / 100 and adds the tech points found
(campaign.md 4.3, 5); `Scenario Info.Credits` and `Province List.Tech` are
not used.

### 5.2 Tech buildings

A building with `TechReward` ≥ 1 counts as **held** while a human unit
stands on any tile of the one-tile ring around its footprint (the eight
neighbors of a 1 × 1 building; diagonals included). The held set is updated
at the start of the battle, at the start of every turn, after moves and
attacks and when the result is computed; a destroyed building
leaves the set. Changes are sent to the clients as a tech record
(`tactic_mmsg_tech`). The result message uses the set as it is at the end.

### 5.3 Battle statistics (counters)

Every **human** player of the battle has a statistics record with four
**per-type counters** (a map type id → count, missing entries count 0):

| Counter | Key | Meaning |
|---|---|---|
| units destroyed (U-D) | unit type id | units this player destroyed |
| units lost (U-L) | unit type id | own units lost |
| buildings destroyed (B-D) | building type id | buildings this player destroyed |
| buildings lost (B-L) | building type id | own buildings lost |

The key is the **type id only**; all tech levels of a type share one
counter. The per-type values are used for the score (5.4) and are saved with
a battle (savegame.md 2.4); only the four **totals** (sum over all types)
are sent in the result message and added to the campaign (5.8). The
computer player (slot 0) has no record.

**Who gets the kill.** Every destruction of a unit or a building by damage
carries an **attacker slot**:

| Damage source | Attacker slot |
|---|---|
| a shot (normal shot, reaction fire, move-to-fire), including every tile of its area damage (own units in the blast too) | the shooter's owner |
| a mine explosion (also when an automated drop unit triggers it) | the **mine's owner** |
| an automated "destroy" unit (bomber etc.) | the automated unit's owner |
| a bridge collapsing under a unit | the attacker of the damage that broke the bridge |

**Unit destroyed** (owner o, attacker a):

- campaign, counter-attack, skirmish: if o ≠ 0 → U-L[o] + 1; if a ≠ 0 →
  U-D[a] + 1. There is **no same-side check**: a player who destroys his own
  unit (area damage, his own mine) gets both one loss and one kill. Kills by
  the computer player count only as the victim's loss, losses of the
  computer player only as the attacker's kill.
- death match: U-L[o] + 1 always; U-D[a] + 1 only if a ≠ o (friendly kills
  are not credited).

**Building destroyed** (owner o, attacker a), all modes: if o ≠ 0 →
B-L[o] + 1; if a ≠ 0 → B-D[a] + 1 (no same-side check, also in death
match). In the counter-attack and in death match the owner's deployable unit
and automated-unit lists are then recomputed and sent to him (campaign.md
6.4).

**Not counted:** walls, roads and bridges, mines, ruins and terrain changes;
automated (air) units shot down; units that disappear without being
destroyed by damage *(?)*; units on a collapsing building *(?: they are only
named in the event message; no counter update was found)*. Towers in city
battles are units (campaign.md 6.1) and count as units.

### 5.4 Score (the RANK number)

The score is computed once per human player when the result message is
built, with the same rule in every mode. It uses the global type tables:
`Unit Types` (one row per type **and tech level**) and `Building Types`.

**Intended formula** (what the program's structure aims at):

    score = Σ over the rows b of Building Types:
                B-D[type(b)] × Cost(b) − B-L[type(b)] × Cost(b)
          + Σ over the rows u of Unit Types:
                2 × U-D[type(u)] × Cost(u) − U-L[type(u)] × Cost(u)

(`Cost` of the unit row is used at tech level 0, i.e. the plain `Cost`
column.) Kills count double, losses and buildings once.

**Actual result of the original (a bug):** in both sums the subtracted term
reuses the first counter instead of the loss counter. The visible score is
therefore

    score = Σ over the rows u of Unit Types:  U-D[type(u)] × Cost(u)

- buildings destroyed, buildings lost and units lost have **no effect**;
- the score is never negative;
- because a type has one `Unit Types` row per tech level (all with the same
  `Cost`), a kill of type t is worth `Cost(t) × (number of rows of t)`.
  Examples from the shipped data: alien types have one row (Shocktrooper 20,
  alien main tank 100, Worm 250 per kill); human types have 6 or 11 rows
  (Trooper 5 × 6 = 30, Commando 10 × 6 = 60, Buggy 20 × 11 = 220, human main
  tank 50 × 11 = 550, Chopper 120 × 11 = 1320 per kill); towers and convoy
  types one row;
- friendly kills (outside death match) raise the score.

The value is sent as an unsigned 16-bit number (modulo 65 536).

**OpenDay** uses the **intended** formula, with one term per *type* (not per
`Unit Types` row, i.e. the plain `Cost` of the type at tech level 0) and
clamped to 0…65 535 for display; this is a deliberate fix, to be listed in
the implementation notes. The original's value can be reproduced from the
same counters if ever needed.

**No ranking:** the RANK column prints this number as it is. Rows are in
slot order; nothing is sorted and no "1st / 2nd" is computed anywhere.

### 5.5 What the report shows

The report (tactical-interface.md 9.2) shows one row per human player in
slot order (no computer row): units / buildings destroyed, units /
buildings lost and the score under the heading RANK. The rows are not
sorted and no place is computed. The tech points found are printed only by
the campaign report on success. What the campaign keeps afterwards is
described in 5.8.

### 5.6 Surrender

- *Surrender* exists only in the in-game options of the tactical view during
  the player's own turn (interface.md, "In-game options dialog"). After the
  confirmation (string 818) the client sends the **cheat-lose request**
  (`CCheatLoseScenMsg`); there is no separate surrender message and no
  cheat gate on this path.
- The server sets the objective's lose flag (loser code 5, the requesting
  slot is ignored), runs the objective check immediately and ends the battle
  (success 0, loser 5).
- Effect by mode:
  - skirmish: the battle is lost for every player (string 520);
  - campaign province battle: lost — the failure path of campaign.md 4.3
    (variant flip); the battle statistics are **not** added;
  - city battles (first city attack, counter-attack): their objective is
    kind 7, whose end test ignores the cheat flags (2.9), so surrender
    appears to have **no effect** there *(?: not confirmed at run time; if
    the flag were honoured the result would be a lost city battle and the
    end of the campaign, campaign.md 6.7)*;
  - death match: success is forced to 1 and loser 5 is sent, so **both**
    players see string 521 (ranking) — the surrendering player is not
    marked as the loser.

### 5.7 Death-match loser codes

| Loser code | Set by | Text shown (client) |
|---|---|---|
| 1 | kind 8/9/10 rule (player 1 lost), or **any player leaving** | player 1: 520, player 2: 519 |
| 2 | kind 8/9/10 rule (player 2 lost), kind 9 at the turn limit | player 2: 520, player 1: 519 |
| 5 | turn limit of kinds 8 and 10, surrender, cheat win / lose | everyone: 521 |

Text rule of the client: loser 5 → string 521 ("battle ended, ranking for
the participating players"); loser = the local player's slot → 520; any
other value → 519. A turn-limit end of kinds 8 and 10 is therefore a draw in
which the RANK numbers (5.4) are the only comparison.

A player leaving a running death match sets the **win** flag with loser code
**1** and ends the battle at once, regardless of who left; so if player 1 is
the one who stays, he is told he lost. *(Original quirk; OpenDay should
make the remaining player the winner: loser = the slot that left.)*

### 5.8 Campaign statistics

When a campaign battle (province battle or first city attack) or a
counter-attack ends with **success**, for every human player:

- `Player Campaign.Units Destroyed` += U-D total, `Units Lost` += U-L total,
  `Buildings Destroyed` += B-D total, `Buildings Lost` += B-L total;
- `Tech` += tech points found (counter-attack: + 0).

Then the battle's statistics records are deleted. A **lost** battle adds
nothing (campaign.md 4.3; a lost counter-attack ends the campaign). The
score is not stored in the campaign.

The four campaign columns are saved and loaded, but no screen showing them
was found: there is no statistics screen in the strategic phase and no
string for one, and the end of the campaign shows only the closing video
*(?: not exhaustively searched)*.

## 6. Message classes

The battle runs in a server phase per mode: `CServerPhaseTactic` (base),
`CServerPhaseCampaignTactic`, `CServerPhaseCounterAttTactic`,
`CServerPhaseSkirTactic`, `CServerPhaseDeathTactic`. The objective is a
`CObjective` subclass per kind (`CObjDestroy`, `CObjHoldPos`, `CObjEscort`,
`CObjProtect`, `CObjIntercept`, `CObjKeepAlive`, `CObjDestroyAll`,
`CObjCAtt`, `CObjDeathA`/`B`/`C` for kinds 8/9/10); the clients learn the
kind, the ID list and the zone from `CScenarioObjectiveMsg` at the start.
Deployment uses `CGoDeployMsg`, `CReqDeployUnitsMsg`, `CDeployUnitMsg`,
`CDeployAcceptedMsg` / `CDeployFailedMsg`; bought reinforcements
`CReqReinfUnitsMsg` / `CReinfAcceptedMsg`. Turns use `CGoTacticMsg`,
`CGoTacticWaitMsg`, `CAItacticMsg`, `CRequestEndTactTurnMsg`. The cheat and
surrender requests are `CCheatWinScenMsg` / `CCheatLoseScenMsg`. The end of
battle is announced with a `CGoAftermathMsg` subclass:
`CGoAftermathCampaignMsg`, `CGoAftermathCounterAttMsg`,
`CGoAftermathSkirmishMsg`, `CGoAftermathDeathMatchMsg` (handled by
`CClientPhaseTactic`, shown e.g. by `CAftermathDeathInterface`); in the
campaign the client confirms with `CEndAftermathCampaignMsg`, after which the
strategic results phase (`CStratResultPhaseMsg`) follows.

## Notes for OpenDay

- Implement the objective as one function per kind with the common rules of
  2.1 applied after it, and call it at the points listed in 2.1.
- Keep the off-by-one details: T starts at 1, the limit is `T > turn_qty`,
  Protect checks only at `T = turn_qty`, Hold Position counts once per round
  with a one-round grace.
- Kind 6's "destroying an exempt object loses" and the death-match start
  quirk (1.3) are original behavior; keep them unless a deliberate fix is
  documented.
- Statistics (5.3): count exactly as described, including the missing
  same-side check outside death match.
- Score (5.4): use the intended formula (deliberate fix); the original's
  visible number is "value of the units destroyed" only.
- Death match, a player leaving (5.7): make the remaining player the winner
  (deliberate fix of the fixed loser code 1).

## Implementation notes (OpenDay)

Implemented in `game/battle/objectives.*` (the check) and
`game/battle/server.*` (turn order, reinforcements, end of battle). The
result travels to the clients in the `BattleOver` event (success, loser).

- `turn_qty` = 0 is treated as "no turn limit" (the original would end the
  battle at once).
- Deployment (1.2), bought reinforcements and the per-turn income (4.2),
  counter-attack battles (4.3, with the campaign), tech buildings (5.2) and
  the cheat requests are implemented. Not implemented yet: automated units,
  the score (5.4), saving and restoring a battle, surrender.
- Reinforcements use the lowest tech level of their type (the rows have no
  tech level column) and face the map center.
- The computer player only plays when at least one human slot takes part;
  otherwise slot 0 is played from the viewer like a human.

## Open questions

- `necessary_unit`: no reader found (it is reached through an accessor
  shared by many classes, so a reader could have been missed).
- `HQ on Map` in tactical battles: no reader found; presumably used by the
  campaign city only.
- Units destroyed by a collapsing building or removed without damage:
  confirm that no counter changes (5.3).
- Surrender in a city battle (kind 7 ignores the lose flag, 5.6): confirm at
  run time.
- Whether any screen reads the campaign statistics columns (5.8).
- (Answered: `Units On Map.Turn` is the last turn of the order and
  `TurnArrival` is compared for equality, 1.1 / tactical-ai.md; score
  formula and counter order, 5.1 / 5.4; death-match loser codes and the
  draw at the turn limit, 5.7.)
