#include "map/terrain_map.hpp"

#include <algorithm>
#include <cctype>
#include <tuple>

namespace openday::game {

TerrainMap loadTerrainMap(const data::SqliteDb& scenario) {
    if (!scenario.hasTable("Map")) throw data::DbError("scenario has no Map table");

    std::vector<std::tuple<int, int, std::string>> rows;
    int maxCoord = -1;
    scenario.forEach("SELECT PosX, PosY, TerrainImg FROM \"Map\"", [&](const data::Row& r) {
        const int x = static_cast<int>(r.integer(0));
        const int y = static_cast<int>(r.integer(1));
        std::string name = r.text(2);
        // Sprite names are mixed case in the databases but upper case in the DBI index.
        std::transform(name.begin(), name.end(), name.begin(),
                       [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        maxCoord = std::max({maxCoord, x, y});
        rows.emplace_back(x, y, std::move(name));
    });

    TerrainMap map;
    map.size = maxCoord + 1;
    if (map.size <= 0 || rows.size() != static_cast<std::size_t>(map.size) * static_cast<std::size_t>(map.size))
        throw data::DbError("Map table does not describe a full square map");
    map.tiles.resize(rows.size());
    for (auto& [x, y, name] : rows) {
        if (x < 0 || y < 0) throw data::DbError("negative map coordinate");
        map.tiles[static_cast<std::size_t>(y * map.size + x)] = std::move(name);
    }
    return map;
}

} // namespace openday::game
