// Terrain map loading and isometric rendering on synthetic data: a generated
// SQLite scenario and a generated DBI archive.

#include "data/sqlite_db.hpp"
#include "formats/dbi.hpp"
#include "map/map_renderer.hpp"
#include "map/terrain_map.hpp"
#include "render/indexed_surface.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>

using namespace openday;

namespace {

int failures = 0;

#define CHECK(cond)                                                                       \
    do {                                                                                  \
        if (!(cond)) {                                                                    \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                                   \
        }                                                                                 \
    } while (0)

// A flat 54 x 56 terrain tile: only the ground diamond (rows 28-55) is opaque.
formats::DbiSprite flatTile(const std::string& name, std::uint8_t color) {
    formats::DbiSprite s{name, game::iso::kSpriteWidth, game::iso::kSpriteHeight, {}, {}};
    s.pixels.assign(static_cast<std::size_t>(s.width * s.height), color);
    s.opaque.assign(s.pixels.size(), 0);
    for (int y = game::iso::kDiamondTop; y < s.height; ++y) {
        const int r = y - game::iso::kDiamondTop;              // 0..27
        const int half = r < 14 ? 1 + 2 * r : 1 + 2 * (27 - r); // 1..27..1
        for (int x = 27 - half; x < 27 + half; ++x) s.opaque[static_cast<std::size_t>(y * s.width + x)] = 1;
    }
    return s;
}

// A tile with a 4 x 20 "tree" rising above its diamond.
formats::DbiSprite tallTile(const std::string& name, std::uint8_t ground, std::uint8_t tree) {
    formats::DbiSprite s = flatTile(name, ground);
    for (int y = 20; y < 40; ++y)
        for (int x = 25; x < 29; ++x) {
            const std::size_t i = static_cast<std::size_t>(y * s.width + x);
            s.pixels[i] = tree;
            s.opaque[i] = 1;
        }
    return s;
}

std::filesystem::path makeScenario() {
    const auto path = std::filesystem::temp_directory_path() / "openday_map_test.sqlite";
    std::filesystem::remove(path);
    data::SqliteDb db(path, data::SqliteDb::Mode::ReadWriteCreate);
    db.exec("CREATE TABLE \"Map\" (\"Id\" INTEGER, \"PosX\" INTEGER, \"PosY\" INTEGER, \"TerrainImg\" TEXT)");
    // 2 x 2 map; names in mixed case as in the shipped databases.
    db.exec("INSERT INTO \"Map\" VALUES (1, 0, 0, 'Grass0'), (2, 1, 0, 'WATER0'), (3, 0, 1, 'grass0'), (4, 1, 1, 'Tree0')");
    return path;
}

void testGeometry() {
    // Neighbors along x go down-right, along y down-left, by half a tile.
    CHECK(game::iso::tileOrigin(0, 0, 2).x == 28 && game::iso::tileOrigin(0, 0, 2).y == 0);
    CHECK(game::iso::tileOrigin(1, 0, 2).x == 56 && game::iso::tileOrigin(1, 0, 2).y == 14);
    CHECK(game::iso::tileOrigin(0, 1, 2).x == 0 && game::iso::tileOrigin(0, 1, 2).y == 14);
    const game::Rect b = game::iso::mapBounds(2);
    CHECK(b.w == 110 && b.h == 84);
}

void testLoadAndRender() {
    const auto path = makeScenario();
    game::TerrainMap map;
    {
        data::SqliteDb db(path);
        map = game::loadTerrainMap(db);
    }
    std::filesystem::remove(path);
    CHECK(map.size == 2);
    CHECK(map.at(0, 0) == "GRASS0" && map.at(1, 0) == "WATER0" && map.at(1, 1) == "TREE0");

    formats::Palette pal{};
    auto archive = std::make_shared<const formats::DbiArchive>(formats::DbiArchive::fromBytes(
        formats::writeDbi({flatTile("GRASS0", 10), flatTile("WATER0", 20), tallTile("TREE0", 30, 40)}, pal, true)));
    game::SpriteCache cache(archive);
    CHECK(cache.get("GRASS0") != nullptr);
    CHECK(cache.get("MISSING") == nullptr);

    const game::Rect bounds = game::iso::mapBounds(map.size);
    render::IndexedSurface surface(bounds.w, bounds.h, 0);
    const game::RenderStats stats = game::drawTerrain(surface, bounds, map, cache);
    CHECK(stats.tilesDrawn == 4 && stats.missingSprites == 0);

    // Diamond centers: origin + (27, 42).
    auto centerColor = [&](int x, int y) {
        const game::Point o = game::iso::tileOrigin(x, y, map.size);
        return surface.at(o.x + 27 - 6, o.y + 42); // left of the tree trunk
    };
    CHECK(centerColor(0, 0) == 10);
    CHECK(centerColor(1, 0) == 20);
    CHECK(centerColor(0, 1) == 10);
    CHECK(centerColor(1, 1) == 30);

    // The tree of tile (1, 1) is drawn last and overlaps tile (0, 0)'s
    // diamond area above it.
    const game::Point back = game::iso::tileOrigin(1, 1, map.size);
    CHECK(surface.at(back.x + 26, back.y + 22) == 40);

    // Diamonds tile the plane: on every row of the ground the drawn pixels
    // form one gap-free run.
    bool gapFree = true;
    for (int y = game::iso::kDiamondTop; y < bounds.h; ++y) {
        int runs = 0;
        for (int x = 0; x < bounds.w; ++x)
            if (surface.at(x, y) != 0 && (x == 0 || surface.at(x - 1, y) == 0)) ++runs;
        if (runs != 1) gapFree = false;
    }
    CHECK(gapFree);

    // A view smaller than the map draws only what it covers.
    render::IndexedSurface small(30, 20, 0);
    const game::RenderStats part = game::drawTerrain(small, {0, 50, 30, 20}, map, cache);
    CHECK(part.tilesDrawn < 4 && part.tilesDrawn > 0);
}

} // namespace

int main() {
    try {
        testGeometry();
        testLoadAndRender();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "exception: %s\n", e.what());
        ++failures;
    }
    if (failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    std::puts("all map tests passed");
    return EXIT_SUCCESS;
}
