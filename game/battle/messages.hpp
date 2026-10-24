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

using Request = std::variant<MoveRequest, ShootRequest, EndTurnRequest>;

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

struct UnitDestroyed {
    int unit = -1;
    std::string wreck; // RUINS.DBI name left on the tile, or empty
};

struct BuildingDestroyed {
    int building = -1;
    std::vector<BattleRuin> rubble;
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
};

// A request that was refused as a whole; the state is unchanged.
struct RequestRejected {
    std::string reason;
};

using Event = std::variant<UnitMoved, UnitShot, UnitDamaged, BuildingDamaged, WallDamaged, UnitDestroyed,
                           BuildingDestroyed, WallDestroyed, RoadDestroyed, MineDestroyed, MineExploded, MinesRevealed,
                           TerrainBurned, TurnStarted, BattleOver, RequestRejected>;

// Applies an event to a state. The server changes its own state only
// through this function, so client copies stay identical.
void applyEvent(BattleState& state, const Event& event);

} // namespace openday::game
