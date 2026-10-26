// Isometric terrain rendering. Geometry: docs/mechanics/isometric-map.md.
#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "formats/dbi.hpp"
#include "map/map_objects.hpp"
#include "map/terrain_map.hpp"
#include "render/indexed_surface.hpp"

namespace openday::game {

struct Point {
    int x = 0, y = 0;
};

struct Rect {
    int x = 0, y = 0, w = 0, h = 0;
};

namespace iso {
// Terrain sprites are 54 x 56; the ground diamond occupies rows 28-55 and
// tiles the plane with a step of 28 px horizontally and 14 px vertically.
inline constexpr int kSpriteWidth = 54;
inline constexpr int kSpriteHeight = 56;
inline constexpr int kDiamondTop = 28;
inline constexpr int kHalfStepX = 28;
inline constexpr int kHalfStepY = 14;

// Top-left of tile (x, y)'s sprite in map pixel space, where tile (0, 0)'s
// diamond starts at the top corner of the map.
inline Point tileOrigin(int x, int y, int mapSize) {
    return {(x - y + mapSize - 1) * kHalfStepX, (x + y) * kHalfStepY};
}

// Tile whose ground diamond contains the map pixel (px, py); the result may
// lie outside the map. Inverse of tileOrigin, with the diamond centered at
// (27, 42) in the sprite.
inline Point tileAt(int px, int py, int mapSize) {
    const double a = (px - (mapSize - 1) * kHalfStepX - 27 + 0.5) / kHalfStepX; // x - y
    const double b = (py - 42 + 0.5) / kHalfStepY;                              // x + y
    return {static_cast<int>(std::floor((a + b) / 2 + 0.5)), static_cast<int>(std::floor((b - a) / 2 + 0.5))};
}

// Pixel extent of a whole map, including the space above the top row that
// tall sprites may reach into.
inline Rect mapBounds(int mapSize) {
    return {0, 0, (2 * mapSize - 2) * kHalfStepX + kSpriteWidth, (2 * mapSize - 2) * kHalfStepY + kSpriteHeight};
}
} // namespace iso

// Decodes sprites on first use and keeps them; lookups are by upper-case name.
class SpriteCache {
public:
    explicit SpriteCache(std::shared_ptr<const formats::DbiArchive> archive);
    // Returns nullptr when the archive has no sprite of that name.
    const formats::DbiSprite* get(const std::string& upperName);
    const formats::DbiArchive& archive() const { return *archive_; }

private:
    std::shared_ptr<const formats::DbiArchive> archive_;
    std::unordered_map<std::string, std::size_t> slots_;
    std::unordered_map<std::size_t, formats::DbiSprite> decoded_;
};

// One SpriteCache per DBI archive; archives that are not loaded draw nothing.
class SpriteLibrary {
public:
    void set(SpriteSet which, std::shared_ptr<const formats::DbiArchive> archive);
    // Loads the archives of a game installation (IMGS folder).
    static SpriteLibrary fromGameDir(const std::filesystem::path& gameDir);
    const formats::DbiSprite* get(SpriteSet which, const std::string& upperName);
    SpriteCache* cache(SpriteSet which) { return caches_[static_cast<std::size_t>(which)].get(); }

private:
    std::array<std::unique_ptr<SpriteCache>, kSpriteSetCount> caches_;
};

struct RenderStats {
    int tilesDrawn = 0;
    int missingSprites = 0;
};

// Draws the terrain visible in `view` (a rectangle in map pixel space) into
// `target`, back to front so tall tiles overlap the ones behind them.
RenderStats drawTerrain(render::IndexedSurface& target, const Rect& view, const TerrainMap& map,
                        SpriteCache& terrain);

// A sprite drawn in addition to the scene for one frame (interface markers:
// paths, cursors, zones). It is drawn in the cell pass of its tile like an
// object of `layer` (map-objects.md, "Per-cell feature lists").
struct Overlay {
    int tileX = -1, tileY = -1; // a tile outside the map = drawn after everything
    int x = 0, y = 0;           // map pixel position of the sprite's top-left corner
    const formats::DbiSprite* sprite = nullptr;
    int layer = 255; // the default: after every object of the tile
    int mode = 0;    // draw mode; kDrawOnTop = after all cells
};

// Part of a map cell that is drawn again after an animation frame, so that
// what stands in front of the animation covers it (unit-animation.md 8).
struct RefreshCell {
    int x = 0, y = 0;               // the cell
    int rx = 0, ry = 0, w = 0, h = 0; // rectangle inside the cell's 54 x 56 sprite
};

// An animation frame of the current tick. Frames are drawn after the map,
// in the given order, each followed by its refresh list.
struct AnimDraw {
    int x = 0, y = 0; // map pixel position of the sprite's top-left corner
    const formats::DbiSprite* sprite = nullptr;
    std::vector<RefreshCell> refresh{};
};

struct SceneExtras {
    std::function<bool(const MapSprite&)> hidden; // objects not to draw (optional)
    // Objects drawn as if their mode had kDrawOnTop (optional): they stay
    // above markers that are drawn on top.
    std::function<bool(const MapSprite&)> onTop;
    std::vector<Overlay> overlays;
    std::vector<AnimDraw> frames{};
};

// Draws terrain and objects together, tile by tile in depth order; objects
// on a tile follow its terrain in Layer order.
class MapScene {
public:
    MapScene(TerrainMap terrain, std::vector<MapSprite> objects);
    const TerrainMap& terrain() const { return terrain_; }
    const std::vector<MapSprite>& objects() const { return objects_; }
    bool hasObject(int x, int y, Layer layer) const;

    // Replaces the sprites of one unit or building (Layer + MapSprite::entity)
    // with `sprites`, which may be empty to remove it.
    void replaceEntity(Layer layer, int entity, std::vector<MapSprite> sprites);
    void addObject(MapSprite sprite);
    // Replaces every sprite of a layer (roads, walls and mines are redrawn
    // as a whole because neighbors change their shape).
    void replaceLayer(Layer layer, std::vector<MapSprite> sprites);
    // Removes the sprites of a layer on one tile.
    void removeObjects(int x, int y, Layer layer);
    void setTerrain(int x, int y, const std::string& name);
    // Takes the Overlapping flags of the terrain from the rules (and again
    // in setTerrain). Without rules no terrain counts as overlapping.
    void useRules(const GlobalRules& rules);
    bool overlapping(int x, int y) const;

    RenderStats draw(render::IndexedSurface& target, const Rect& view, SpriteLibrary& sprites, bool showObjects = true,
                     const SceneExtras* extras = nullptr) const;

private:
    void reindex();
    // Draws the terrain and the features of one cell (clipped to `clip` in
    // target coordinates); features drawn on top are added to `deferred`.
    struct Deferred {
        const formats::DbiSprite* sprite;
        int x, y, mode;
        Rect clip;
    };
    void drawCell(render::IndexedSurface& target, const Rect& view, SpriteLibrary& sprites, bool showObjects,
                  const SceneExtras* extras, int x, int y, const Rect& clip, bool terrain,
                  const std::vector<const Overlay*>* overlays, std::vector<Deferred>& deferred, RenderStats& stats) const;

    TerrainMap terrain_;
    const GlobalRules* rules_ = nullptr;
    std::vector<std::uint8_t> overlapping_; // per tile
    std::vector<MapSprite> objects_;      // sorted by tile, then layer
    std::vector<std::uint32_t> firstObject_; // per tile: index into objects_, size*size + 1 entries
};

} // namespace openday::game
