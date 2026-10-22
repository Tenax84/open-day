// The parts of GLOBAL.MDB (converted to SQLite) that map rendering needs.
// See docs/mechanics/data-model.md.
#pragma once

#include <map>
#include <string>
#include <unordered_map>
#include <utility>

#include "data/sqlite_db.hpp"

namespace openday::game {

enum class TerrainClass { ShallowWater = 0, DeepWater = 1, Riverside = 2, Plain = 3, Mountains = 4, Forest = 5, Crack = 6, Unknown = -1 };

struct BuildingType {
    std::string image; // UB.DBI code, upper case
    int sizeX = 1;
    int sizeY = 1;
    int armor = 0;
    bool damagedImages = false;
};

struct UnitType {
    std::string image; // UB.DBI code, upper case
    int movement = 0;  // Unit Movement Lookup; 2 = trooper (infantry)
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
