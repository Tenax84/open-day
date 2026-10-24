// Objects placed on a scenario map (roads, walls, mines, ruins, buildings,
// units), resolved to sprite names. Rules: docs/mechanics/map-objects.md.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "data/sqlite_db.hpp"
#include "map/rules.hpp"
#include "map/terrain_map.hpp"

namespace openday::game {

// The DBI archive a sprite comes from.
enum class SpriteSet { Terrain, Roads, Walls, Mines, Ruins, UnitsBuildings, Symbols, ListBox };
inline constexpr std::size_t kSpriteSetCount = 8;

// Draw order of objects on the same tile, after the terrain
// (docs/mechanics/map-objects.md, "Drawing").
enum class Layer { Road = 0, Mine = 4, Ruin = 5, Building = 10, Wall = 11, Unit = 15 };

struct MapSprite {
    int x = 0, y = 0; // tile
    Layer layer = Layer::Road;
    SpriteSet set = SpriteSet::Roads;
    std::string name; // upper case
    int dy = 0;       // vertical pixel offset (units on bridges are drawn 5 px higher)
    int entity = -1;  // Unit / Building layer: index into MapEntities::units / buildings
    int mode = 0;     // draw mode: low 4 bits = blit, 16 = drawn after all cells ("Draw mode")
};

// Draw mode bit: the sprite is drawn after all cells, on top of everything.
inline constexpr int kDrawOnTop = 16;

// Player slot -> color value, built per client (map-objects.md, "Color
// variant"): the identity, with the local player's slot and chosen color
// swapped. Single player: the identity.
struct ColorTable {
    std::array<int, 5> color{0, 1, 2, 3, 4};
    // Color value of an owner slot; slots outside 0-4 get 0.
    int operator()(int owner) const { return owner >= 0 && owner < 5 ? color[static_cast<std::size_t>(owner)] : 0; }
};
ColorTable makeColorTable(int localSlot, int chosenColor);

// A unit or building placed on the map, for animating and changing it.
struct MapUnit {
    const UnitType* type = nullptr; // points into GlobalRules
    int color = 0;                  // color value: ColorTable(owner)
    int facing = 0;                 // 0-7, docs/mechanics/unit-animation.md
    int x = 0, y = 0;
    bool alive = true;
    int mode = 0; // explicit draw mode (1 = placed in the deployment view), 0 = from the AP
    int ap = -1;  // usable AP (currentAP - negAP), -1 = unknown
};

// Draw mode of a unit's static sprite: its explicit mode, otherwise 1
// (darkened) when its usable AP is below both its shot cost and 4.
int unitDrawMode(const MapUnit& unit);

struct MapBuilding {
    const BuildingType* type = nullptr; // points into GlobalRules
    int x = 0, y = 0;                   // footprint corner with the smallest x and y
    bool alive = true;
    int color = 0; // color value: ColorTable(owner)
    int armor = 0; // currentAR, selects the damaged sprites
};

struct MapEntities {
    std::vector<MapUnit> units;
    std::vector<MapBuilding> buildings;
    ColorTable colors; // the table the color values were made with
};

// The damaged sprites are used once armor drops to a third of the maximum:
// currentAR <= 0.33 x Armor (floating point, as in the original).
bool isDamaged(int currentArmor, int maxArmor);

// The per-tile UB.DBI sprites of a building: <ImageID><c><d><i><j>, or for a
// tower building <code><c>0<i><j> with the facing fixed to 0 and no damage
// digit (code = the tower unit type's facingsImageID).
std::vector<MapSprite> buildingSprites(const MapBuilding& building, int entity, const GlobalRules& rules,
                                       const TerrainMap& terrain);

// MINES.DBI name of a mine: ImageID + 001 when armed, 000 when not.
std::string mineSpriteName(const MineType& type, bool armed);

// Neighbor directions on the isometric grid, as bits of a connection mask.
// NE = (x, y-1), SE = (x+1, y), SW = (x, y+1), NW = (x-1, y).
enum Neighbor : std::uint8_t { kNE = 1, kSE = 2, kSW = 4, kNW = 8 };

// Connection variant (0-15) of a road or wall piece for a neighbor mask.
int roadVariant(std::uint8_t mask);

// Number of a riverside road (bridge ramp) piece, 16-31, or 32 when the
// combination has no sprite. `orientation` is 0-3, see map-objects.md.
int rampNumber(int variant, int orientation);

struct RoadPiece {
    int type = 0; // Road Types.RoadType
    int x = 0, y = 0;
};

struct WallPiece {
    int type = 0; // Wall Types.WallType
    int x = 0, y = 0;
    int armor = 0;          // currentAR
    bool brokenEnd = false; // Walls On Map.isDamaged
    int previous = -1;      // sprite number of the previous computation
};

// Road sprites with their connection variants.
std::vector<MapSprite> roadSprites(const std::vector<RoadPiece>& roads, const GlobalRules& rules, const TerrainMap& terrain);
// Wall sprites; updates each piece's `previous` number.
std::vector<MapSprite> wallSprites(std::vector<WallPiece>& walls, const GlobalRules& rules, const TerrainMap& terrain);

// The objects of a scenario map, as stored in its "... On Map" tables.
struct ScenarioObjects {
    struct Mine {
        int type = 0, x = 0, y = 0;
        bool armed = true;
    };
    struct Ruin {
        std::string name;
        int x = 0, y = 0;
    };
    struct Building {
        int type = 0, owner = 0, x = 0, y = 0, armor = 0;
    };
    struct Unit {
        int type = 0, techLevel = 0, owner = 0, facing = 0, x = 0, y = 0;
    };
    std::vector<RoadPiece> roads;
    std::vector<WallPiece> walls;
    std::vector<Mine> mines;
    std::vector<Ruin> ruins;
    std::vector<Building> buildings;
    std::vector<Unit> units;
};

ScenarioObjects readScenarioObjects(const data::SqliteDb& scenario);

// Object sprites of a map. When `entities` is given, it receives the units
// and buildings, and their sprites refer to them through MapSprite::entity.
// `colors` turns owners into color values.
std::vector<MapSprite> mapObjectSprites(ScenarioObjects objects, const GlobalRules& rules, const TerrainMap& terrain,
                                        MapEntities* entities = nullptr, const ColorTable& colors = {});

// Builds the object sprites of a scenario from its "... On Map" tables.
// When `entities` is given, it receives the units and buildings, and their
// sprites refer to them through MapSprite::entity.
std::vector<MapSprite> loadMapObjects(const data::SqliteDb& scenario, const GlobalRules& rules, const TerrainMap& terrain,
                                      MapEntities* entities = nullptr, const ColorTable& colors = {});

// Static UB.DBI sprite of a unit at its tile. `onBridge`: the tile is deep
// water with a road on it.
MapSprite unitSprite(const MapUnit& unit, int entity, const TerrainMap& terrain, const GlobalRules& rules, bool onBridge);

// Formats a sprite number as three digits ("7" -> "007").
std::string threeDigits(int n);

} // namespace openday::game
