#include "battle/objectives.hpp"

#include <algorithm>

namespace openday::game {
namespace {

constexpr int kHeadquarters = 130;

bool unitExists(const BattleState& s, int id) {
    return std::any_of(s.units().begin(), s.units().end(), [&](const BattleUnit& u) { return u.alive && u.id == id; });
}

bool objectExists(const BattleState& s, int id) {
    return unitExists(s, id) || std::any_of(s.buildings().begin(), s.buildings().end(),
                                            [&](const BattleBuilding& b) { return b.alive && b.id == id; });
}

bool inZone(const BattleState& s, Point p) {
    return std::any_of(s.objective.zone.begin(), s.objective.zone.end(),
                       [&](Point z) { return z.x == p.x && z.y == p.y; });
}

// A living unit with one of the listed ids stands in the zone.
bool listedUnitInZone(const BattleState& s) {
    for (const BattleUnit& u : s.units())
        if (u.alive && inZone(s, u.pos) &&
            std::find(s.objective.ids.begin(), s.objective.ids.end(), u.id) != s.objective.ids.end())
            return true;
    return false;
}

bool anyListedUnitExists(const BattleState& s) {
    return std::any_of(s.objective.ids.begin(), s.objective.ids.end(), [&](int id) { return unitExists(s, id); });
}

bool anyListedGone(const BattleState& s) {
    return std::any_of(s.objective.ids.begin(), s.objective.ids.end(), [&](int id) { return !objectExists(s, id); });
}

bool unitInZoneOwnedBy(const BattleState& s, bool human) {
    for (const BattleUnit& u : s.units())
        if (u.alive && (u.owner != 0) == human && inZone(s, u.pos)) return true;
    return false;
}

bool hasHeadquarters(const BattleState& s, int player) {
    return std::any_of(s.buildings().begin(), s.buildings().end(), [&](const BattleBuilding& b) {
        return b.alive && b.owner == player && b.type->id == kHeadquarters;
    });
}

bool hasUnits(const BattleState& s, int player) {
    return std::any_of(s.units().begin(), s.units().end(),
                       [&](const BattleUnit& u) { return u.alive && u.owner == player; });
}

ObjectiveResult win() { return {true, true, 5}; }
ObjectiveResult lose() { return {true, false, 5}; }
ObjectiveResult loser(int slot) { return {true, false, slot}; }

} // namespace

const char* objectiveName(int kind) {
    switch (kind) {
    case 0: return "Destroy";
    case 1: return "Hold Position";
    case 2: return "Escort";
    case 3: return "Protect";
    case 4: return "Intercept";
    case 5: return "Keep";
    case 6: return "Destroy All Except";
    case 7: return "Counter-attack";
    case 8: return "Death match: 2 cities";
    case 9: return "Death match: city + alien";
    case 10: return "Death match: slug fest";
    default: return "none";
    }
}

ObjectiveResult checkObjective(BattleState& s) {
    const int t = s.turn;
    const int n = s.objective.turns;
    const bool afterLimit = s.turnLimit > 0 && t > 0 && t > s.turnLimit; // 0 = no limit (OpenDay)
    ObjectiveResult r;

    switch (s.objective.kind) {
    case 0: { // Destroy: the listed objects, and every road and mine in the zone
        bool cleared = std::none_of(s.objective.ids.begin(), s.objective.ids.end(), [&](int id) { return objectExists(s, id); });
        for (const Point z : s.objective.zone)
            if (s.roadAt(z) >= 0 || s.mineAt(z) >= 0) cleared = false;
        if (cleared) r = win();
        break;
    }
    case 1: { // Hold Position
        const bool held = unitInZoneOwnedBy(s, true);
        if (held && t != s.lastHeldTurn) {
            ++s.holdCount;
            s.lastHeldTurn = t;
        } else if (!held && s.lastHeldTurn >= 0 && t >= s.lastHeldTurn + 2) {
            s.holdCount = 0;
            s.lastHeldTurn = -1;
        }
        if (s.holdCount > n) r = win();
        else if (t > 0 && s.turnLimit - n + 1 < t - s.holdCount) r = lose();
        break;
    }
    case 2: // Escort: one listed unit reaches the zone
        if (listedUnitInZone(s)) r = win();
        else if (!anyListedUnitExists(s)) r = lose();
        break;
    case 3: // Protect: no AI unit in the zone during the last round
        if (afterLimit) return win();
        if (t == s.turnLimit && unitInZoneOwnedBy(s, false)) return lose();
        return r;
    case 4: // Intercept
        if (listedUnitInZone(s)) r = lose();
        else if (!anyListedUnitExists(s)) r = win();
        break;
    case 5: // Keep: lose nothing until the turn limit
        if (anyListedGone(s)) return lose();
        if (afterLimit) return win();
        return r;
    case 6: { // Destroy All Except
        if (anyListedGone(s)) {
            r = lose();
            break;
        }
        const bool aiLeft = std::any_of(s.units().begin(), s.units().end(), [&](const BattleUnit& u) {
            return u.alive && u.owner == 0 &&
                   std::find(s.objective.ids.begin(), s.objective.ids.end(), u.id) == s.objective.ids.end();
        });
        if (!aiLeft) r = win();
        break;
    }
    case 7: { // Counter-attack: no common rules
        if (!hasUnits(s, 0)) return win();
        const bool humanBuilding = std::any_of(s.buildings().begin(), s.buildings().end(),
                                               [](const BattleBuilding& b) { return b.alive && b.owner != 0; });
        if (!humanBuilding) return lose();
        if (t >= 1 && t >= n) return win();
        return r;
    }
    case 8: { // Death match, two cities: a player without Headquarters loses
        const bool lost1 = !hasHeadquarters(s, 1), lost2 = !hasHeadquarters(s, 2);
        if (lost2) r = loser(2);
        else if (lost1) r = loser(1);
        break;
    }
    case 9: // Death match, city + alien
        if (!hasHeadquarters(s, 1)) r = loser(1);
        else if (afterLimit) r = loser(2);
        break;
    case 10: // Death match, slug fest
        if (!hasUnits(s, 1)) r = loser(1);
        else if (!hasUnits(s, 2)) r = loser(2);
        break;
    default:
        break;
    }
    if (r.decided) return r;
    // Common rules: the turn limit ends the battle as a failure.
    if (afterLimit) r.decided = true;
    return r;
}

} // namespace openday::game
