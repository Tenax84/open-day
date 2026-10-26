#include "map/map_renderer.hpp"

#include <algorithm>
#include <limits>
#include <tuple>

#include "formats/game_path.hpp"
#include "map/rules.hpp"

namespace openday::game {
namespace {

// Calls fn(x, y, origin) for every tile whose sprite cell intersects the
// view, back to front: rows of equal x + y lie at the same depth. With a
// border, the cells up to `border` tiles outside the map are included.
template <typename Fn>
void forVisibleTiles(int n, const Rect& view, Fn&& fn, int border = 0) {
    const int lo = -border, hi = n - 1 + border;
    for (int depth = 2 * lo; depth <= 2 * hi; ++depth) {
        const int rowTop = depth * iso::kHalfStepY;
        if (rowTop > view.y + view.h) break;
        if (rowTop + iso::kSpriteHeight < view.y) continue;
        for (int x = std::max(lo, depth - hi); x <= std::min(depth - lo, hi); ++x) {
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
        lib.set(set, std::make_shared<const formats::DbiArchive>(formats::DbiArchive::fromFile(formats::findGameFile(gameDir, std::string("IMGS/") + file))));
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

void MapScene::removeObjects(int x, int y, Layer layer) {
    std::erase_if(objects_, [&](const MapSprite& o) { return o.x == x && o.y == y && o.layer == layer; });
    reindex();
}

void MapScene::setTerrain(int x, int y, const std::string& name) {
    if (!terrain_.inside(x, y)) return;
    const auto i = static_cast<std::size_t>(y * terrain_.size + x);
    terrain_.tiles[i] = name;
    // The cell keeps the Overlapping flag of its new terrain (isometric-map.md).
    if (rules_ && i < overlapping_.size()) {
        const TerrainType* t = rules_->terrainType(name);
        overlapping_[i] = t && t->overlapping ? 1 : 0;
    }
}

void MapScene::useRules(const GlobalRules& rules) {
    rules_ = &rules;
    overlapping_.assign(terrain_.tiles.size(), 0);
    for (std::size_t i = 0; i < terrain_.tiles.size(); ++i) {
        const TerrainType* t = rules.terrainType(terrain_.tiles[i]);
        overlapping_[i] = t && t->overlapping ? 1 : 0;
    }
}

bool MapScene::overlapping(int x, int y) const {
    if (!terrain_.inside(x, y)) return false;
    const auto i = static_cast<std::size_t>(y * terrain_.size + x);
    return i < overlapping_.size() && overlapping_[i] != 0;
}

void MapScene::addObject(MapSprite sprite) {
    if (!terrain_.inside(sprite.x, sprite.y)) return;
    objects_.push_back(std::move(sprite));
    reindex();
}

void MapScene::drawCell(render::IndexedSurface& target, const Rect& view, SpriteLibrary& sprites, bool showObjects,
                        const SceneExtras* extras, int x, int y, const Rect& clip, bool terrain,
                        const std::vector<const Overlay*>* overlays, std::vector<Deferred>& deferred, RenderStats& stats) const {
    const int n = terrain_.size;
    auto put = [&](const formats::DbiSprite& sprite, int tx, int ty, int mode) {
        if (mode & kDrawOnTop) deferred.push_back({&sprite, tx, ty, mode & 15, clip});
        else target.blit(sprite, tx, ty, mode, clip.x, clip.y, clip.w, clip.h);
    };
    const Point o = iso::tileOrigin(x, y, n);
    const int sx = o.x - view.x, sy = o.y - view.y;
    if (!terrain_.inside(x, y)) {
        // The frame of black diamonds around the map.
        if (terrain)
            if (const formats::DbiSprite* black = sprites.get(SpriteSet::Terrain, "BLACK")) put(*black, sx, sy, 0);
        return;
    }
    if (terrain) {
        const std::string& name = terrain_.at(x, y);
        if (const formats::DbiSprite* sprite = sprites.get(SpriteSet::Terrain, name.empty() ? "BORE000" : name))
            put(*sprite, sx, sy, 0);
        else
            ++stats.missingSprites;
    }
    // The features of the cell in layer order; markers follow the objects
    // of their layer.
    std::size_t next = 0;
    auto overlaysUpTo = [&](int layer) {
        if (!overlays) return;
        for (; next < overlays->size() && (*overlays)[next]->layer <= layer; ++next) {
            const Overlay& ov = *(*overlays)[next];
            if (ov.sprite) put(*ov.sprite, ov.x - view.x, ov.y - view.y, ov.mode);
        }
    };
    const std::size_t tile = static_cast<std::size_t>(y) * static_cast<std::size_t>(n) + static_cast<std::size_t>(x);
    if (showObjects)
        for (std::uint32_t i = firstObject_[tile]; i < firstObject_[tile + 1]; ++i) {
            const MapSprite& obj = objects_[i];
            overlaysUpTo(static_cast<int>(obj.layer) - 1);
            if (extras && extras->hidden && extras->hidden(obj)) continue;
            const int mode = extras && extras->onTop && extras->onTop(obj) ? obj.mode | kDrawOnTop : obj.mode;
            if (const formats::DbiSprite* sprite = sprites.get(obj.set, obj.name)) put(*sprite, sx, sy + obj.dy, mode);
            else ++stats.missingSprites;
        }
    overlaysUpTo(std::numeric_limits<int>::max());
}

RenderStats MapScene::draw(render::IndexedSurface& target, const Rect& view, SpriteLibrary& sprites, bool showObjects,
                           const SceneExtras* extras) const {
    RenderStats stats;
    const int n = terrain_.size;
    // Markers by tile, in layer order; those outside the map go last.
    std::unordered_map<std::size_t, std::vector<const Overlay*>> overlayAt;
    std::vector<const Overlay*> late;
    if (extras)
        for (const Overlay& ov : extras->overlays) {
            if (terrain_.inside(ov.tileX, ov.tileY))
                overlayAt[static_cast<std::size_t>(ov.tileY) * static_cast<std::size_t>(n) + static_cast<std::size_t>(ov.tileX)]
                    .push_back(&ov);
            else
                late.push_back(&ov);
        }
    for (auto& entry : overlayAt)
        std::stable_sort(entry.second.begin(), entry.second.end(),
                         [](const Overlay* a, const Overlay* b) { return a->layer < b->layer; });
    auto overlaysOf = [&](int x, int y) -> const std::vector<const Overlay*>* {
        if (!terrain_.inside(x, y)) return nullptr;
        const auto it = overlayAt.find(static_cast<std::size_t>(y) * static_cast<std::size_t>(n) + static_cast<std::size_t>(x));
        return it == overlayAt.end() ? nullptr : &it->second;
    };
    auto flush = [&](std::vector<Deferred>& deferred) {
        for (const Deferred& d : deferred) target.blit(*d.sprite, d.x, d.y, d.mode, d.clip.x, d.clip.y, d.clip.w, d.clip.h);
        deferred.clear();
    };

    // 1. The cells back to front, with a border of black cells two tiles
    //    wide; features drawn on top wait until all cells are drawn.
    const Rect whole{0, 0, target.width(), target.height()};
    std::vector<Deferred> deferred;
    forVisibleTiles(
        n, view,
        [&](int x, int y, Point) {
            drawCell(target, view, sprites, showObjects, extras, x, y, whole, true, overlaysOf(x, y), deferred, stats);
            if (terrain_.inside(x, y)) ++stats.tilesDrawn;
        },
        2);
    for (const Overlay* ov : late)
        if (ov->sprite) {
            if (ov->mode & kDrawOnTop) deferred.push_back({ov->sprite, ov->x - view.x, ov->y - view.y, ov->mode & 15, whole});
            else target.blit(*ov->sprite, ov->x - view.x, ov->y - view.y, ov->mode);
        }
    flush(deferred);

    // 2. The animation frames on top, each followed by the repair of the
    //    cells in front of it (unit-animation.md 8).
    if (extras)
        for (const AnimDraw& f : extras->frames) {
            if (f.sprite) target.blit(*f.sprite, f.x - view.x, f.y - view.y);
            RenderStats ignored;
            for (const RefreshCell& r : f.refresh) {
                if (!terrain_.inside(r.x, r.y)) continue;
                const Point o = iso::tileOrigin(r.x, r.y, n);
                const Rect clip{o.x - view.x + r.rx, o.y - view.y + r.ry, r.w, r.h};
                drawCell(target, view, sprites, showObjects, extras, r.x, r.y, clip, overlapping(r.x, r.y), overlaysOf(r.x, r.y),
                         deferred, ignored);
            }
            flush(deferred);
        }
    return stats;
}

} // namespace openday::game
