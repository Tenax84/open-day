// Terrain layer of a scenario map. See docs/mechanics/data-model.md
// ("Scenario databases", table `Map`).
#pragma once

#include <string>
#include <vector>

#include "data/sqlite_db.hpp"

namespace openday::game {

struct TerrainMap {
    int size = 0;                   // maps are square: size x size tiles
    std::vector<std::string> tiles; // TERRAINS.DBI sprite names, upper case, index y * size + x

    const std::string& at(int x, int y) const { return tiles[static_cast<std::size_t>(y * size + x)]; }
    bool inside(int x, int y) const { return x >= 0 && y >= 0 && x < size && y < size; }
};

// Reads the `Map` table of a converted scenario database. Throws
// data::DbError when the table is missing or does not cover a full square.
TerrainMap loadTerrainMap(const data::SqliteDb& scenario);

} // namespace openday::game
