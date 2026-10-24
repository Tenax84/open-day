#include "map/map_objects.hpp"

#include <array>
#include <limits>
#include <map>
#include <set>
#include <tuple>
#include <utility>

namespace openday::game {
namespace {

using Tile = std::pair<int, int>;

std::uint8_t neighborMask(const std::set<Tile>& tiles, int x, int y) {
    std::uint8_t m = 0;
    if (tiles.count({x, y - 1})) m |= kNE;
    if (tiles.count({x + 1, y})) m |= kSE;
    if (tiles.count({x, y + 1})) m |= kSW;
    if (tiles.count({x - 1, y})) m |= kNW;
    return m;
}

int toInt(const data::Row& row, int col) { return static_cast<int>(row.integer(col)); }

std::string twoDigits(int n) { return threeDigits(n).substr(1); }

} // namespace

bool isDamaged(int currentArmor, int maxArmor) {
    return static_cast<double>(currentArmor) <= 0.33 * static_cast<double>(maxArmor);
}

ColorTable makeColorTable(int localSlot, int chosenColor) {
    ColorTable t;
    if (localSlot < 0 || localSlot > 4 || chosenColor < 0 || chosenColor > 4) return t;
    std::swap(t.color[static_cast<std::size_t>(localSlot)], t.color[static_cast<std::size_t>(chosenColor)]);
    return t;
}

int unitDrawMode(const MapUnit& unit) {
    if (unit.mode != 0) return unit.mode;
    return unit.ap >= 0 && unit.type && unit.ap < unit.type->shotAp && unit.ap < 4 ? 1 : 0;
}

std::string mineSpriteName(const MineType& type, bool armed) { return type.image + (armed ? "001" : "000"); }

std::vector<MapSprite> buildingSprites(const MapBuilding& b, int entity, const GlobalRules& rules, const TerrainMap& terrain) {
    const BuildingType& bt = *b.type;
    std::string prefix;
    if (bt.transformIntoUnit != 65535) {
        // A tower: the unit naming with facing 0 and no damage digit.
        std::string code = bt.image;
        const auto unit = rules.units.lower_bound({bt.transformIntoUnit, std::numeric_limits<int>::min()});
        if (unit != rules.units.end() && unit->first.first == bt.transformIntoUnit && !unit->second.image.empty())
            code = unit->second.image;
        prefix = code + std::to_string(b.color) + "0";
    } else {
        prefix = bt.image + std::to_string(b.color) + (bt.damagedImages && isDamaged(b.armor, bt.armor) ? "1" : "0");
    }
    std::vector<MapSprite> out;
    for (int dy = 0; dy < bt.sizeY; ++dy)
        for (int dx = 0; dx < bt.sizeX; ++dx) {
            const int x = b.x + dx, y = b.y + dy;
            if (terrain.inside(x, y))
                out.push_back({x, y, Layer::Building, SpriteSet::UnitsBuildings, prefix + std::to_string(dx) + std::to_string(dy), 0,
                               entity});
        }
    return out;
}

std::string threeDigits(int n) {
    std::string s = std::to_string(n);
    while (s.size() < 3) s.insert(s.begin(), '0');
    return s;
}

int roadVariant(std::uint8_t mask) {
    // Index: connection mask (NE=1, SE=2, SW=4, NW=8).
    static constexpr std::array<int, 16> kVariant = {
        0,  // none: isolated
        10, // NE: dead end
        8,  // SE: dead end
        12, // NE+SE: corner
        9,  // SW: dead end
        7,  // NE+SW: straight
        13, // SE+SW: corner
        2,  // NE+SE+SW: T
        11, // NW: dead end
        14, // NE+NW: corner
        6,  // SE+NW: straight
        4,  // NE+SE+NW: T
        15, // SW+NW: corner
        5,  // NE+SW+NW: T
        3,  // SE+SW+NW: T
        1,  // all: crossing
    };
    return kVariant[mask & 15];
}

int rampNumber(int variant, int orientation) {
    // Orientations 0/1 ramp along the NW-SE axis, 2/3 along NE-SW.
    switch (variant) {
    case 0: return 16 + 4 * orientation;
    case 6: return orientation < 2 ? 17 + 4 * orientation : 32;
    case 8: return orientation < 2 ? 18 + 4 * orientation : 32;
    case 11: return orientation < 2 ? 19 + 4 * orientation : 32;
    case 7: return orientation >= 2 ? 25 + 4 * (orientation - 2) : 32;
    case 9: return orientation >= 2 ? 26 + 4 * (orientation - 2) : 32;
    case 10: return orientation >= 2 ? 27 + 4 * (orientation - 2) : 32;
    default: return 32;
    }
}

MapSprite unitSprite(const MapUnit& unit, int entity, const TerrainMap& terrain, const GlobalRules& rules, bool onBridge) {
    const bool inForest = unit.type->movement == 2 && terrain.inside(unit.x, unit.y) &&
                          rules.classOf(terrain.at(unit.x, unit.y)) == TerrainClass::Forest;
    const int facing = unit.facing & 7;
    const char facingChar = static_cast<char>(inForest ? 'A' + facing : '0' + facing);
    return {unit.x, unit.y, Layer::Unit, SpriteSet::UnitsBuildings,
            unit.type->image + std::to_string(unit.color) + facingChar + "00", onBridge ? -5 : 0, entity, unitDrawMode(unit)};
}

std::vector<MapSprite> roadSprites(const std::vector<RoadPiece>& roads, const GlobalRules& rules, const TerrainMap& terrain) {
    // Roads join only roads of the same type; the sprite set follows the
    // terrain class of the road's own tile.
    auto classAt = [&](int x, int y) {
        return terrain.inside(x, y) ? rules.classOf(terrain.at(x, y)) : TerrainClass::Unknown;
    };
    auto isWaterSide = [&](int x, int y) {
        const TerrainClass c = classAt(x, y);
        return c == TerrainClass::DeepWater || c == TerrainClass::Riverside;
    };
    auto isLandSide = [&](int x, int y) { return classAt(x, y) == TerrainClass::Plain; };
    std::map<int, std::set<Tile>> roadTiles; // per road type
    for (const RoadPiece& r : roads) roadTiles[r.type].insert({r.x, r.y});
    std::vector<MapSprite> out;
    for (const auto& [type, x, y] : roads) {
        const auto road = rules.roads.find(type);
        if (road == rules.roads.end() || !terrain.inside(x, y)) continue;
        const std::string& image = road->second.image;
        const int variant = roadVariant(neighborMask(roadTiles[type], x, y));
        std::string name;
        switch (classAt(x, y)) {
        case TerrainClass::DeepWater:
            name = image + "B" + threeDigits(variant);
            break;
        case TerrainClass::Riverside: {
            int orientation = 3;
            if (isWaterSide(x - 1, y) && isLandSide(x + 1, y)) orientation = 0;
            else if (isWaterSide(x + 1, y) && isLandSide(x - 1, y)) orientation = 1;
            else if (isWaterSide(x, y - 1) && isLandSide(x, y + 1)) orientation = 2;
            name = image + "R" + threeDigits(rampNumber(variant, orientation));
            break;
        }
        default:
            name = image + "G" + threeDigits(variant);
            break;
        }
        out.push_back({x, y, Layer::Road, SpriteSet::Roads, std::move(name)});
    }
    return out;
}

std::vector<MapSprite> wallSprites(std::vector<WallPiece>& walls, const GlobalRules& rules, const TerrainMap& terrain) {
    // Walls join only walls of the same type. The damage digit comes from
    // armor; brokenEnd (the isDamaged column) marks a broken end.
    std::map<int, std::set<Tile>> wallTiles;
    for (const WallPiece& w : walls) wallTiles[w.type].insert({w.x, w.y});
    std::vector<MapSprite> out;
    for (WallPiece& w : walls) {
        const auto wall = rules.walls.find(w.type);
        if (wall == rules.walls.end() || !terrain.inside(w.x, w.y)) continue;
        int number = roadVariant(neighborMask(wallTiles[w.type], w.x, w.y));
        if (w.brokenEnd) {
            static constexpr std::pair<int, int> kBroken[] = {{10, 16}, {11, 17}, {9, 18}, {8, 19}};
            for (const auto& [from, to] : kBroken)
                if (number == from) number = to;
            // An isolated piece keeps a straight shape it had before.
            if (number == 0 && w.previous == 7) number = 20;
            else if (number == 0 && w.previous == 6) number = 21;
        }
        w.previous = number;
        out.push_back({w.x, w.y, Layer::Wall, SpriteSet::Walls,
                       wall->second.image + (isDamaged(w.armor, wall->second.armor) ? "1" : "0") + twoDigits(number)});
    }
    return out;
}

ScenarioObjects readScenarioObjects(const data::SqliteDb& scenario) {
    ScenarioObjects o;
    scenario.forEach("SELECT roadtype, PosX, PosY FROM \"Roads On Map\"", [&](const data::Row& r) {
        o.roads.push_back({toInt(r, 0), toInt(r, 1), toInt(r, 2)});
    });
    scenario.forEach("SELECT roadtype, posX, posY, currentAR, isDamaged FROM \"Walls On Map\"", [&](const data::Row& r) {
        o.walls.push_back({toInt(r, 0), toInt(r, 1), toInt(r, 2), toInt(r, 3), r.integer(4) != 0});
    });
    scenario.forEach("SELECT typeID, PosX, PosY, armed FROM \"Mines On Map\"", [&](const data::Row& r) {
        o.mines.push_back({toInt(r, 0), toInt(r, 1), toInt(r, 2), r.integer(3) != 0});
    });
    scenario.forEach("SELECT id, PosX, PosY FROM \"Ruins On Map\"",
                     [&](const data::Row& r) { o.ruins.push_back({toUpper(r.text(0)), toInt(r, 1), toInt(r, 2)}); });
    scenario.forEach("SELECT buildingType, playerOwner, PosX, PosY, currentAR FROM \"Buildings On Map\"",
                     [&](const data::Row& r) {
                         o.buildings.push_back({toInt(r, 0), toInt(r, 1), toInt(r, 2), toInt(r, 3), toInt(r, 4)});
                     });
    scenario.forEach("SELECT unitType, unitTL, playerOwner, facing, PosX, PosY FROM \"Units On Map\"",
                     [&](const data::Row& r) {
                         o.units.push_back({toInt(r, 0), toInt(r, 1), toInt(r, 2), toInt(r, 3), toInt(r, 4), toInt(r, 5)});
                     });
    return o;
}

std::vector<MapSprite> mapObjectSprites(ScenarioObjects objects, const GlobalRules& rules, const TerrainMap& terrain,
                                        MapEntities* entities, const ColorTable& colors) {
    MapEntities local;
    MapEntities& ent = entities ? *entities : local;
    ent = {};
    ent.colors = colors;
    std::vector<MapSprite> out;
    auto inside = [&](int x, int y) { return x >= 0 && y >= 0 && x < terrain.size && y < terrain.size; };
    // Terrain class of a tile; tiles outside the map count as unknown.
    auto classAt = [&](int x, int y) {
        return inside(x, y) ? rules.classOf(terrain.at(x, y)) : TerrainClass::Unknown;
    };

    std::set<Tile> anyRoad;
    for (const RoadPiece& r : objects.roads) anyRoad.insert({r.x, r.y});
    for (MapSprite& sprite : roadSprites(objects.roads, rules, terrain)) out.push_back(std::move(sprite));
    for (MapSprite& sprite : wallSprites(objects.walls, rules, terrain)) out.push_back(std::move(sprite));

    // Mines: ImageID + 001 (armed) or 000. All of them are shown here; a
    // battle shows only those the local player knows (BattleView).
    for (const ScenarioObjects::Mine& m : objects.mines) {
        const auto mine = rules.mines.find(m.type);
        if (mine != rules.mines.end() && inside(m.x, m.y))
            out.push_back({m.x, m.y, Layer::Mine, SpriteSet::Mines, mineSpriteName(mine->second, m.armed)});
    }

    for (const ScenarioObjects::Ruin& r : objects.ruins)
        if (inside(r.x, r.y)) out.push_back({r.x, r.y, Layer::Ruin, SpriteSet::Ruins, r.name});

    // Buildings: one UB.DBI sprite per footprint tile,
    // <code><color><damaged><offset along PosX><offset along PosY>.
    for (const ScenarioObjects::Building& b : objects.buildings) {
        const auto type = rules.buildings.find(b.type);
        if (type == rules.buildings.end()) continue;
        const int entity = static_cast<int>(ent.buildings.size());
        ent.buildings.push_back({&type->second, b.x, b.y, true, colors(b.owner), b.armor});
        for (MapSprite& sprite : buildingSprites(ent.buildings.back(), entity, rules, terrain)) out.push_back(std::move(sprite));
    }

    // Units: <code><color><facing>00. Infantry in forest uses the facing
    // letters A-H; units on bridges are lifted by 5 px.
    for (const ScenarioObjects::Unit& u : objects.units) {
        const auto type = rules.units.find({u.type, u.techLevel});
        if (type == rules.units.end() || !inside(u.x, u.y)) continue;
        const MapUnit unit{&type->second, colors(u.owner), u.facing & 7, u.x, u.y};
        const bool onBridge = classAt(u.x, u.y) == TerrainClass::DeepWater && anyRoad.count({u.x, u.y});
        out.push_back(unitSprite(unit, static_cast<int>(ent.units.size()), terrain, rules, onBridge));
        ent.units.push_back(unit);
    }
    return out;
}

std::vector<MapSprite> loadMapObjects(const data::SqliteDb& scenario, const GlobalRules& rules, const TerrainMap& terrain,
                                      MapEntities* entities, const ColorTable& colors) {
    return mapObjectSprites(readScenarioObjects(scenario), rules, terrain, entities, colors);
}

} // namespace openday::game
