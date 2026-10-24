#include "map/rules.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>

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

namespace {

bool hasColumn(const data::SqliteDb& db, const std::string& table, const std::string& column) {
    bool has = false;
    db.forEach("SELECT name FROM pragma_table_info('" + table + "') WHERE name = '" + column + "'",
               [&](const data::Row&) { has = true; });
    return has;
}

} // namespace

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
                   "damageBase, damageRandom, DamageArea, canOverwatch, Jammer, Cost, AIcategory, NameEnglish "
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
                       u.cost = toInt(row, 41);
                       u.aiCategory = toInt(row, 42);
                       u.typeId = toInt(row, 0);
                       u.techLevel = toInt(row, 1);
                       u.name = row.isNull(43) ? std::string() : row.text(43);
                       r.units[{toInt(row, 0), toInt(row, 1)}] = std::move(u);
                   });
    // Campaign columns of the unit rows.
    global.forEach("SELECT typeID, unitTechLevel, Prerequisite, CostToNextTL, Category, DamageArea, IsAA, DamageAA "
                   "FROM \"Unit Types\"",
                   [&](const data::Row& row) {
                       const auto it = r.units.find({toInt(row, 0), toInt(row, 1)});
                       if (it == r.units.end()) return;
                       UnitType& u = it->second;
                       u.prerequisite = row.isNull(2) ? 65535 : toInt(row, 2);
                       u.costToNextTL = row.isNull(3) ? 0 : toInt(row, 3);
                       u.category = toInt(row, 4);
                       u.weaponArea = toInt(row, 5);
                       u.isAA = row.integer(6) != 0;
                       u.damageAA = toInt(row, 7);
                   });
    global.forEach("SELECT TypeID, ImageID, SizeX, SizeY, Armor, DamagedImages, ExplosionAnimation_Begin, Race, "
                   "isDestructible, hasRuin, AIcategory, NameEnglish, TechReward, BuildingCat, CanBuild, Prerequisite, "
                   "UniqueOnMap, TransformIntoUnit, Position, Cost, Recycle, ResProduction FROM \"Building Types\"",
                   [&](const data::Row& row) {
                       BuildingType b{toUpper(row.text(1)), toInt(row, 2),         toInt(row, 3),
                                      toInt(row, 4),        row.integer(5) != 0,   row.integer(6) != -1,
                                      toInt(row, 7),        row.integer(8) != 0,   row.integer(9) != 0,
                                      toInt(row, 0),        toInt(row, 10),        row.isNull(11) ? std::string() : row.text(11),
                                      toInt(row, 12),       toInt(row, 13)};
                       b.canBuild = row.integer(14) != 0;
                       b.prerequisite = toInt(row, 15);
                       b.uniqueOnMap = row.integer(16) != 0;
                       b.transformIntoUnit = toInt(row, 17);
                       b.position = toInt(row, 18);
                       b.cost = toInt(row, 19);
                       b.recycle = toInt(row, 20);
                       b.resProduction = toInt(row, 21);
                       r.buildings[b.id] = std::move(b);
                   });
    global.forEach("SELECT RoadType, img_id, ar, nameEnglish, canBeBuilt, Cost, Recycle FROM \"Road Types\"",
                   [&](const data::Row& row) {
                       RoadType t{toUpper(row.text(1)), toInt(row, 2)};
                       t.id = toInt(row, 0);
                       t.name = row.text(3);
                       t.canBuild = row.integer(4) != 0;
                       t.cost = toInt(row, 5);
                       t.recycle = toInt(row, 6);
                       r.roads[t.id] = std::move(t);
                   });
    global.forEach("SELECT WallType, img_id, Armor, nameEnglish, prerequisite, canBeBuilt, Cost, Recycle FROM \"Wall Types\"",
                   [&](const data::Row& row) {
                       WallType t{toUpper(row.text(1)), toInt(row, 2)};
                       t.id = toInt(row, 0);
                       t.name = row.text(3);
                       t.prerequisite = toInt(row, 4);
                       t.canBuild = row.integer(5) != 0;
                       t.cost = toInt(row, 6);
                       t.recycle = toInt(row, 7);
                       r.walls[t.id] = std::move(t);
                   });
    global.forEach("SELECT typeID, ImageID, damageType, damageBase, damageRandom, nameEnglish, prerequisite, CanBuy, Cost, "
                   "Recycle, deployTerrain, listboxImageID FROM \"Mine Types\"",
                   [&](const data::Row& row) {
                       MineType t{toUpper(row.text(1)), toInt(row, 2) == 1, toInt(row, 3), toInt(row, 4)};
                       t.id = toInt(row, 0);
                       t.name = row.text(5);
                       t.prerequisite = toInt(row, 6);
                       t.canBuy = row.integer(7) != 0;
                       t.cost = toInt(row, 8);
                       t.recycle = toInt(row, 9);
                       t.deployTerrain = toInt(row, 10);
                       t.listbox = toUpper(row.text(11));
                       r.mines[t.id] = std::move(t);
                   });
    if (hasColumn(global, "Mine Types", "ExplosionAnimation"))
        global.forEach("SELECT typeID, ExplosionAnimation FROM \"Mine Types\"", [&](const data::Row& row) {
            const auto it = r.mines.find(toInt(row, 0));
            if (it != r.mines.end()) it->second.hasExplosion = row.integer(1) != -1;
        });
    if (global.hasTable("Automated Units"))
        global.forEach("SELECT typeID, NameEnglish, Race, Category, Cost, Prerequisite, DeployOnce, facingsImageID, cursorID, "
                       "ExplainEnglish, FrameAA, FrameAction, AAPosX, AAPosY FROM \"Automated Units\"",
                       [&](const data::Row& row) {
                           AutomatedType a;
                           a.id = toInt(row, 0);
                           a.name = row.text(1);
                           a.race = toInt(row, 2);
                           a.category = toInt(row, 3);
                           a.cost = toInt(row, 4);
                           a.prerequisite = toInt(row, 5);
                           a.deployOnce = row.integer(6) != 0;
                           a.image = toUpper(row.text(7));
                           a.cursor = toUpper(row.text(8));
                           a.explain = row.isNull(9) ? std::string() : row.text(9);
                           a.frameAA = row.isNull(10) ? -1 : toInt(row, 10);
                           a.frameAction = row.isNull(11) ? -1 : toInt(row, 11);
                           a.aaX = toInt(row, 12);
                           a.aaY = toInt(row, 13);
                           r.automated[a.id] = std::move(a);
                       });
    // Per-type properties (automated-units.md 1.2), values stored as text.
    if (global.hasTable("Automated Units Infos"))
        global.forEach("SELECT typeID, Property, Value FROM \"Automated Units Infos\"", [&](const data::Row& row) {
            const auto it = r.automated.find(toInt(row, 0));
            if (it == r.automated.end()) return;
            AutomatedType& a = it->second;
            const std::string text = row.isNull(2) ? std::string() : row.text(2);
            const int v = static_cast<int>(std::strtol(text.c_str(), nullptr, 10));
            const int p = toInt(row, 1);
            switch (p) {
            case 0: a.armor = v; break;
            case 1: a.damage = v; break;
            case 2: a.armorDamage = toUpper(text) == "AR"; break;
            case 3: a.mineDetectionRange = v; break;
            case 4: a.damageArea = v; break;
            case 5: a.dropUnit = v; break;
            case 7: a.dropCount = v; break;
            case 24: a.secondAAX = v; break;
            case 25: a.secondAAY = v; break;
            case 26: a.secondFrameAA = v; break;
            case 27: a.secondFrameAction = v; break;
            case 28: a.armorInc = v; break;
            case 34: a.secondExplain = text; break;
            case 35: a.actionInc = v; break;
            default:
                if (p >= 8 && p <= 17) { // unitPosX1, unitPosY1, ... unitPosX5, unitPosY5
                    DropSlot& s = a.dropSlots[static_cast<std::size_t>((p - 8) / 2)];
                    ((p - 8) % 2 == 0 ? s.dx : s.dy) = v;
                } else if (p >= 29 && p <= 33) { // facing1 ... facing5
                    a.dropSlots[static_cast<std::size_t>(p - 29)].facing = v;
                }
                break;
            }
        });
    if (global.hasTable("Tech Ratio"))
        global.forEach("SELECT MinRange, MaxRange, Ratio FROM \"Tech Ratio\" ORDER BY MinRange", [&](const data::Row& row) {
            r.techRatios.push_back({toInt(row, 0), toInt(row, 1), toInt(row, 2)});
        });
    global.forEach("SELECT id, type, can_fire_through, burned_id, Color_Red, Color_Green, Color_Blue FROM \"Terrain Types\"",
                   [&](const data::Row& row) {
                       r.terrain[toUpper(row.text(0))] =
                           TerrainType{static_cast<TerrainClass>(toInt(row, 1)), row.integer(2) != 0,
                                       row.isNull(3) ? std::string() : toUpper(row.text(3)),
                                       Rgb{toInt(row, 4), toInt(row, 5), toInt(row, 6)}};
                   });
    if (hasColumn(global, "Terrain Types", "Overlapping"))
        global.forEach("SELECT id, Overlapping FROM \"Terrain Types\"", [&](const data::Row& row) {
            const auto it = r.terrain.find(toUpper(row.text(0)));
            if (it != r.terrain.end()) it->second.overlapping = row.integer(1) != 0;
        });
    if (global.hasTable("Colors"))
        global.forEach("SELECT Type, Enum, Color_Red, Color_Green, Color_Blue FROM \"Colors\"", [&](const data::Row& row) {
            r.colors[{toInt(row, 0), toInt(row, 1)}] = Rgb{toInt(row, 2), toInt(row, 3), toInt(row, 4)};
        });
    global.forEach("SELECT Short, Medium FROM \"Ranges\"",
                   [&](const data::Row& row) { r.ranges = {toInt(row, 0), toInt(row, 1)}; });
    if (global.hasTable("AI Priority"))
        global.forEach("SELECT AiSource, AiTarget, Priority FROM \"AI Priority\"", [&](const data::Row& row) {
            const int s = toInt(row, 0), t = toInt(row, 1);
            if (s >= 0 && s < 12 && t >= 0 && t < 12)
                r.aiPriority[static_cast<std::size_t>(s)][static_cast<std::size_t>(t)] = toInt(row, 2);
        });
    global.forEach("SELECT DamageEasy, DamageHard, DamageAvg, CreditEasy, CreditAvg, CreditHard, DeployEasy, DeployAvg, "
                   "DeployHard FROM \"Difficulty\"",
                   [&](const data::Row& row) {
                       r.damageEasy = toInt(row, 0);
                       r.damageHard = toInt(row, 1);
                       r.damageAvg = toInt(row, 2);
                       r.creditPercent = {toInt(row, 3), toInt(row, 4), toInt(row, 5)};
                       r.deployPercent = {toInt(row, 6), toInt(row, 7), toInt(row, 8)};
                   });
    global.forEach("SELECT id, can_fire_through, can_move_through FROM \"Ruin Types\"", [&](const data::Row& row) {
        r.ruins[toUpper(row.text(0))] = RuinType{row.integer(1) != 0, row.integer(2) != 0};
    });
    return r;
}

} // namespace openday::game
