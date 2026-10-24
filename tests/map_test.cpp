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
#include <utility>
#include <vector>

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
        rules.units[{5, 1}].image = "BDN";
        rules.terrain["GRASS0"].cls = game::TerrainClass::Plain;
        rules.terrain["EAU000"].cls = game::TerrainClass::DeepWater;
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

// Color table, mine suffix, damaged threshold, tower names, draw modes and
// the deferred / border drawing of the scene.
void testDrawRules() {
    // Identity, then the local slot and its chosen color swapped.
    const game::ColorTable identity;
    CHECK(identity(0) == 0 && identity(1) == 1 && identity(4) == 4 && identity(7) == 0);
    const game::ColorTable t = game::makeColorTable(2, 4);
    CHECK(t(0) == 0 && t(1) == 1 && t(2) == 4 && t(3) == 3 && t(4) == 2);
    const game::ColorTable single = game::makeColorTable(1, 1);
    CHECK(single.color == identity.color);

    game::MineType mine;
    mine.image = "GMINE";
    CHECK(game::mineSpriteName(mine, true) == "GMINE001" && game::mineSpriteName(mine, false) == "GMINE000");

    // currentAR <= 0.33 x Armor.
    CHECK(game::isDamaged(33, 100) && !game::isDamaged(34, 100));
    CHECK(game::isDamaged(19, 60) && !game::isDamaged(20, 60) && game::isDamaged(0, 60));

    game::GlobalRules rules;
    game::UnitType tower;
    tower.image = "HLAS";
    tower.typeId = 39;
    rules.units[{39, 0}] = tower;
    game::TerrainMap terrain;
    terrain.size = 8;
    terrain.tiles.assign(64, "GRASS");
    game::BuildingType barracks{"HBAR", 2, 1, 90, true, true};
    game::MapBuilding b{&barracks, 2, 3, true, 3, 29}; // 29 <= 0.33 x 90
    std::vector<game::MapSprite> s = game::buildingSprites(b, 5, rules, terrain);
    CHECK(s.size() == 2 && s[0].name == "HBAR3100" && s[1].name == "HBAR3110" && s[1].x == 3 && s[0].entity == 5);
    b.armor = 30; // above 29.7: the intact sprite
    CHECK(game::buildingSprites(b, 5, rules, terrain)[0].name == "HBAR3000");
    barracks.damagedImages = false;
    b.armor = 1;
    CHECK(game::buildingSprites(b, 5, rules, terrain)[0].name == "HBAR3000");
    // A tower: the unit type's code, facing 0, never damaged.
    game::BuildingType towerBuilding{"HLAS", 1, 1, 100, true, false};
    towerBuilding.transformIntoUnit = 39;
    const game::MapBuilding tb{&towerBuilding, 0, 0, true, 1, 1};
    CHECK(game::buildingSprites(tb, 0, rules, terrain)[0].name == "HLAS1000");

    // Units without enough AP to fire are darkened.
    game::UnitType tank;
    tank.image = "HTNK";
    tank.shotAp = 6;
    game::MapUnit u{&tank, 1, 0, 1, 1};
    CHECK(game::unitDrawMode(u) == 0); // AP unknown
    u.ap = 5;
    CHECK(game::unitDrawMode(u) == 0); // 5 >= 4
    u.ap = 3;
    CHECK(game::unitDrawMode(u) == 1);
    tank.shotAp = 2;
    CHECK(game::unitDrawMode(u) == 0); // can still fire
    u.mode = 1;
    CHECK(game::unitDrawMode(u) == 1); // explicit (deployment)
    CHECK(game::unitSprite(u, 0, terrain, rules, false).mode == 1);

    // Checkerboard blits: odd (column + row) pixels become 0 / 249 / 250 /
    // 79 or stay untouched (mode 2).
    formats::DbiSprite sq{"SQ", 2, 2, {7, 7, 7, 7}, {1, 1, 1, 1}};
    for (const auto& [mode, odd] : {std::pair{1, 0}, std::pair{2, 9}, std::pair{3, 249}, std::pair{4, 250}, std::pair{5, 79}}) {
        render::IndexedSurface surface(2, 2, 9);
        surface.blit(sq, 0, 0, mode);
        CHECK(surface.at(0, 0) == 7 && surface.at(1, 1) == 7 && surface.at(1, 0) == odd && surface.at(0, 1) == odd);
    }
    render::IndexedSurface clipped(4, 4, 9);
    clipped.blit(sq, 1, 1, 0, 2, 2, 1, 1); // only the pixel (2, 2)
    CHECK(clipped.at(2, 2) == 7 && clipped.at(1, 1) == 9 && clipped.at(2, 1) == 9);

    // Scene: a marker drawn on top stays visible over the unit of the cell
    // in front; an ordinary one of a lower layer is covered by it. The map
    // is framed by BLACK cells.
    formats::DbiSprite block{"BLOCK", game::iso::kSpriteWidth, game::iso::kSpriteHeight, {}, {}};
    block.pixels.assign(static_cast<std::size_t>(block.width * block.height), 3);
    block.opaque.assign(block.pixels.size(), 1);
    const formats::Palette pal{};
    auto archive = std::make_shared<const formats::DbiArchive>(
        formats::DbiArchive::fromBytes(formats::writeDbi({flatTile("GRASS", 1), flatTile("BLACK", 2), block}, pal, true)));
    game::SpriteLibrary lib;
    lib.set(game::SpriteSet::Terrain, archive);
    lib.set(game::SpriteSet::UnitsBuildings, archive);
    lib.set(game::SpriteSet::Symbols, archive);
    game::TerrainMap small;
    small.size = 3;
    small.tiles.assign(9, "GRASS");
    // A full block on (1, 1), in front of (0, 0).
    const game::MapScene scene(small, {{1, 1, game::Layer::Unit, game::SpriteSet::UnitsBuildings, "BLOCK"}});
    const formats::DbiSprite* marker = lib.get(game::SpriteSet::Symbols, "GRASS");
    const game::Point o = game::iso::tileOrigin(0, 0, 3);
    game::SceneExtras extras;
    extras.overlays.push_back({0, 0, o.x, o.y, marker, 7, 0});
    const game::Rect bounds = game::iso::mapBounds(3);
    // A pixel of tile (0, 0)'s diamond that the block on (1, 1) covers.
    const game::Point p{o.x + 27, o.y + 50};
    render::IndexedSurface surface(bounds.w, bounds.h, 0);
    scene.draw(surface, bounds, lib, true, &extras);
    CHECK(surface.at(p.x, p.y) == 3);
    extras.overlays[0].mode = game::kDrawOnTop;
    scene.draw(surface, bounds, lib, true, &extras);
    CHECK(surface.at(p.x, p.y) == 1);
    // Outside the map: the BLACK border (tile (-1, 0) left of (0, 0)).
    const game::Point left = game::iso::tileOrigin(-1, 0, 3);
    CHECK(surface.at(left.x + 27, left.y + 42) == 2);

    // An animation frame is repaired by the cells in front of it: the block
    // on (1, 1) covers the part of the frame inside its refresh rectangle.
    render::IndexedSurface anim(bounds.w, bounds.h, 0);
    const formats::DbiSprite* frame = lib.get(game::SpriteSet::Symbols, "BLACK");
    extras.overlays.clear();
    extras.frames.push_back({o.x, o.y + 14, frame, {{1, 1, 0, 0, 54, 28}}});
    scene.draw(anim, bounds, lib, true, &extras);
    const game::Point o11 = game::iso::tileOrigin(1, 1, 3);
    CHECK(anim.at(o11.x + 27, o11.y + 20) == 3); // inside the refresh rectangle: the block again
    extras.frames[0].refresh.clear();
    scene.draw(anim, bounds, lib, true, &extras);
    CHECK(anim.at(o11.x + 27, o11.y + 20) == 2); // without the repair the frame covers it
}

} // namespace

int main() {
    try {
        testGeometry();
        testLoadAndRender();
        testRoadVariants();
        testMapObjects();
        testDrawRules();
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
