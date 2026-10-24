// The parts of GLOBAL.MDB (converted to SQLite) that the map and battle need.
// See docs/mechanics/data-model.md.
#pragma once

#include <array>
#include <map>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

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
    std::string name{}; // NameEnglish
    int techReward = 0;
    int category = 0;   // Building Category Lookup (1 = ruin)
    // Construction in the campaign (docs/mechanics/city-building.md).
    bool canBuild = false;
    int prerequisite = 65535; // TypeID of a building that must exist, 65535 = none
    bool uniqueOnMap = false;
    int transformIntoUnit = 65535; // towers: Unit Types.typeID that represents it in battle
    int position = 3;              // placement mask: 1 = deep water, 3 = plain
    int cost = 0;
    int recycle = 0;
    int resProduction = 0; // deployment points per turn (HQ, deployment centres)
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
    std::string name{}; // NameEnglish
    int techLevel = 0;  // unitTechLevel of this row
    // Campaign (docs/mechanics/army-tech.md).
    int prerequisite = 65535; // building TypeID needed to deploy the unit
    int costToNextTL = 0;     // tech points to reach the next level
    int category = 0;         // Unit Category Lookup: 0 grav, 1 ground, 2 water
    int weaponArea = 0;       // DamageArea as stored (0 = single tile)
    bool isAA = false;
    int damageAA = 0;
};

struct WallType {
    std::string image; // WALLS.DBI prefix (WALLC, ...)
    int armor = 0;
    int id = 0;
    std::string name{};
    int prerequisite = 65535;
    bool canBuild = false;
    int cost = 0;
    int recycle = 0;
};

struct RoadType {
    std::string image; // ROADS.DBI prefix (ROAL, ROAH)
    int armor = 0;     // `ar`: armor damage that destroys the road in one hit
    int id = 0;
    std::string name{};
    bool canBuild = false;
    int cost = 0;
    int recycle = 0;
};

struct MineType {
    std::string image; // MINES.DBI prefix (MINE, GMINE)
    bool apDamage = false;
    int damageBase = 0;
    int damageRandom = 0;
    int id = 0;
    std::string name{};
    int prerequisite = 65535;
    bool canBuy = false;
    int cost = 0;
    int recycle = 0;
    int deployTerrain = 3; // TerrainType Lookup class where it can be laid
    std::string listbox{}; // LISTBOX.DBI name
};

// A called-in support action (Automated Units, data-model.md).
struct AutomatedType {
    int id = 0; // typeID
    std::string name{};
    int race = 0;
    int category = 0; // Automated Category Lookup
    int cost = 0;
    int prerequisite = 65535;
    bool deployOnce = false;
    std::string image{};  // ANMSUNIT.FF AUTOUNIT code, LISTBOX.DBI code
    std::string cursor{}; // SYMBOLS.DBI targeting cursor
};

// One bracket of the Tech Ratio table.
struct TechRatio {
    int min = 0, max = -1; // -1 = open-ended
    int ratio = 1;
};

struct Rgb {
    int r = 0, g = 0, b = 0;
};

struct TerrainType {
    TerrainClass cls = TerrainClass::Unknown;
    bool canFireThrough = true;
    std::string burned; // TERRAINS.DBI name after burning; empty = cannot burn
    Rgb color{};        // minimap color
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
    int damageAvg = 100;
    std::array<int, 3> creditPercent{100, 100, 100};      // Difficulty CreditEasy/Avg/Hard
    std::array<int, 3> deployPercent{100, 100, 100};      // Difficulty DeployEasy/Avg/Hard
    std::map<int, AutomatedType> automated;               // typeID
    std::vector<TechRatio> techRatios;
    std::array<std::array<int, 12>, 12> aiPriority{};     // AI Priority [source][target] category
    std::map<std::pair<int, int>, Rgb> colors;            // Colors: (Type, Enum) -> minimap color

    TerrainClass classOf(const std::string& terrainName) const;
    const TerrainType* terrainType(const std::string& terrainName) const;
    // Number of RUINS.DBI names starting with `prefix` (building rubble sets).
    int ruinCount(const std::string& prefix) const;
};

GlobalRules loadGlobalRules(const data::SqliteDb& global);

std::string toUpper(std::string s);

} // namespace openday::game
