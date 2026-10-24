// The parts of GLOBAL.MDB (converted to SQLite) that the map and battle need.
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
    int race = 0;
    bool destructible = true;
    bool hasRuin = false;
    int id = 0; // TypeID; 130 = Headquarters
    int aiCategory = 9; // AI Category Lookup
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
    int actionPoints = 0;                       // AP per turn
    int sizeX = 1, sizeY = 1;                   // footprint; only 1 x 1 units move
    int mineDetectionRange = 0;
    // Combat, docs/mechanics/tactical-combat.md.
    int armor = 0; // maximum armor (hit points)
    bool hasWeapon = false;
    bool indirect = false;    // weaponType 1: no line of fire needed
    int range = 0;            // weaponRange
    int minRange = 0;         // weaponMinimumRange, exclusive
    std::array<int, 3> hit{}; // HitShortRange, HitMediumRange, HitLongRange
    int shotAp = 0;           // actionPointsShoot
    bool apDamage = false;    // damageType 1: the weapon reduces AP
    int damageBase = 0;
    int damageRandom = 0;
    int damageArea = 1;
    bool canOverwatch = false;
    int jammer = 0;
    // Computer player, docs/mechanics/tactical-ai.md.
    int typeId = 0;
    int cost = 0;
    int aiCategory = 0; // AI Category Lookup
};

struct WallType {
    std::string image; // WALLS.DBI prefix (WALLC, ...)
    int armor = 0;
};

struct RoadType {
    std::string image; // ROADS.DBI prefix (ROAL, ROAH)
    int armor = 0;     // `ar`: armor damage that destroys the road in one hit
};

struct MineType {
    std::string image; // MINES.DBI prefix (MINE, GMINE)
    bool apDamage = false;
    int damageBase = 0;
    int damageRandom = 0;
};

struct TerrainType {
    TerrainClass cls = TerrainClass::Unknown;
    bool canFireThrough = true;
    std::string burned; // TERRAINS.DBI name after burning; empty = cannot burn
};

struct RuinType {
    bool canFireThrough = true;
    bool canMoveThrough = true;
};

struct GlobalRules {
    std::map<std::pair<int, int>, UnitType> units;        // (typeID, unitTechLevel)
    std::map<int, BuildingType> buildings;                // TypeID
    std::map<int, RoadType> roads;                        // RoadType
    std::map<int, WallType> walls;                        // WallType
    std::map<int, MineType> mines;                        // typeID
    std::unordered_map<std::string, TerrainType> terrain; // TERRAINS.DBI name -> properties
    std::unordered_map<std::string, RuinType> ruins;      // RUINS.DBI name -> properties
    std::array<int, 2> ranges{3, 6};                      // Ranges: Short, Medium
    int damageEasy = 100, damageHard = 100;               // Difficulty, percent
    std::array<std::array<int, 12>, 12> aiPriority{};     // AI Priority [source][target] category

    TerrainClass classOf(const std::string& terrainName) const;
    const TerrainType* terrainType(const std::string& terrainName) const;
    // Number of RUINS.DBI names starting with `prefix` (building rubble sets).
    int ruinCount(const std::string& prefix) const;
};

GlobalRules loadGlobalRules(const data::SqliteDb& global);

std::string toUpper(std::string s);

} // namespace openday::game
