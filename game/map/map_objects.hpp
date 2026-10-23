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
enum class SpriteSet { Terrain, Roads, Walls, Mines, Ruins, UnitsBuildings };
inline constexpr std::size_t kSpriteSetCount = 6;

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
};

// A unit or building placed on the map, for animating and changing it.
struct MapUnit {
    const UnitType* type = nullptr; // points into GlobalRules
    int color = 0;                  // color variant (currently the owner slot *(?)*)
    int facing = 0;                 // 0-7, docs/mechanics/unit-animation.md
    int x = 0, y = 0;
    bool alive = true;
};

struct MapBuilding {
    const BuildingType* type = nullptr; // points into GlobalRules
    int x = 0, y = 0;                   // footprint corner with the smallest x and y
    bool alive = true;
};

struct MapEntities {
    std::vector<MapUnit> units;
    std::vector<MapBuilding> buildings;
};

// Neighbor directions on the isometric grid, as bits of a connection mask.
// NE = (x, y-1), SE = (x+1, y), SW = (x, y+1), NW = (x-1, y).
enum Neighbor : std::uint8_t { kNE = 1, kSE = 2, kSW = 4, kNW = 8 };

// Connection variant (0-15) of a road or wall piece for a neighbor mask.
int roadVariant(std::uint8_t mask);

// Number of a riverside road (bridge ramp) piece, 16-31, or 32 when the
// combination has no sprite. `orientation` is 0-3, see map-objects.md.
int rampNumber(int variant, int orientation);

// Builds the object sprites of a scenario from its "... On Map" tables.
// When `entities` is given, it receives the units and buildings, and their
// sprites refer to them through MapSprite::entity.
std::vector<MapSprite> loadMapObjects(const data::SqliteDb& scenario, const GlobalRules& rules, const TerrainMap& terrain,
                                      MapEntities* entities = nullptr);

// Static UB.DBI sprite of a unit at its tile. `onBridge`: the tile is deep
// water with a road on it.
MapSprite unitSprite(const MapUnit& unit, int entity, const TerrainMap& terrain, const GlobalRules& rules, bool onBridge);

// Formats a sprite number as three digits ("7" -> "007").
std::string threeDigits(int n);

} // namespace openday::game
