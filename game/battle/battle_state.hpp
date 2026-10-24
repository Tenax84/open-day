// The state of a tactical battle: units, buildings and the map objects that
// matter for the rules, plus whose turn it is. Rules:
// docs/mechanics/tactical-movement.md, docs/mechanics/tactical-combat.md.
#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "data/sqlite_db.hpp"
#include "map/map_renderer.hpp"
#include "map/rules.hpp"
#include "map/terrain_map.hpp"

namespace openday::game {

// Player slots: 0 = AI side, 1-4 = human players.
inline constexpr int kPlayerSlots = 5;

// A unit's scripted order for the computer player (docs/mechanics/
// tactical-ai.md): Units On Map.OrderID, Turn, Probability, EnemyUnitType.
struct ScriptedOrder {
    int order = 0; // Unit Order Id Lookup; 0 = no order
    int turn = 0;  // last turn of the order
    int probability = 100;
    int enemyType = 65535;
    std::vector<Point> positions{}; // Unit Order Pos List: waypoints, patrol route
    std::vector<int> ids{};         // Unit Order ID List: escorted / defended / attacked objects
};

struct BattleUnit {
    int id = 0;                     // object id from the scenario
    const UnitType* type = nullptr; // points into GlobalRules
    int owner = 0;                  // player slot
    int ap = 0;                     // currentAP
    int negAp = 0;                  // AP debt, subtracted at the next refill
    int armor = 0;                  // currentAR
    Point pos;
    int facing = 0;
    bool alive = true;
    ScriptedOrder order{};

    int availableAp() const { return ap > negAp ? ap - negAp : 0; }
};

struct BattleBuilding {
    int id = 0;
    const BuildingType* type = nullptr;
    int owner = 0;
    int armor = 0;
    Point pos; // footprint corner with the smallest x and y
    bool alive = true;
};

struct BattleWall {
    int type = 0; // Wall Types.WallType
    int owner = 0;
    int armor = 0;
    Point pos;
    bool brokenEnd = false; // Walls On Map.isDamaged, see map-objects.md
    bool alive = true;
};

struct BattleRoad {
    int type = 0; // Road Types.RoadType
    int owner = 0;
    Point pos;
    bool alive = true;
};

struct BattleMine {
    int type = 0; // Mine Types.typeID: 0 = standard, 1 = grav
    int owner = 0;
    Point pos;
    bool armed = true;
    std::array<bool, kPlayerSlots> hidden{}; // hidden from player slot
    bool alive = true;
};

struct BattleRuin {
    std::string name; // RUINS.DBI
    Point pos;
};

// The scenario's objective (docs/mechanics/battle-flow.md section 2).
struct Objective {
    int kind = -1;           // Objectives Id Lookup; -1 = none
    int turns = 0;           // Objective.TurnNumber (N)
    std::vector<int> ids;    // Objectives ID List: unit or building ids
    std::vector<Point> zone; // Objectives Pos List
};

// The battle statistics of one human player (battle-flow.md 5.3): four
// counters keyed by unit / building type id (all tech levels share one key).
struct BattleStats {
    std::map<int, int> unitsDestroyed, unitsLost, buildingsDestroyed, buildingsLost;

    static int total(const std::map<int, int>& counter);
};

// One human player's row of the result (battle-flow.md 5.1): the four
// totals and the score (5.4).
struct PlayerResult {
    int player = 0;
    int unitsDestroyed = 0, buildingsDestroyed = 0, unitsLost = 0, buildingsLost = 0;
    int score = 0;
};

// A pending Reinforcement row (battle-flow.md 4.1).
struct Reinforcement {
    int unitId = 0;
    int type = 0; // Unit Types.typeID
    Point pos;
    int turn = 0;
    bool order = false;
};

class BattleState {
public:
    BattleState(const GlobalRules& rules, TerrainMap terrain);

    const GlobalRules& rules() const { return *rules_; }
    const TerrainMap& terrain() const { return terrain_; }
    int size() const { return terrain_.size; }
    bool inside(Point p) const { return terrain_.inside(p.x, p.y); }
    TerrainClass classAt(Point p) const;
    const TerrainType* terrainAt(Point p) const;
    void setTerrain(Point p, const std::string& name);

    // Objects. Add and change them through the functions below, which keep
    // the per-tile lookup current.
    const std::vector<BattleUnit>& units() const { return units_; }
    const std::vector<BattleBuilding>& buildings() const { return buildings_; }
    const std::vector<BattleWall>& walls() const { return walls_; }
    const std::vector<BattleRoad>& roads() const { return roads_; }
    const std::vector<BattleMine>& mines() const { return mines_; }
    const std::vector<BattleRuin>& ruins() const { return ruins_; }
    const BattleUnit& unit(int i) const { return units_[static_cast<std::size_t>(i)]; }
    BattleUnit& unit(int i) { return units_[static_cast<std::size_t>(i)]; }
    BattleBuilding& building(int i) { return buildings_[static_cast<std::size_t>(i)]; }
    BattleWall& wall(int i) { return walls_[static_cast<std::size_t>(i)]; }
    BattleMine& mine(int i) { return mines_[static_cast<std::size_t>(i)]; }
    const BattleBuilding& building(int i) const { return buildings_[static_cast<std::size_t>(i)]; }
    const BattleWall& wall(int i) const { return walls_[static_cast<std::size_t>(i)]; }
    const BattleMine& mine(int i) const { return mines_[static_cast<std::size_t>(i)]; }

    int addUnit(BattleUnit u);
    int addBuilding(BattleBuilding b);
    int addWall(BattleWall w);
    int addRoad(BattleRoad r);
    int addMine(BattleMine m);
    void addRuin(BattleRuin r);
    void moveUnit(int unit, Point to);
    void removeUnit(int unit);
    void removeBuilding(int building);
    void removeWall(int wall);
    void removeRoad(int road);
    void removeMine(int mine);

    // Index of the living object on a tile, or -1.
    int unitAt(Point p) const { return lookup(unitGrid_, p); }
    int buildingAt(Point p) const { return lookup(buildingGrid_, p); }
    int wallAt(Point p) const { return lookup(wallGrid_, p); }
    int roadAt(Point p) const { return lookup(roadGrid_, p); }
    int mineAt(Point p) const { return lookup(mineGrid_, p); }
    // Road type on a tile, or -1.
    int roadTypeAt(Point p) const;
    bool hasRuin(Point p) const { return inside(p) && ruinFlags_[index(p)] != 0; }
    bool ruinBlocksMove(Point p) const { return inside(p) && (ruinFlags_[index(p)] & 2) != 0; }
    bool ruinBlocksFire(Point p) const { return inside(p) && (ruinFlags_[index(p)] & 4) != 0; }

    // Turn order: the participating player slots in ascending order, slot 0
    // first; slot 0 plays only when there is a computer player.
    std::vector<int> players;
    bool computerPlayer = true; // false in death match
    int currentPlayer = 0;
    int turn = 0;      // Tactic IO.Current Turn: 1 at the start, +1 per round
    int turnLimit = 0; // Scenario Info.turn_qty; 0 = none
    bool over = false;
    bool success = false; // when over: the human side won
    int loser = 5;        // when over: death-match loser slot, 5 = none

    Objective objective;
    std::vector<Reinforcement> reinforcements; // not yet placed

    // Deployment (battle-flow.md 1.2): while `deploying`, human players
    // place units bought from their budget on their zone tiles.
    bool deploying = false;
    std::array<std::vector<Point>, kPlayerSlots> deployZone{};
    std::array<int, kPlayerSlots> deployBudget{};
    std::vector<int> purchasable; // One Off Units Available: unit typeIDs
    bool inDeployZone(int player, Point p) const;
    // After deployment the budget is the player's reinforcement resource
    // (battle-flow.md 4.2): it grows by `income` at the start of each of his
    // turns. In a city battle (campaign.md 6.4) the income is recomputed
    // from the deployment centres and HQ still standing instead.
    std::array<int, kPlayerSlots> income{};
    struct CityIncome {
        int deployType = -1;  // Building TypeID of the colony's deployment centre; -1 = not a city battle
        int deployProduction = 0;
        int hqProduction = 0;
        int multiplier = 100; // M(d)
        int share = 100;      // human-player share in percent
    } cityIncome;
    // City battle: a type can only be bought while its prerequisite building
    // stands on the map (campaign.md 6.4).
    bool deployNeedsBuildings = false;
    // Tech level per unit type for a player's deployments; missing = 0.
    std::array<std::map<int, int>, kPlayerSlots> techLevels{};
    int techLevel(int player, int typeId) const;
    // Unit types the player may buy now.
    std::vector<int> buyable(int player) const;
    // Tech buildings held by the human side (battle-flow.md 5.2): indices
    // into buildings().
    std::vector<int> techHeld;
    int techFound() const;
    // Hold Position bookkeeping (battle-flow.md 2.3); server only.
    int holdCount = 0;
    int lastHeldTurn = -1;
    // The cheat win / lose flags (also surrender), battle-flow.md 2.1 and
    // 3.1; server only.
    bool cheatWin = false, cheatLose = false;
    // The objective's progress counter sent with each turn (the red number
    // of the turn counter, tactical-interface.md 2.5).
    int objectiveProgress = 0;

    // Battle statistics per human slot (battle-flow.md 5.3); slot 0 stays
    // empty. Counted by applyEvent, so every copy of the state has them.
    std::array<BattleStats, kPlayerSlots> stats{};
    bool deathMatch() const { return !computerPlayer; }
    // When over: one row per human player in slot order (5.1).
    std::vector<PlayerResult> results;

    // Automated units (docs/mechanics/automated-units.md 2). Per player the
    // usable types; with `automatedNeedsBuildings` only those whose
    // Prerequisite building the player owns on the map (campaign city
    // battles, death match).
    std::array<std::vector<int>, kPlayerSlots> automated{};
    std::array<bool, kPlayerSlots> automatedNeedsBuildings{};
    // DeployOnce types already called, per player (2.4).
    std::array<std::vector<int>, kPlayerSlots> automatedUsed{};
    std::vector<int> usableAutomated(int player) const;
    // Bridge sites of the map (city-building.md 4.3), for the bridge builders.
    std::vector<Point> bridgeSites;
    bool isBridgeSite(Point p) const;
    // The scenario's Automated Units rows, called by the computer player in
    // the turn `turn` (tactical-ai.md 6.2); server only.
    struct ScheduledAutomated {
        int type = 0;
        Point start, dest;
        int turn = 0;
    };
    std::vector<ScheduledAutomated> scheduledAutomated;
    // The computer player's strategic AI info (tactical-ai.md 0, 6.1):
    // present in campaign and counter-attack battles.
    struct AiSupport {
        bool present = false;
        int chance = 0; // AiAutoUnitChanceCall
        int budget = 0; // AiAutoUnitRes
        std::vector<int> types{}; // AI automated available
    } aiSupport;
    bool counterAttack = false; // the computer player attacks the player's city
    int oneOffDeployment = 0;   // Scenario Info.One Off Deployment (emergency drop, 6.3)

    // An object id for a new unit (bought, dropped): above every unit and
    // building id, every pending reinforcement and every objective id, so
    // it never matches a scripted reinforcement or a listed object.
    int nextObjectId() const;

    // Looks up a unit type at a tech level; with a negative `techLevel`,
    // the lowest level of that type. nullptr if unknown.
    const UnitType* unitType(int typeId, int techLevel) const;

private:
    std::size_t index(Point p) const { return static_cast<std::size_t>(p.y * terrain_.size + p.x); }
    int lookup(const std::vector<int>& grid, Point p) const { return inside(p) ? grid[index(p)] : -1; }
    void mark(std::vector<int>& grid, Point p, int value);

    const GlobalRules* rules_;
    TerrainMap terrain_;
    std::vector<BattleUnit> units_;
    std::vector<BattleBuilding> buildings_;
    std::vector<BattleWall> walls_;
    std::vector<BattleRoad> roads_;
    std::vector<BattleMine> mines_;
    std::vector<BattleRuin> ruins_;
    std::vector<int> unitGrid_, buildingGrid_, wallGrid_, roadGrid_, mineGrid_;
    std::vector<std::uint8_t> ruinFlags_; // 1 = ruin, 2 = blocks moves, 4 = blocks fire
};

// Reads the objects of a scenario. Units and buildings come in the same
// order as MapEntities from loadMapObjects, so their indices match.
BattleState loadBattle(const data::SqliteDb& scenario, const GlobalRules& rules, const TerrainMap& terrain);

// Sets up the participants (slot 0 plus every slot that owns a unit or
// building) and gives every unit its AP. The first turn is begun by the
// server (BattleServer::start).
void startBattle(BattleState& state);

// Refills the AP of a player's units at the start of their turn.
void refillAp(BattleState& state, int player);

// Counts a destroyed unit or building in the statistics (battle-flow.md
// 5.3): `owner` lost it, `attacker` (slot, -1 = nobody) destroyed it.
void countUnitDestroyed(BattleState& state, int typeId, int owner, int attacker);
void countBuildingDestroyed(BattleState& state, int typeId, int owner, int attacker);

// A human player's score (battle-flow.md 5.4). OpenDay uses the intended
// formula (kills x 2 and losses of units, buildings destroyed and lost, at
// the plain Cost of each type), clamped to 0 ... 65535.
int battleScore(const BattleState& state, int player);
// The score as the original computes it: the value of the units destroyed,
// counted once per Unit Types row of the type (5.4, "Actual result").
int originalBattleScore(const BattleState& state, int player);
// The rows of the result message: every human participant in slot order.
std::vector<PlayerResult> battleResults(const BattleState& state);

} // namespace openday::game
