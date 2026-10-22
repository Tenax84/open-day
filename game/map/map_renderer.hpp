// Isometric terrain rendering. Geometry: docs/mechanics/isometric-map.md.
#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
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

// Draws terrain and objects together, tile by tile in depth order; objects
// on a tile follow its terrain in Layer order.
class MapScene {
public:
    MapScene(TerrainMap terrain, std::vector<MapSprite> objects);
    const TerrainMap& terrain() const { return terrain_; }
    const std::vector<MapSprite>& objects() const { return objects_; }
    RenderStats draw(render::IndexedSurface& target, const Rect& view, SpriteLibrary& sprites, bool showObjects = true) const;

private:
    TerrainMap terrain_;
    std::vector<MapSprite> objects_;      // sorted by tile, then layer
    std::vector<std::uint32_t> firstObject_; // per tile: index into objects_, size*size + 1 entries
};

} // namespace openday::game
