# Army screen and unit technology

Tech points, unit tech levels and the army screen where unit types are
upgraded. Unit statistics per level are the rows of `Unit Types`
([data-model.md](data-model.md)); a type at level L uses the row with
`unitTechLevel` = L.

Status: derived from studying the original program. Items marked *(?)* are
not fully confirmed.

## 1. Tech levels

- Every unit type has rows for levels 0 … k (shipped: 45 types have only
  level 0, Commando / Trooper / Grenadier 0–5, 14 types 0–10).
- `costToNextTL` of a row = tech points to go from that row's level to the
  next; 0 on the last row = maximum level.
- Levels are kept **per player and per unit type**. In the campaign they last
  for the whole campaign (also across colonies) and are saved with it. A type
  without an entry is at level 0.
- Upgrading a type at level L costs `costToNextTL` of row L and gives L + 1.
  Example Trooper: 5, 10, 15, 20, 50, then maximum.
- Displayed levels are 1-based ("Next tech: L + 2" for a type at level L).
- Automated units are never upgradable.

## 2. Gaining tech points

### 2.1 Campaign

- **Only by winning a battle:** the tech points found (sum of `TechReward` of
  the tech buildings the human side holds at the end, battle-flow.md 5.2) are
  added to every player's pool. Nothing on a defeat; nothing for city battles.
- No conversion: the raw sum is added (small / medium / large Tech Center
  250 / 500 / 1000; Aztec ruins and the pyramid up to 6000).
- A new campaign starts with 0. `Province List.Tech` is only informative (it
  matches the tech centres on the province's maps).
- The **Research Center** (`H_Center_Laboratory`, type 135) does not convert
  anything: owning one is what allows upgrades on the strategic army screen.

### 2.2 Skirmish

Once at the start: v = the scenario's `One Off Deployment`; take the first
`Tech Ratio` row with (`MinRange` = −1 or `MinRange` ≤ v) and (`MaxRange` = −1
or v ≤ `MaxRange`); every player's pool = v × `Ratio` (1 if no row matches).
Examples: 300 → 300, 500 → 2500, 1000 → 8000. All listed types start at level
0. This is the only use of `Tech Ratio`. Unspent points are lost.

### 2.3 Death match

No army screen; every available type is at its highest level.

## 3. Which types are listed

- Campaign: the player's deployable types ([city-building.md](city-building.md)
  8): owned prerequisite building, not a tower unit, not a convoy. A type keeps
  its level when its building is lost and shows it again when it is rebuilt.
- Skirmish: the scenario's `One Off Units Available` types of the human race.
- Automated list: the usable automated types.

## 4. The army screen

### 4.1 Variants

| Variant | Opened from | Upgrades | Bottom button | On close |
|---|---|---|---|---|
| strategic | ARMY on the city screen | only when the player owns a Research Center | BACK (Esc) | apply the pending upgrades |
| skirmish | before a skirmish battle | yes | CONTINUE (Enter) | apply, then load the battle |
| deployment | ARMY in the deployment / reinforcement view | no (browsing) | BACK (Esc) | select the same type in the deployment list |

Strategic: without a Research Center a message box with string 850 comes
first, then the screen opens without the UPGRADE button and without the
information text.

### 4.2 Layout

Panel art frame **157** (588 × 451) at x = (screen w − 588) / 2,
y = (screen h − 451) / 2, but y = 0 when the screen height is below 768
(640 × 480 → (26, 0); 1024 × 768 → (218, 158)). Positions relative to the
panel:

| Control | Position | Size | Frames | Key |
|---|---|---|---|---|
| UPGRADE | (471, 400) | 110×17 | 162–165 | `T` (string 117) |
| CONTINUE (skirmish) | (471, 428) | 110×17 | 158–161 | Enter |
| BACK | (471, 428) | 110×17 | 168–171 | Esc |
| encyclopedia | (496, 309) | 60×55 | 172–175 | F1 |
| unit list: previous / up / down / next | (247, 280) / (269, 280) / (291, 280) / (313, 280) | 19×17 | 180 / 188 / 184 / 176 (+0…3) | |
| automated list: previous / up / down / next | (185, 426) / (207, 426) / (229, 426) / (251, 426) | 19×17 | same | |
| tech points | (474, 371)–(558, 388) | | | number, centred, white on black |
| information text | (300, 315) | 159×124 | | 4.5 |
| unit list | (8, 10) | 4 columns × 6 rows of 135×43 | | 4.3 |
| automated list | (10, 315) | 2 columns × 2 rows, row pitch 51 | | 4.4 |

Keys act on the active list (the last one clicked; the unit list at first):
Left / Right = previous / next entry, Up / Down = scroll one row.
"Previous" at the start of a row scrolls up first; "next" onto the start of a
row scrolls down. A click selects the cell (row × columns + x / 135).

### 4.3 Unit list cells

Cleared to palette 0. The 38 × 38 list icon of the type at (0, 2), drawn with
every second pixel (checkerboard) replaced by palette 0 when the type is at its
maximum level. Text at (40, 0), standard font, white: the name; then string
848 "Maximum level", or 851 "Next tech:" + (L + 2) and 852 "Cost:" +
`costToNextTL`. The selected cell has a one-pixel frame in palette 255
(135 × 42). The first entry is selected when the screen opens. Entries are in
unit-type order (campaign) or scenario order (skirmish).

### 4.4 Automated list cells

Like the unit cells, icon always stippled, text: name and 848.

### 4.5 Information text

For the selected unit type, with `cur` = the row at its level and `nxt` = the
row at level + 1 (or `cur` at the maximum): the name; 848 at the maximum;
then one line per statistic showing the **next** level's value, in red when it
differs from the current one:

| Label | Value | Red when |
|---|---|---|
| 24 ARMOR | `Armor` | differs |
| 25 ACTION POINTS | `actionPoints` | differs |
| 27 AVG DAMAGE | `damageBase` + `damageRandom` div 2 | differs |
| 235 DAMAGE AA (only for AA units) | `DamageAA` | differs |
| 29 RANGE | `weaponRange` | differs |
| 28 ROF | `actionPoints` div `actionPointsShoot` | `actionPointsShoot` differs |

For an automated unit: name, 848, then ARMOR, AVG DAMAGE and AREA from
`Automated Units Infos` when present.

### 4.6 Upgrade

Local and immediate: available = pool − points spent in this visit. Not
enough → message box 849. Otherwise the cost is spent, the type's level rises
by one at once, the cell and the information text are redrawn, sound
`CASHOUT`. UPGRADE is enabled when the selected type is not at its maximum.
The pending upgrades are sent when the screen closes; the server subtracts the
costs and sets the levels.

## 5. Level used in battle

Deployment and reinforcements place each human unit at the player's level for
its type. AI units use their scenario `unitTL`; AI reinforcements the lowest
level.

## Implementation notes (OpenDay)

Implemented in `game/campaign/campaign.*` (levels, upgrade rules) and
`app/campaign_army.cpp` (screen).

- The skirmish army screen and tech allotment (2.2) are used before skirmish
  battles; death match is not implemented.
- The encyclopedia button is disabled.
