#include "map/map_renderer.hpp"

#include <algorithm>
#include <tuple>

#include "map/rules.hpp"

namespace openday::game {
namespace {

// Calls fn(x, y, origin) for every tile whose sprite cell intersects the
// view, back to front: rows of equal x + y lie at the same depth.
template <typename Fn>
void forVisibleTiles(int n, const Rect& view, Fn&& fn) {
    for (int depth = 0; depth <= 2 * (n - 1); ++depth) {
        const int rowTop = depth * iso::kHalfStepY;
        if (rowTop > view.y + view.h) break;
        if (rowTop + iso::kSpriteHeight < view.y) continue;
        for (int x = std::max(0, depth - (n - 1)); x <= std::min(depth, n - 1); ++x) {
            const int y = depth - x;
            const Point o = iso::tileOrigin(x, y, n);
            if (o.x + iso::kSpriteWidth < view.x || o.x > view.x + view.w) continue;
            fn(x, y, o);
        }
    }
}

} // namespace

SpriteCache::SpriteCache(std::shared_ptr<const formats::DbiArchive> archive) : archive_(std::move(archive)) {
    for (std::size_t i = 0; i < archive_->count(); ++i) slots_.emplace(toUpper(archive_->slotName(i)), i);
}

const formats::DbiSprite* SpriteCache::get(const std::string& upperName) {
    const auto slot = slots_.find(upperName);
    if (slot == slots_.end()) return nullptr;
    auto it = decoded_.find(slot->second);
    if (it == decoded_.end()) it = decoded_.emplace(slot->second, archive_->decode(slot->second)).first;
    return &it->second;
}

void SpriteLibrary::set(SpriteSet which, std::shared_ptr<const formats::DbiArchive> archive) {
    caches_[static_cast<std::size_t>(which)] = archive ? std::make_unique<SpriteCache>(std::move(archive)) : nullptr;
}

SpriteLibrary SpriteLibrary::fromGameDir(const std::filesystem::path& gameDir) {
    static constexpr std::pair<SpriteSet, const char*> kFiles[] = {
        {SpriteSet::Terrain, "TERRAINS.DBI"}, {SpriteSet::Roads, "ROADS.DBI"}, {SpriteSet::Walls, "WALLS.DBI"},
        {SpriteSet::Mines, "MINES.DBI"},      {SpriteSet::Ruins, "RUINS.DBI"}, {SpriteSet::UnitsBuildings, "UB.DBI"},
        {SpriteSet::Symbols, "SYMBOLS.DBI"}, {SpriteSet::ListBox, "LISTBOX.DBI"},
    };
    SpriteLibrary lib;
    for (const auto& [set, file] : kFiles)
        lib.set(set, std::make_shared<const formats::DbiArchive>(formats::DbiArchive::fromFile(gameDir / "IMGS" / file)));
    return lib;
}

const formats::DbiSprite* SpriteLibrary::get(SpriteSet which, const std::string& upperName) {
    SpriteCache* c = cache(which);
    return c ? c->get(upperName) : nullptr;
}

RenderStats drawTerrain(render::IndexedSurface& target, const Rect& view, const TerrainMap& map,
                        SpriteCache& terrain) {
    RenderStats stats;
    forVisibleTiles(map.size, view, [&](int x, int y, Point o) {
        const formats::DbiSprite* sprite = terrain.get(map.at(x, y));
        if (!sprite) {
            ++stats.missingSprites;
            return;
        }
        target.blit(*sprite, o.x - view.x, o.y - view.y);
        ++stats.tilesDrawn;
    });
    return stats;
}

MapScene::MapScene(TerrainMap terrain, std::vector<MapSprite> objects)
    : terrain_(std::move(terrain)), objects_(std::move(objects)) {
    reindex();
}

void MapScene::reindex() {
    const int n = terrain_.size;
    std::stable_sort(objects_.begin(), objects_.end(), [](const MapSprite& a, const MapSprite& b) {
        return std::tie(a.y, a.x, a.layer) < std::tie(b.y, b.x, b.layer);
    });
    firstObject_.assign(static_cast<std::size_t>(n) * static_cast<std::size_t>(n) + 1, 0);
    std::size_t i = 0;
    for (std::size_t tile = 0; tile < firstObject_.size() - 1; ++tile) {
        firstObject_[tile] = static_cast<std::uint32_t>(i);
        while (i < objects_.size() &&
               static_cast<std::size_t>(objects_[i].y) * static_cast<std::size_t>(n) + static_cast<std::size_t>(objects_[i].x) == tile)
            ++i;
    }
    firstObject_.back() = static_cast<std::uint32_t>(objects_.size());
}

bool MapScene::hasObject(int x, int y, Layer layer) const {
    if (!terrain_.inside(x, y)) return false;
    const std::size_t tile = static_cast<std::size_t>(y) * static_cast<std::size_t>(terrain_.size) + static_cast<std::size_t>(x);
    for (std::uint32_t i = firstObject_[tile]; i < firstObject_[tile + 1]; ++i)
        if (objects_[i].layer == layer) return true;
    return false;
}

void MapScene::replaceEntity(Layer layer, int entity, std::vector<MapSprite> sprites) {
    std::erase_if(objects_, [&](const MapSprite& o) { return o.layer == layer && o.entity == entity; });
    for (MapSprite& s : sprites)
        if (terrain_.inside(s.x, s.y)) objects_.push_back(std::move(s));
    reindex();
}

void MapScene::replaceLayer(Layer layer, std::vector<MapSprite> sprites) {
    std::erase_if(objects_, [&](const MapSprite& o) { return o.layer == layer; });
    for (MapSprite& s : sprites)
        if (terrain_.inside(s.x, s.y)) objects_.push_back(std::move(s));
    reindex();
}

void MapScene::setTerrain(int x, int y, const std::string& name) {
    if (terrain_.inside(x, y)) terrain_.tiles[static_cast<std::size_t>(y * terrain_.size + x)] = name;
}

void MapScene::addObject(MapSprite sprite) {
    if (!terrain_.inside(sprite.x, sprite.y)) return;
    objects_.push_back(std::move(sprite));
    reindex();
}

RenderStats MapScene::draw(render::IndexedSurface& target, const Rect& view, SpriteLibrary& sprites, bool showObjects,
                           const SceneExtras* extras) const {
    RenderStats stats;
    const int n = terrain_.size;
    // Overlays by tile; the ones whose tile is not drawn go last.
    std::unordered_multimap<std::size_t, const Overlay*> overlayAt;
    std::vector<bool> overlayDone;
    if (extras) {
        overlayDone.assign(extras->overlays.size(), false);
        for (const Overlay& ov : extras->overlays)
            if (terrain_.inside(ov.tileX, ov.tileY))
                overlayAt.emplace(static_cast<std::size_t>(ov.tileY) * static_cast<std::size_t>(n) + static_cast<std::size_t>(ov.tileX), &ov);
    }
    auto drawOverlay = [&](const Overlay& ov) {
        if (ov.sprite) target.blit(*ov.sprite, ov.x - view.x, ov.y - view.y);
        overlayDone[static_cast<std::size_t>(&ov - extras->overlays.data())] = true;
    };
    forVisibleTiles(n, view, [&](int x, int y, Point o) {
        auto blit = [&](SpriteSet set, const std::string& name, int dy) {
            const formats::DbiSprite* sprite = sprites.get(set, name);
            if (!sprite) {
                ++stats.missingSprites;
                return;
            }
            target.blit(*sprite, o.x - view.x, o.y - view.y + dy);
        };
        blit(SpriteSet::Terrain, terrain_.at(x, y), 0);
        ++stats.tilesDrawn;
        const std::size_t tile = static_cast<std::size_t>(y) * static_cast<std::size_t>(n) + static_cast<std::size_t>(x);
        if (showObjects)
            for (std::uint32_t i = firstObject_[tile]; i < firstObject_[tile + 1]; ++i) {
                const MapSprite& obj = objects_[i];
                if (extras && extras->hidden && extras->hidden(obj)) continue;
                blit(obj.set, obj.name, obj.dy);
            }
        const auto [first, last] = overlayAt.equal_range(tile);
        for (auto it = first; it != last; ++it) drawOverlay(*it->second);
    });
    if (extras)
        for (std::size_t i = 0; i < extras->overlays.size(); ++i)
            if (!overlayDone[i]) drawOverlay(extras->overlays[i]);
    return stats;
}

} // namespace openday::game
