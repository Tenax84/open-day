// Messages between the battle clients and the server. Clients send
// requests; the server checks them, resolves them and answers with events,
// which every client applies to its copy of the state and plays as
// animations. The split follows the original's client-server design
// (docs/mechanics/tactical-movement.md section 7,
// docs/mechanics/tactical-combat.md section 8); the message layout is
// OpenDay's own.
#pragma once

#include <string>
#include <variant>
#include <vector>

#include "battle/battle_state.hpp"
#include "battle/movement.hpp"

namespace openday::game {

// --- Requests (client -> server) ---

struct UnitOrder {
    int unit = -1;
    std::vector<PathStep> path; // planned by the client; may be empty for shots
};

struct MoveRequest {
    int player = 0;
    std::vector<UnitOrder> orders; // up to 64
};

struct ShootRequest {
    int player = 0;
    std::vector<UnitOrder> orders; // shooters, each with an optional path to walk first
    Point target;
    bool autofire = false;
};

struct EndTurnRequest {
    int player = 0;
};

// Units bought and placed during deployment (battle-flow.md 1.2), or
// later in the player's own turn as bought reinforcements (4.2): those
// start without AP.
struct DeployEntry {
    int typeId = 0;
    int techLevel = -1; // -1 = lowest level of the type
    Point pos;
};

struct DeployRequest {
    int player = 0;
    std::vector<DeployEntry> entries;
};

// Takes back a unit this player placed during deployment, with a refund.
struct UndeployRequest {
    int player = 0;
    int unit = -1;
};

// The cheat win / lose requests (and surrender = lose), battle-flow.md 3.1.
struct CheatRequest {
    int player = 0;
    bool win = false;
};

// Calls an automated unit (automated-units.md 3.4): one target point, or
// for a transport the unit to pick up and the destination.
struct AutomatedRequest {
    int player = 0;
    int typeId = 0;
    std::vector<Point> points;
};

using Request =
    std::variant<MoveRequest, ShootRequest, EndTurnRequest, DeployRequest, UndeployRequest, CheatRequest, AutomatedRequest>;

// --- Events (server -> clients) ---

struct UnitMoved {
    int unit = -1;
    std::vector<Point> tiles; // tiles walked, start tile excluded
    int facing = 0;           // facing on arrival
    int ap = 0;               // currentAP afterwards
};

struct UnitShot {
    int unit = -1;
    int facing = 0;
    Point aim;    // the tile aimed at
    Point impact; // where the shot landed
    int ap = 0;   // currentAP afterwards
};

struct UnitDamaged {
    int unit = -1;
    int armor = 0;
    int ap = 0;
    int negAp = 0;
};

struct BuildingDamaged {
    int building = -1;
    int armor = 0;
};

struct WallDamaged {
    int wall = -1;
    int armor = 0;
};

// Who destroyed a unit or a building (battle-flow.md 5.3, "Who gets the
// kill"). This is the hook for the battle statistics: `attacker` is the
// credited player slot (-1 = unknown), `automatedType` the automated type
// when an automated "destroy" unit did it (-1 otherwise). Statistics are not
// counted yet.
struct DamageCause {
    int attacker = -1;
    int automatedType = -1;
};

struct UnitDestroyed {
    int unit = -1;
    std::string wreck{}; // RUINS.DBI name left on the tile, or empty
    DamageCause cause{};
};

struct BuildingDestroyed {
    int building = -1;
    std::vector<BattleRuin> rubble{};
    DamageCause cause{};
};

struct WallDestroyed {
    int wall = -1;
};

struct RoadDestroyed {
    int road = -1;
};

struct MineDestroyed {
    int mine = -1;
};

struct MineExploded {
    int mine = -1;
};

struct MinesRevealed {
    int player = 0;
    std::vector<int> mines;
};

struct TerrainBurned {
    Point tile;
    std::string terrain; // new TERRAINS.DBI name
};

struct TurnStarted {
    int player = 0;
    int turn = 0;
};

struct BattleOver {
    int turn = 0;
    bool success = false; // the human side won
    int loser = 5;        // death match: the slot that lost; 5 = none
    int techFound = 0;    // TechReward of the tech buildings held at the end
};

// A tech building became held or no longer held by the human side
// (battle-flow.md 5.2).
struct TechChanged {
    int building = -1;
    bool held = false;
};

// A unit placed during the battle (reinforcements).
struct UnitAdded {
    int id = 0;
    int typeId = 0;
    int techLevel = -1; // -1 = lowest level of the type
    int owner = 0;
    Point pos;
    int facing = 0;
    ScriptedOrder order;
    bool noAp = false; // bought reinforcement: no AP until the next turn
};

// A player's remaining deployment budget (after deployment: the
// reinforcement resource).
struct DeployBudget {
    int player = 0;
    int budget = 0;
};

// A unit taken back during deployment.
struct UnitRemoved {
    int unit = -1;
};

// A unit's scripted order changed (for example dropped by its Probability).
struct OrderChanged {
    int unit = -1;
    ScriptedOrder order;
};

// An automated unit arrived over `tile` (CTMMsgAutoUnit, automated-units.md
// 5.2). `second`: the delivery leg of a transport (its second animation).
struct AutomatedArrived {
    int typeId = 0;
    int owner = 0;
    int outcome = 1; // AutomatedOutcome: 1 OK, 2 damaged, 3 destroyed
    Point tile;
    bool second = false;
};

// A unit fired at an automated unit (CTMMsgUnitAA); `ap` is its currentAP
// afterwards.
struct UnitFiredAA {
    int unit = -1;
    int ap = 0;
};

// Repair of one tile by a medic (CTMMsgRepair, 6.2): the new armor of the
// building, unit and wall on it and the unit's new AP (-1 = no such object).
struct Repaired {
    Point tile;
    int building = -1, buildingArmor = 0;
    int unit = -1, unitArmor = 0, unitAp = 0;
    int wall = -1, wallArmor = 0;
};

// A bridge section built by an automated bridge builder (CTMMsgBridge).
struct BridgeBuilt {
    int roadType = 1;
    int owner = 0;
    Point tile;
};

// A transported unit (CTMMsgUpdateUnit, 6.6): picked up (taken off the map),
// put down on `pos` with `ap` and `armor`, or lost (`pos` outside the map).
struct UnitTransported {
    int unit = -1;
    bool pickedUp = false;
    int ap = 0;
    int armor = 0;
    Point pos{-1, -1};
};

// A DeployOnce type was used (2.4); `player` -1 = by every player.
struct AutomatedUsed {
    int player = -1;
    int typeId = 0;
};

// A request that was refused as a whole; the state is unchanged.
struct RequestRejected {
    std::string reason;
};

using Event = std::variant<UnitMoved, UnitShot, UnitDamaged, BuildingDamaged, WallDamaged, UnitDestroyed,
                           BuildingDestroyed, WallDestroyed, RoadDestroyed, MineDestroyed, MineExploded, MinesRevealed,
                           TerrainBurned, TurnStarted, BattleOver, UnitAdded, OrderChanged, DeployBudget, UnitRemoved,
                           RequestRejected, TechChanged, AutomatedArrived, UnitFiredAA, Repaired, BridgeBuilt,
                           UnitTransported, AutomatedUsed>;

// Applies an event to a state. The server changes its own state only
// through this function, so client copies stay identical.
void applyEvent(BattleState& state, const Event& event);

} // namespace openday::game
