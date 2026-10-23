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

    // The animation columns are in UnitAction order; -1 means "no clip".
    global.forEach("SELECT typeID, unitTechLevel, facingsImageID, Movement, Race, isConvoy, Facings, "
                   "MoveAnimation_PixSpeed, ShootShootAnimation_ProjBegin, ShootProjectileAnimation_PixOffset, "
                   "ShootProjectileAnimation_PixSpeed, ShootProjectileAnimation_Curve, ruinImageID, "
                   "MoveStartAnimation_Begin, MoveAnimation_Begin, MoveEndAnimation_Begin, "
                   "ShootBeginAnimation_Begin, ShootShootAnimation_Begin, ShootEndAnimation_Begin, "
                   "ShootProjectileAnimation_Begin, ShootImpactAnimation_Begin, ExplosionAnimation_Begin "
                   "FROM \"Unit Types\"",
                   [&](const data::Row& row) {
                       UnitType u;
                       u.image = toUpper(row.text(2));
                       u.movement = toInt(row, 3);
                       u.race = toInt(row, 4);
                       u.isConvoy = row.integer(5) != 0;
                       u.hasFacings = row.integer(6) != 0;
                       u.movePixSpeed = toInt(row, 7);
                       u.projectileStart = toInt(row, 8);
                       u.projectileOffset = toInt(row, 9);
                       u.projectileSpeed = toInt(row, 10);
                       u.projectileCurve = toInt(row, 11);
                       u.ruinImage = toUpper(row.text(12));
                       for (std::size_t a = 0; a < kUnitActionCount; ++a) u.clips[a] = row.integer(13 + static_cast<int>(a)) != -1;
                       r.units[{toInt(row, 0), toInt(row, 1)}] = std::move(u);
                   });
    global.forEach("SELECT TypeID, ImageID, SizeX, SizeY, Armor, DamagedImages, ExplosionAnimation_Begin "
                   "FROM \"Building Types\"",
                   [&](const data::Row& row) {
                       r.buildings[toInt(row, 0)] = BuildingType{toUpper(row.text(1)), toInt(row, 2), toInt(row, 3),
                                                                 toInt(row, 4), row.integer(5) != 0, row.integer(6) != -1};
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
