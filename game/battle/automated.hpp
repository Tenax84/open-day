// Automated units (air strikes, drops, transports, bridge builders, mine
// detection, medics): the rules shared by the client and the server.
// docs/mechanics/automated-units.md.
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "battle/battle_state.hpp"
#include "battle/combat.hpp"

namespace openday::game {

// Outcome of an automated unit's flight (automated-units.md 5.1).
enum class AutomatedOutcome { Ok = 1, Damaged = 2, Destroyed = 3 };

// Relation of a player (the caller) to a unit, convoys counting by race
// (tactical-combat.md 1.1).
Side playerUnitSide(int player, const BattleUnit& unit, bool coop);

// Bridge sites of a map (city-building.md 4.3). `cls` gives the terrain
// class of a tile, Unknown outside the map.
std::vector<Point> findBridgeSites(int size, const std::function<TerrainClass(Point)>& cls);

// The road placement test for a bridge section (automated-units.md 3.2,
// build): a bridge site with nothing on it, riverside or deep water, deep
// water only next to a road.
bool bridgePlacementOk(const BattleState& state, Point tile);

// Number of target points: 2 for transports (source, destination), else 1.
int automatedPoints(const AutomatedType& type);

// The target test of automated-units.md 3.2 for `player`, cost test
// included. `source` is given for a transport's second point.
bool automatedTargetOk(const BattleState& state, int player, const AutomatedType& type, Point tile, bool coop,
                       const Point* source = nullptr);

// ANMSUNIT.FF entry of the clip (8.1): AUTOUNIT\<code><s><n>.SMK.
std::string automatedClipName(const AutomatedType& type, bool second, AutomatedOutcome outcome);

} // namespace openday::game
