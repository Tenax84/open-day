// Terrain map loading and isometric rendering on synthetic data: a generated
// SQLite scenario and a generated DBI archive.

#include "data/sqlite_db.hpp"
#include "formats/dbi.hpp"
#include "map/map_objects.hpp"
#include "map/map_renderer.hpp"
#include "map/rules.hpp"
#include "map/terrain_map.hpp"
#include "render/indexed_surface.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <set>
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

    // Every pixel of a tile's diamond maps back to that tile.
    const int n = 5;
    for (int ty = 0; ty < n; ++ty)
        for (int tx = 0; tx < n; ++tx) {
            const game::Point o = game::iso::tileOrigin(tx, ty, n);
            for (int row = 0; row < 28; ++row) {
                const int half = row < 14 ? 1 + 2 * row : 1 + 2 * (27 - row); // half of the diamond width
                for (int col = 27 - half; col < 27 + half; ++col) {
                    const game::Point t = game::iso::tileAt(o.x + col, o.y + 28 + row, n);
                    CHECK(t.x == tx && t.y == ty);
                    if (t.x != tx || t.y != ty) return;
                }
            }
        }
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

void testRoadVariants() {
    using namespace game;
    CHECK(roadVariant(0) == 0);
    CHECK(roadVariant(kNE | kSE | kSW | kNW) == 1);
    CHECK(roadVariant(kSE | kNW) == 6 && roadVariant(kNE | kSW) == 7);
    CHECK(roadVariant(kSE) == 8 && roadVariant(kSW) == 9 && roadVariant(kNE) == 10 && roadVariant(kNW) == 11);
    CHECK(roadVariant(kNE | kSE) == 12 && roadVariant(kSE | kSW) == 13);
    CHECK(roadVariant(kNE | kNW) == 14 && roadVariant(kSW | kNW) == 15);
    CHECK(roadVariant(kNE | kSE | kSW) == 2 && roadVariant(kSE | kSW | kNW) == 3);
    CHECK(roadVariant(kNE | kSE | kNW) == 4 && roadVariant(kNE | kSW | kNW) == 5);
    // Every variant is used exactly once.
    std::set<int> seen;
    for (int m = 0; m < 16; ++m) seen.insert(roadVariant(static_cast<std::uint8_t>(m)));
    CHECK(seen.size() == 16);
}

void testMapObjects() {
    const auto path = std::filesystem::temp_directory_path() / "openday_map_objects_test.sqlite";
    std::filesystem::remove(path);
    std::vector<game::MapSprite> objects;
    game::TerrainMap terrain;
    {
        data::SqliteDb db(path, data::SqliteDb::Mode::ReadWriteCreate);
        db.exec("CREATE TABLE \"Map\" (\"Id\" INTEGER, \"PosX\" INTEGER, \"PosY\" INTEGER, \"TerrainImg\" TEXT)");
        for (int y = 0; y < 4; ++y)
            for (int x = 0; x < 4; ++x)
                db.exec("INSERT INTO \"Map\" VALUES (0, " + std::to_string(x) + ", " + std::to_string(y) +
                        (x == 3 && y == 3 ? ", 'eau000')" : ", 'grass0')"));
        db.exec("CREATE TABLE \"Roads On Map\" (\"Counter\" INTEGER, \"roadtype\" INTEGER, \"playerOwner\" INTEGER, \"PosX\" INTEGER, \"PosY\" INTEGER)");
        db.exec("INSERT INTO \"Roads On Map\" VALUES (1, 0, 0, 0, 0), (2, 0, 0, 1, 0), (3, 0, 0, 2, 0), (4, 1, 0, 3, 3)");
        db.exec("CREATE TABLE \"Walls On Map\" (\"Counter\" INTEGER, \"roadtype\" INTEGER, \"playerOwner\" INTEGER, \"currentAR\" INTEGER, \"posX\" INTEGER, \"posY\" INTEGER, \"isDamaged\" INTEGER)");
        db.exec("INSERT INTO \"Walls On Map\" VALUES (1, 0, 0, 10, 0, 3, 1)");
        db.exec("CREATE TABLE \"Mines On Map\" (\"Counter\" INTEGER, \"typeID\" INTEGER, \"playerOwner\" INTEGER, \"PosX\" INTEGER, \"PosY\" INTEGER, \"armed\" INTEGER)");
        db.exec("CREATE TABLE \"Ruins On Map\" (\"Counter\" INTEGER, \"id\" TEXT, \"PosX\" INTEGER, \"PosY\" INTEGER)");
        db.exec("CREATE TABLE \"Buildings On Map\" (\"Counter\" INTEGER, \"id\" INTEGER, \"buildingType\" INTEGER, \"playerOwner\" INTEGER, \"currentAR\" INTEGER, \"PosX\" INTEGER, \"PosY\" INTEGER)");
        db.exec("INSERT INTO \"Buildings On Map\" VALUES (1, 7, 130, 2, 100, 1, 1)");
        db.exec("CREATE TABLE \"Units On Map\" (\"Counter\" INTEGER, \"ID\" INTEGER, \"unitType\" INTEGER, \"unitTL\" INTEGER, \"playerOwner\" INTEGER, \"facing\" INTEGER, \"PosX\" INTEGER, \"PosY\" INTEGER)");
        db.exec("INSERT INTO \"Units On Map\" VALUES (1, 9, 5, 1, 0, 6, 1, 1)");

        game::GlobalRules rules;
        rules.roads = {{0, {"ROAL"}}, {1, {"ROAH"}}};
        rules.walls[0] = {"WALLC", 60}; // currentAR 10 <= 33% of 60: damaged
        rules.buildings[130] = {"HHQ", 2, 2};
        rules.units[{5, 1}] = {"BDN", 0};
        rules.terrain = {{"GRASS0", {game::TerrainClass::Plain}}, {"EAU000", {game::TerrainClass::DeepWater}}};
        terrain = game::loadTerrainMap(db);
        objects = game::loadMapObjects(db, rules, terrain);
    }
    std::filesystem::remove(path);

    std::set<std::string> names;
    for (const auto& o : objects) names.insert(std::to_string(o.x) + "," + std::to_string(o.y) + ":" + o.name);
    // A straight road along x: dead end, straight, dead end; the lone
    // high-tech piece over deep water becomes a bridge.
    CHECK(names.count("0,0:ROALG008") && names.count("1,0:ROALG006") && names.count("2,0:ROALG011"));
    CHECK(names.count("3,3:ROAHB000"));
    CHECK(names.count("0,3:WALLC100"));
    // A 2 x 2 building of player 2 at (1, 1): one sprite per footprint tile.
    CHECK(names.count("1,1:HHQ2000") && names.count("2,1:HHQ2010") && names.count("1,2:HHQ2001") && names.count("2,2:HHQ2011"));
    CHECK(names.count("1,1:BDN0600"));
    CHECK(objects.size() == 4 + 1 + 4 + 1);

    CHECK(game::rampNumber(0, 0) == 16 && game::rampNumber(6, 1) == 21 && game::rampNumber(10, 3) == 31);
    CHECK(game::rampNumber(7, 0) == 32 && game::rampNumber(1, 2) == 32);

    // Within a tile the scene draws by layer: the unit after the building.
    const game::MapScene scene(std::move(terrain), std::move(objects));
    const game::MapSprite* building = nullptr;
    const game::MapSprite* unit = nullptr;
    for (const auto& o : scene.objects()) {
        if (o.x == 1 && o.y == 1 && o.layer == game::Layer::Building) building = &o;
        if (o.x == 1 && o.y == 1 && o.layer == game::Layer::Unit) unit = &o;
    }
    CHECK(building && unit && building < unit);
}

} // namespace

int main() {
    try {
        testGeometry();
        testLoadAndRender();
        testRoadVariants();
        testMapObjects();
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
