// Tactical combat rules that do not change the state: sides, line of fire,
// target legality and hit chance. docs/mechanics/tactical-combat.md.
#pragma once

#include <vector>

#include "battle/battle_state.hpp"

namespace openday::game {

enum class Side { Same, Ally, Enemy };

// Game options that change combat (section 1.1 and 4.2).
struct CombatOptions {
    bool coop = true;      // human players are allies
    bool aiDamage = true;  // the difficulty factor scales AI damage (campaign, skirmish)
    int difficulty = 5;    // 0-10 *(?)*
    bool computerAi = true; // the computer player plays slot 0 (else slot 0 is played like a human)
    // Automated units as the original does them (automated-units.md 2.4,
    // 5.3): the AA candidates of a transport fire at both points, and a
    // DeployOnce type is lost by every player. Off: each pass has its own
    // candidates, and only the caller loses the type.
    bool faithfulAutomated = false;
};

// Relation of two units (section 1.1), convoys counting by race.
Side unitSide(const BattleUnit& a, const BattleUnit& b, bool coop);
// Relation of a player to a building's owner.
Side buildingSide(int player, const BattleBuilding& b, bool coop);

int chebyshev(Point a, Point b);
// Center tile of a unit's footprint.
Point centerTile(const BattleUnit& u);

// Grid line between two tiles, both ends included, in a -> b order. The
// same tiles are produced in both directions (section 1.4).
std::vector<Point> fireLine(Point a, Point b);

enum class ShotError { None, InvalidTarget, NoWeapon, NotEnoughAp, OutOfRange, TooClose, NoLineOfFire };

struct ShotCheck {
    ShotError error = ShotError::InvalidTarget;
    Point aim; // valid when error == None

    bool ok() const { return error == ShotError::None; }
};

// Checks whether a unit may fire at a tile now (section 1, in order).
ShotCheck checkShot(const BattleState& state, int shooter, Point target, bool coop);

// Base hit chance in percent for a shot at `aim`, after the Jammer
// modifier; > 100 means the roll cannot fail. Adjacent targets always hit
// and give 101.
int hitChance(const BattleState& state, int shooter, Point aim);

} // namespace openday::game
