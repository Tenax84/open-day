#include "map/map_objects.hpp"

#include <array>
#include <map>
#include <set>
#include <tuple>
#include <utility>

namespace openday::game {
namespace {

using Tile = std::pair<int, int>;

// Damaged sprites are shown once armor drops to a third of the type's maximum.
bool isDamaged(int currentArmor, int maxArmor) { return currentArmor * 100 <= maxArmor * 33; }

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

std::vector<MapSprite> loadMapObjects(const data::SqliteDb& scenario, const GlobalRules& rules, const TerrainMap& terrain) {
    std::vector<MapSprite> out;
    auto inside = [&](int x, int y) { return x >= 0 && y >= 0 && x < terrain.size && y < terrain.size; };
    // Terrain class of a tile; tiles outside the map count as unknown.
    auto classAt = [&](int x, int y) {
        return inside(x, y) ? rules.classOf(terrain.at(x, y)) : TerrainClass::Unknown;
    };
    auto isWaterSide = [&](int x, int y) {
        const TerrainClass c = classAt(x, y);
        return c == TerrainClass::DeepWater || c == TerrainClass::Riverside;
    };
    auto isLandSide = [&](int x, int y) { return classAt(x, y) == TerrainClass::Plain; };

    // Roads join only roads of the same type; the sprite set follows the
    // terrain class of the road's own tile.
    std::vector<std::tuple<int, int, int>> roads;
    std::map<int, std::set<Tile>> roadTiles; // per road type
    std::set<Tile> anyRoad;
    scenario.forEach("SELECT roadtype, PosX, PosY FROM \"Roads On Map\"", [&](const data::Row& r) {
        roads.emplace_back(toInt(r, 0), toInt(r, 1), toInt(r, 2));
        roadTiles[toInt(r, 0)].insert({toInt(r, 1), toInt(r, 2)});
        anyRoad.insert({toInt(r, 1), toInt(r, 2)});
    });
    for (const auto& [type, x, y] : roads) {
        const auto img = rules.roadImage.find(type);
        if (img == rules.roadImage.end() || !inside(x, y)) continue;
        const int variant = roadVariant(neighborMask(roadTiles[type], x, y));
        std::string name;
        switch (classAt(x, y)) {
        case TerrainClass::DeepWater:
            name = img->second + "B" + threeDigits(variant);
            break;
        case TerrainClass::Riverside: {
            int orientation = 3;
            if (isWaterSide(x - 1, y) && isLandSide(x + 1, y)) orientation = 0;
            else if (isWaterSide(x + 1, y) && isLandSide(x - 1, y)) orientation = 1;
            else if (isWaterSide(x, y - 1) && isLandSide(x, y + 1)) orientation = 2;
            name = img->second + "R" + threeDigits(rampNumber(variant, orientation));
            break;
        }
        default:
            name = img->second + "G" + threeDigits(variant);
            break;
        }
        out.push_back({x, y, Layer::Road, SpriteSet::Roads, std::move(name)});
    }

    // Walls join only walls of the same type. The damage digit comes from
    // armor; the isDamaged column marks a broken end.
    std::vector<std::tuple<int, int, int, int, bool>> walls;
    std::map<int, std::set<Tile>> wallTiles;
    scenario.forEach("SELECT roadtype, posX, posY, currentAR, isDamaged FROM \"Walls On Map\"", [&](const data::Row& r) {
        walls.emplace_back(toInt(r, 0), toInt(r, 1), toInt(r, 2), toInt(r, 3), r.integer(4) != 0);
        wallTiles[toInt(r, 0)].insert({toInt(r, 1), toInt(r, 2)});
    });
    for (const auto& [type, x, y, armor, brokenEnd] : walls) {
        const auto wall = rules.walls.find(type);
        if (wall == rules.walls.end() || !inside(x, y)) continue;
        int number = roadVariant(neighborMask(wallTiles[type], x, y));
        if (brokenEnd) {
            // Freshly loaded walls have no previous shape, so an isolated
            // piece stays a plain post (0).
            static constexpr std::pair<int, int> kBroken[] = {{10, 16}, {11, 17}, {9, 18}, {8, 19}};
            for (const auto& [from, to] : kBroken)
                if (number == from) number = to;
        }
        out.push_back({x, y, Layer::Wall, SpriteSet::Walls,
                       wall->second.image + (isDamaged(armor, wall->second.armor) ? "1" : "0") + twoDigits(number)});
    }

    // Mines: ImageID + 000 or 001; which state selects 001 is unknown, so
    // every mine uses 000 for now.
    scenario.forEach("SELECT typeID, PosX, PosY FROM \"Mines On Map\"", [&](const data::Row& r) {
        const auto img = rules.mineImage.find(toInt(r, 0));
        if (img != rules.mineImage.end() && inside(toInt(r, 1), toInt(r, 2)))
            out.push_back({toInt(r, 1), toInt(r, 2), Layer::Mine, SpriteSet::Mines, img->second + "000"});
    });

    scenario.forEach("SELECT id, PosX, PosY FROM \"Ruins On Map\"", [&](const data::Row& r) {
        if (inside(toInt(r, 1), toInt(r, 2)))
            out.push_back({toInt(r, 1), toInt(r, 2), Layer::Ruin, SpriteSet::Ruins, toUpper(r.text(0))});
    });

    // Buildings: one UB.DBI sprite per footprint tile,
    // <code><color><damaged><offset along PosX><offset along PosY>.
    // The color variant is the owner's slot (0 = AI) *(?)*.
    scenario.forEach("SELECT buildingType, playerOwner, PosX, PosY, currentAR FROM \"Buildings On Map\"",
                     [&](const data::Row& r) {
                         const auto type = rules.buildings.find(toInt(r, 0));
                         if (type == rules.buildings.end()) return;
                         const BuildingType& bt = type->second;
                         const std::string prefix = bt.image + std::to_string(toInt(r, 1)) +
                                                    (bt.damagedImages && isDamaged(toInt(r, 4), bt.armor) ? "1" : "0");
                         for (int dy = 0; dy < bt.sizeY; ++dy)
                             for (int dx = 0; dx < bt.sizeX; ++dx) {
                                 const int x = toInt(r, 2) + dx, y = toInt(r, 3) + dy;
                                 if (inside(x, y))
                                     out.push_back({x, y, Layer::Building, SpriteSet::UnitsBuildings,
                                                    prefix + std::to_string(dx) + std::to_string(dy)});
                             }
                     });

    // Units: <code><color><facing>00. Infantry in forest uses the facing
    // letters A-H; units on bridges are lifted by 5 px.
    scenario.forEach("SELECT unitType, unitTL, playerOwner, facing, PosX, PosY FROM \"Units On Map\"",
                     [&](const data::Row& r) {
                         const auto type = rules.units.find({toInt(r, 0), toInt(r, 1)});
                         const int x = toInt(r, 4), y = toInt(r, 5), facing = toInt(r, 3) & 7;
                         if (type == rules.units.end() || !inside(x, y)) return;
                         const bool inForest = type->second.movement == 2 && classAt(x, y) == TerrainClass::Forest;
                         const char facingChar = static_cast<char>(inForest ? 'A' + facing : '0' + facing);
                         const bool onBridge = classAt(x, y) == TerrainClass::DeepWater && anyRoad.count({x, y});
                         out.push_back({x, y, Layer::Unit, SpriteSet::UnitsBuildings,
                                        type->second.image + std::to_string(toInt(r, 2)) + facingChar + "00",
                                        onBridge ? -5 : 0});
                     });
    return out;
}

} // namespace openday::game
