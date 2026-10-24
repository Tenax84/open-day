// The player's city in the campaign: its map and the structures built on
// it, and the construction rules (docs/mechanics/city-building.md).
#pragma once

#include <array>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "battle/battle_state.hpp"
#include "data/sqlite_db.hpp"
#include "map/map_objects.hpp"
#include "map/rules.hpp"
#include "map/terrain_map.hpp"

namespace openday::game {

inline constexpr int kHeadquarters = 130; // H_Center_HQ
inline constexpr int kLaboratory = 135;   // H_Center_Laboratory
inline constexpr int kSatellite = 137;    // H_Center_Satellite
inline constexpr int kNone = 65535;

struct CityBuilding {
    int id = 0;
    int type = 0; // Building Types.TypeID
    int owner = 1;
    int armor = 0;
    Point pos; // top-left tile
    int w = 1, h = 1; // footprint (SizeX, SizeY of the type)
};

struct CityRoad {
    int type = 0;
    int owner = 1;
    Point pos;
};

struct CityWall {
    int type = 0;
    int owner = 1;
    int armor = 0;
    Point pos;
    bool brokenEnd = false;
};

struct CityMine {
    int type = 0;
    int owner = 1;
    Point pos;
    bool armed = true;
    std::array<bool, kPlayerSlots> hidden{}; // hidden from player slot
};

struct HqSite {
    int slot = 1;
    Point pos;
};

// A `Counter Attack` row: an attacker of a battle on the city.
struct AttackRow {
    int unitId = 0;
    int type = 0;
    Point pos;
    int turn = 0;
    bool order = false;
};

// What a placement is about.
enum class StructureKind { Building, Tower, Road, Wall, Mine };

// Result codes of the placement check; the message is string 384 + code.
enum BuildCode : int {
    kBuildOk = -1,
    kBuildGeneric = 0,
    kBuildNoCredits = 1,
    kBuildTerrain = 2,
    kBuildTooClose = 4,
    kBuildOtherPlayer = 5,
    kBuildTooFar = 6,
    kBuildMoveHq = 7,
    kBuildRecycleHq = 8,
    kBuildHqSite = 9,
    kBuildBorder = 10,
    kBuildBridgeRoad = 12,
    kBuildAroundHq = 13,
    kBuildDeployLimit = 14,
    kBuildNoRoad = 15,
    kBuildNotOwn = 16,
};

// Objects being moved; they are ignored by the placement check. -1 = none.
struct MovedObjects {
    int building = -1, road = -1, wall = -1, mine = -1;
};

class City {
public:
    std::string scenario; // scenario name, e.g. "s1-1-1"
    TerrainMap terrain;
    std::vector<CityBuilding> buildings;
    std::vector<CityRoad> roads;
    std::vector<CityWall> walls;
    std::vector<CityMine> mines;
    std::vector<HqSite> hqSites;
    std::vector<AttackRow> counterAttack; // the city's own attackers (first city attack)
    int nextBuildingId = 1000000;
    int nextUnitId = 100;

    int size() const { return terrain.size; }
    bool inside(Point p) const { return terrain.inside(p.x, p.y); }
    // Index of the object on a tile, or -1.
    int buildingAt(Point p) const;
    int roadAt(Point p) const;
    int wallAt(Point p) const;
    int mineAt(Point p) const;
    int buildingById(int id) const;
    int count(int player, int buildingType) const;
    bool owns(int player, int buildingType) const { return count(player, buildingType) > 0; }

    // Bridge-site tiles computed from the terrain (city-building.md 4.3).
    const std::vector<Point>& bridgeSites() const { return bridgeSites_; }
    bool isBridgeSite(Point p) const;
    void computeBridgeSites(const GlobalRules& rules);

    // Removes the HQ and HQ site of every slot above `humans`.
    void removeUnusedSlots(int humans);

    // Map objects for drawing.
    ScenarioObjects objects() const;

private:
    std::vector<Point> bridgeSites_;
};

City loadCity(const data::SqliteDb& scenario, const std::string& name, const GlobalRules& rules);

bool isTower(const BuildingType& t);
bool isDeployCentre(int buildingType); // Deploy Mk1 ... Mk4

// The placement check (city-building.md 3). `type` is the Building Types,
// Road Types, Wall Types or Mine Types id.
int checkPlacement(const City& city, const GlobalRules& rules, int player, StructureKind kind, int type, Point pos,
                   const MovedObjects& moved = {});

// Road terrain state (4.2): 0 road, 1 ramp, 2 bridge, 3 forbidden.
int roadState(TerrainClass c);
// The "area" part of the check alone (step 1), used for the segment preview.
bool areaOk(const City& city, const GlobalRules& rules, int player, StructureKind kind, int type, Point pos,
            const MovedObjects& moved = {});

// Structure lists of the city screen (city-building.md 2).
struct BuildOption {
    StructureKind kind = StructureKind::Building;
    int type = 0;
};
std::vector<BuildOption> buildingOptions(const City& city, const GlobalRules& rules, const std::vector<int>& available,
                                         int player);
std::vector<BuildOption> defenseOptions(const City& city, const GlobalRules& rules, const std::vector<int>& available,
                                        int player);

// Unit types and automated units a player's city unlocks (8).
std::vector<int> deployableTypes(const City& city, const GlobalRules& rules, int player);
std::vector<int> automatedTypes(const City& city, const GlobalRules& rules, int player);

// Structures placed in the current strategic phase (full refund).
struct BuiltThisPhase {
    std::set<int> buildings; // ids
    std::set<std::pair<int, int>> roads, walls, mines;
    void clear() { *this = {}; }
};

// The result of a build, recycle or move request.
struct BuildResult {
    int code = kBuildOk;       // first failure, or kBuildOk
    int pieces = 0;            // structures built / recycled / moved
    int credits = 0;           // credits spent (build) or refunded (recycle)
    std::string name;          // name of the structure type
};

// Construction requests (city-building.md 3-7). `credits` is changed.
BuildResult buildStructure(City& city, const GlobalRules& rules, int player, int& credits, BuiltThisPhase& built,
                           StructureKind kind, int type, Point pos);
BuildResult buildSegment(City& city, const GlobalRules& rules, int player, int& credits, BuiltThisPhase& built,
                         StructureKind kind, int type, Point start, Point end);
// Recycles every own structure on the tile.
BuildResult recycleAt(City& city, const GlobalRules& rules, int player, int& credits, BuiltThisPhase& built, Point pos);
// Moves every own structure on `from` to `to` (a building by its top-left).
BuildResult moveAt(City& city, const GlobalRules& rules, int player, BuiltThisPhase& built, Point from, Point to);

} // namespace openday::game
