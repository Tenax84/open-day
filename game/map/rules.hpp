// The parts of GLOBAL.MDB (converted to SQLite) that map rendering needs.
// See docs/mechanics/data-model.md.
#pragma once

#include <array>
#include <map>
#include <string>
#include <unordered_map>
#include <utility>

#include "data/sqlite_db.hpp"

namespace openday::game {

enum class TerrainClass { ShallowWater = 0, DeepWater = 1, Riverside = 2, Plain = 3, Mountains = 4, Forest = 5, Crack = 6, Unknown = -1 };

struct BuildingType {
    std::string image; // UB.DBI code and ANMSUNIT.FF BUILDING\ clip, upper case
    int sizeX = 1;
    int sizeY = 1;
    int armor = 0;
    bool damagedImages = false;
    bool hasExplosion = false;
};

// Unit actions with their own animation clip, in the program's order
// (docs/mechanics/unit-animation.md, "Entry name").
enum class UnitAction { MoveStart, Move, MoveEnd, ShootBegin, Shoot, ShootEnd, Projectile, Impact, Explosion };
inline constexpr std::size_t kUnitActionCount = 9;

struct UnitType {
    std::string image; // UB.DBI and ANMSUNIT.FF code, upper case
    int movement = 0;  // Unit Movement Lookup; 2 = trooper (infantry)
    int race = 0;      // 0 = human
    bool isConvoy = false;
    bool hasFacings = true;
    std::array<bool, kUnitActionCount> clips{}; // per UnitAction: the clip exists
    int movePixSpeed = 8;                       // pixels per tick along the longer screen axis
    int projectileStart = 0;                    // ticks into the shot clip
    int projectileOffset = 0;                   // pixels skipped at the start
    int projectileSpeed = 20;                   // pixels per tick
    int projectileCurve = 0;                    // arc height in percent of the path length
    std::string ruinImage;                      // RUINS.DBI name, upper case
};

struct WallType {
    std::string image; // WALLS.DBI prefix (WALLC, ...)
    int armor = 0;
};

struct GlobalRules {
    std::map<std::pair<int, int>, UnitType> units;        // (typeID, unitTechLevel)
    std::map<int, BuildingType> buildings;                // TypeID
    std::map<int, std::string> roadImage;                 // RoadType -> ROADS.DBI prefix (ROAL, ROAH)
    std::map<int, WallType> walls;                        // WallType
    std::map<int, std::string> mineImage;                 // typeID -> MINES.DBI prefix (MINE, GMINE)
    std::unordered_map<std::string, TerrainClass> terrainClass; // TERRAINS.DBI name -> class

    TerrainClass classOf(const std::string& terrainName) const;
};

GlobalRules loadGlobalRules(const data::SqliteDb& global);

std::string toUpper(std::string s);

} // namespace openday::game
