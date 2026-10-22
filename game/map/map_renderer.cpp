#include "map/map_renderer.hpp"

#include <algorithm>
#include <cctype>

namespace openday::game {

SpriteCache::SpriteCache(std::shared_ptr<const formats::DbiArchive> archive) : archive_(std::move(archive)) {
    for (std::size_t i = 0; i < archive_->count(); ++i) {
        std::string name = archive_->slotName(i);
        std::transform(name.begin(), name.end(), name.begin(),
                       [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        slots_.emplace(std::move(name), i);
    }
}

const formats::DbiSprite* SpriteCache::get(const std::string& upperName) {
    const auto slot = slots_.find(upperName);
    if (slot == slots_.end()) return nullptr;
    auto it = decoded_.find(slot->second);
    if (it == decoded_.end()) it = decoded_.emplace(slot->second, archive_->decode(slot->second)).first;
    return &it->second;
}

RenderStats drawTerrain(render::IndexedSurface& target, const Rect& view, const TerrainMap& map,
                        SpriteCache& terrain) {
    RenderStats stats;
    const int n = map.size;
    // Painter's order: rows of equal x + y lie at the same depth.
    for (int depth = 0; depth <= 2 * (n - 1); ++depth) {
        const int rowTop = depth * iso::kHalfStepY;
        if (rowTop > view.y + view.h) break;
        if (rowTop + iso::kSpriteHeight < view.y) continue;
        for (int x = std::max(0, depth - (n - 1)); x <= std::min(depth, n - 1); ++x) {
            const int y = depth - x;
            const Point o = iso::tileOrigin(x, y, n);
            if (o.x + iso::kSpriteWidth < view.x || o.x > view.x + view.w) continue;
            const formats::DbiSprite* sprite = terrain.get(map.at(x, y));
            if (!sprite) {
                ++stats.missingSprites;
                continue;
            }
            target.blit(*sprite, o.x - view.x, o.y - view.y);
            ++stats.tilesDrawn;
        }
    }
    return stats;
}

} // namespace openday::game
