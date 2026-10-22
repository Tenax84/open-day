#include "map/rules.hpp"

#include <algorithm>
#include <cctype>

namespace openday::game {

std::string toUpper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return s;
}

TerrainClass GlobalRules::classOf(const std::string& terrainName) const {
    const auto it = terrainClass.find(terrainName);
    return it == terrainClass.end() ? TerrainClass::Unknown : it->second;
}

GlobalRules loadGlobalRules(const data::SqliteDb& global) {
    GlobalRules r;
    auto toInt = [](const data::Row& row, int col) { return static_cast<int>(row.integer(col)); };

    global.forEach("SELECT typeID, unitTechLevel, facingsImageID, Movement FROM \"Unit Types\"",
                   [&](const data::Row& row) {
                       r.units[{toInt(row, 0), toInt(row, 1)}] = UnitType{toUpper(row.text(2)), toInt(row, 3)};
                   });
    global.forEach("SELECT TypeID, ImageID, SizeX, SizeY, Armor, DamagedImages FROM \"Building Types\"",
                   [&](const data::Row& row) {
                       r.buildings[toInt(row, 0)] = BuildingType{toUpper(row.text(1)), toInt(row, 2), toInt(row, 3),
                                                                 toInt(row, 4), row.integer(5) != 0};
                   });
    global.forEach("SELECT RoadType, img_id FROM \"Road Types\"",
                   [&](const data::Row& row) { r.roadImage[toInt(row, 0)] = toUpper(row.text(1)); });
    global.forEach("SELECT WallType, img_id, Armor FROM \"Wall Types\"", [&](const data::Row& row) {
        r.walls[toInt(row, 0)] = WallType{toUpper(row.text(1)), toInt(row, 2)};
    });
    global.forEach("SELECT typeID, ImageID FROM \"Mine Types\"",
                   [&](const data::Row& row) { r.mineImage[toInt(row, 0)] = toUpper(row.text(1)); });
    global.forEach("SELECT id, type FROM \"Terrain Types\"", [&](const data::Row& row) {
        r.terrainClass[toUpper(row.text(0))] = static_cast<TerrainClass>(toInt(row, 1));
    });
    return r;
}

} // namespace openday::game
