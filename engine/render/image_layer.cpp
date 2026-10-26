#include "render/image_layer.hpp"

#include <algorithm>

namespace openday::render {

namespace {
thread_local ImageLayer* activeLayer = nullptr;
} // namespace

ImageLayer* ImageLayer::active() { return activeLayer; }
void ImageLayer::setActive(ImageLayer* layer) { activeLayer = layer; }

void ImageLayer::clear() {
    items_.clear();
    attached_.clear();
}

void ImageLayer::attach(const IndexedSurface& surface, int ox, int oy) {
    detach(surface);
    attached_.push_back({&surface, ox, oy});
}

void ImageLayer::detach(const IndexedSurface& surface) {
    std::erase_if(attached_, [&](const Attachment& a) { return a.surface == &surface; });
}

bool ImageLayer::attached(const IndexedSurface& surface) const {
    return std::any_of(attached_.begin(), attached_.end(), [&](const Attachment& a) { return a.surface == &surface; });
}

void ImageLayer::record(const IndexedSurface& surface, const formats::DbiSprite& sprite, Rect source, int x, int y, Rect clip) {
    if (!keyOf_) return;
    const auto a = std::find_if(attached_.begin(), attached_.end(), [&](const Attachment& at) { return at.surface == &surface; });
    if (a == attached_.end()) return;
    const int key = keyOf_(sprite);
    if (key < 0) return;
    // Clipped to the sprite, the surface and `clip`.
    const int left = std::max({0, clip.x, x, x - source.x}), top = std::max({0, clip.y, y, y - source.y});
    const int right = std::min({surface.width(), clip.x + clip.w, x + source.w, x + sprite.width - source.x});
    const int bottom = std::min({surface.height(), clip.y + clip.h, y + source.h, y + sprite.height - source.y});
    if (right <= left || bottom <= top) return;
    Item item;
    item.key = key;
    item.sprite = &sprite;
    item.source = {source.x + left - x, source.y + top - y, right - left, bottom - top};
    item.x = left + a->ox;
    item.y = top + a->oy;
    items_.push_back(std::move(item));
}

void ImageLayer::resolve(const IndexedSurface& frame) {
    std::vector<std::pair<int, int>> runs, last;
    for (Item& item : items_) {
        const formats::DbiSprite& sp = *item.sprite;
        item.visible.clear();
        item.allVisible = true;
        last.clear();
        // Runs of matching pixels along each row; a row with the same runs
        // as the one above lengthens its rectangles.
        std::size_t open = 0; // first rectangle of the row above
        for (int y = 0; y <= item.source.h; ++y) {
            runs.clear();
            const int fy = item.y + y;
            if (y < item.source.h && fy >= 0 && fy < frame.height()) {
                const std::size_t srcRow = static_cast<std::size_t>(item.source.y + y) * static_cast<std::size_t>(sp.width);
                int run = -1;
                for (int x = 0; x <= item.source.w; ++x) {
                    const int fx = item.x + x;
                    bool v = false;
                    if (x < item.source.w && fx >= 0 && fx < frame.width()) {
                        const std::size_t src = srcRow + static_cast<std::size_t>(item.source.x + x);
                        v = sp.opaque[src] && frame.at(fx, fy) == sp.pixels[src];
                    }
                    if (x < item.source.w && !v) item.allVisible = false;
                    if (v && run < 0) run = x;
                    if (!v && run >= 0) {
                        runs.emplace_back(run, x);
                        run = -1;
                    }
                }
            } else if (y < item.source.h) {
                item.allVisible = false;
            }
            if (!runs.empty() && runs == last) {
                for (std::size_t i = open; i < item.visible.size(); ++i) ++item.visible[i].h;
                continue;
            }
            open = item.visible.size();
            for (const auto& [x0, x1] : runs) item.visible.push_back({item.x + x0, item.y + y, x1 - x0, 1});
            last = runs;
        }
    }
}

} // namespace openday::render
