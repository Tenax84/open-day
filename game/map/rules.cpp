#include "map/rules.hpp"

#include <algorithm>
#include <cctype>

namespace openday::game {

std::string toUpper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return s;
}

TerrainClass GlobalRules::classOf(const std::string& terrainName) const {
    const TerrainType* t = terrainType(terrainName);
    return t ? t->cls : TerrainClass::Unknown;
}

const TerrainType* GlobalRules::terrainType(const std::string& terrainName) const {
    const auto it = terrain.find(terrainName);
    return it == terrain.end() ? nullptr : &it->second;
}

int GlobalRules::ruinCount(const std::string& prefix) const {
    int n = 0;
    for (const auto& [name, type] : ruins)
        if (name.compare(0, prefix.size(), prefix) == 0) ++n;
    return n;
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
                   "ShootProjectileAnimation_Begin, ShootImpactAnimation_Begin, ExplosionAnimation_Begin, "
                   "actionPoints, SizeX, SizeY, MineDetectionRange, Armor, UnitHasWeapon, weaponType, weaponRange, "
                   "weaponMinimumRange, HitShortRange, HitMediumRange, HitLongRange, actionPointsShoot, damageType, "
                   "damageBase, damageRandom, DamageArea, canOverwatch, Jammer "
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
                       u.actionPoints = toInt(row, 22);
                       u.sizeX = toInt(row, 23);
                       u.sizeY = toInt(row, 24);
                       u.mineDetectionRange = toInt(row, 25);
                       u.armor = toInt(row, 26);
                       u.hasWeapon = row.integer(27) != 0;
                       u.indirect = toInt(row, 28) == 1;
                       u.range = toInt(row, 29);
                       u.minRange = toInt(row, 30);
                       u.hit = {toInt(row, 31), toInt(row, 32), toInt(row, 33)};
                       u.shotAp = toInt(row, 34);
                       u.apDamage = toInt(row, 35) == 1;
                       u.damageBase = toInt(row, 36);
                       u.damageRandom = toInt(row, 37);
                       u.damageArea = std::max(1, toInt(row, 38));
                       u.canOverwatch = row.integer(39) != 0;
                       u.jammer = toInt(row, 40);
                       r.units[{toInt(row, 0), toInt(row, 1)}] = std::move(u);
                   });
    global.forEach("SELECT TypeID, ImageID, SizeX, SizeY, Armor, DamagedImages, ExplosionAnimation_Begin, Race, "
                   "isDestructible, hasRuin FROM \"Building Types\"",
                   [&](const data::Row& row) {
                       r.buildings[toInt(row, 0)] =
                           BuildingType{toUpper(row.text(1)), toInt(row, 2),         toInt(row, 3),
                                        toInt(row, 4),        row.integer(5) != 0,   row.integer(6) != -1,
                                        toInt(row, 7),        row.integer(8) != 0,   row.integer(9) != 0};
                   });
    global.forEach("SELECT RoadType, img_id, ar FROM \"Road Types\"", [&](const data::Row& row) {
        r.roads[toInt(row, 0)] = RoadType{toUpper(row.text(1)), toInt(row, 2)};
    });
    global.forEach("SELECT WallType, img_id, Armor FROM \"Wall Types\"", [&](const data::Row& row) {
        r.walls[toInt(row, 0)] = WallType{toUpper(row.text(1)), toInt(row, 2)};
    });
    global.forEach("SELECT typeID, ImageID, damageType, damageBase, damageRandom FROM \"Mine Types\"",
                   [&](const data::Row& row) {
                       r.mines[toInt(row, 0)] =
                           MineType{toUpper(row.text(1)), toInt(row, 2) == 1, toInt(row, 3), toInt(row, 4)};
                   });
    global.forEach("SELECT id, type, can_fire_through, burned_id FROM \"Terrain Types\"", [&](const data::Row& row) {
        r.terrain[toUpper(row.text(0))] = TerrainType{static_cast<TerrainClass>(toInt(row, 1)), row.integer(2) != 0,
                                                       row.isNull(3) ? std::string() : toUpper(row.text(3))};
    });
    global.forEach("SELECT Short, Medium FROM \"Ranges\"",
                   [&](const data::Row& row) { r.ranges = {toInt(row, 0), toInt(row, 1)}; });
    global.forEach("SELECT DamageEasy, DamageHard FROM \"Difficulty\"", [&](const data::Row& row) {
        r.damageEasy = toInt(row, 0);
        r.damageHard = toInt(row, 1);
    });
    global.forEach("SELECT id, can_fire_through, can_move_through FROM \"Ruin Types\"", [&](const data::Row& row) {
        r.ruins[toUpper(row.text(0))] = RuinType{row.integer(1) != 0, row.integer(2) != 0};
    });
    return r;
}

} // namespace openday::game
