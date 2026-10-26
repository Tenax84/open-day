#include "render/text_layer.hpp"

#include <algorithm>
#include <cmath>
#include <span>
#include <stdexcept>

#include "stb_truetype.h"

namespace openday::embedded {
std::span<const std::uint8_t> fontRegular();
std::span<const std::uint8_t> fontBold();
} // namespace openday::embedded

namespace openday::render {

namespace {
thread_local TextLayer* activeLayer = nullptr;
// Room around the pixel text for the anti-aliased glyphs, which may reach a
// little further.
constexpr int kPad = 2;
} // namespace

TextLayer* TextLayer::active() { return activeLayer; }
void TextLayer::setActive(TextLayer* layer) { activeLayer = layer; }

void TextLayer::clear() {
    items_.clear();
    attached_.clear();
}

void TextLayer::attach(const IndexedSurface& surface, int ox, int oy) {
    detach(surface);
    attached_.push_back({&surface, ox, oy});
}

void TextLayer::detach(const IndexedSurface& surface) {
    std::erase_if(attached_, [&](const Attachment& a) { return a.surface == &surface; });
}

const TextLayer::Attachment* TextLayer::find(const IndexedSurface& surface) const {
    for (const Attachment& a : attached_)
        if (a.surface == &surface) return &a;
    return nullptr;
}

bool TextLayer::attached(const IndexedSurface& surface) const { return find(surface) != nullptr; }

void TextLayer::snapshot(const IndexedSurface& surface, const Attachment& a, Item& item, std::vector<std::uint8_t>& out) const {
    out.resize(static_cast<std::size_t>(item.boxW) * static_cast<std::size_t>(item.boxH));
    for (int y = 0; y < item.boxH; ++y) {
        const std::uint8_t* row = surface.row(item.boxY - a.oy + y) + (item.boxX - a.ox);
        std::copy_n(row, item.boxW, out.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(item.boxW));
    }
}

void TextLayer::beginItem(const IndexedSurface& surface, int x0, int y0, int x1, int y1) {
    pending_ = {};
    const Attachment* a = find(surface);
    if (!a) return;
    x0 = std::max(0, x0 - kPad);
    y0 = std::max(0, y0 - kPad);
    x1 = std::min(surface.width(), x1 + kPad);
    y1 = std::min(surface.height(), y1 + kPad);
    if (x1 <= x0 || y1 <= y0) return;
    pending_.boxX = x0 + a->ox;
    pending_.boxY = y0 + a->oy;
    pending_.boxW = x1 - x0;
    pending_.boxH = y1 - y0;
    snapshot(surface, *a, pending_, pending_.before);
}

void TextLayer::endItem(const IndexedSurface& surface, FontFace face, int cellHeight, int x, int y, int width,
                        std::u32string text, std::uint8_t color) {
    const Attachment* a = find(surface);
    if (!a || pending_.boxW <= 0 || pending_.boxH <= 0) return;
    Item item = std::move(pending_);
    pending_ = {};
    snapshot(surface, *a, item, item.after);
    if (item.after == item.before) return; // nothing of it was drawn
    item.face = face;
    item.cellHeight = cellHeight;
    item.x = x + a->ox;
    item.y = y + a->oy;
    item.width = width;
    item.text = std::move(text);
    item.color = color;
    items_.push_back(std::move(item));
}

void TextLayer::resolve(IndexedSurface& frame) {
    std::vector<std::uint8_t> match, hidden;
    // Latest first: a text drawn over an earlier one (a shadow) is taken out
    // before the earlier one is looked at.
    for (auto it = items_.rbegin(); it != items_.rend(); ++it) {
        Item& item = *it;
        const int w = item.boxW, h = item.boxH;
        auto at = [w](int x, int y) { return static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x); };
        // A pixel still as the text left it matches. Text pixels that do not
        // match are hidden by something drawn later, and so is everything
        // near them: what covers them may have the background's colour.
        match.assign(item.before.size(), 0);
        hidden.assign(item.before.size(), 0);
        for (int y = 0; y < h; ++y) {
            const int fy = item.boxY + y;
            for (int x = 0; x < w; ++x) {
                const int fx = item.boxX + x;
                match[at(x, y)] = fy < frame.height() && fx < frame.width() && frame.at(fx, fy) == item.after[at(x, y)];
                if (!match[at(x, y)] && item.after[at(x, y)] != item.before[at(x, y)])
                    for (int ny = std::max(0, y - kPad); ny <= std::min(h - 1, y + kPad); ++ny)
                        for (int nx = std::max(0, x - kPad); nx <= std::min(w - 1, x + kPad); ++nx) hidden[at(nx, ny)] = 1;
            }
        }
        // Where the text is visible the frame gets back what was under it.
        item.visible.assign(item.before.size(), 0);
        item.anyVisible = false;
        item.allVisible = true;
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                if (match[at(x, y)] && !hidden[at(x, y)]) {
                    item.visible[at(x, y)] = 1;
                    frame.row(item.boxY + y)[item.boxX + x] = item.before[at(x, y)];
                    item.anyVisible = item.anyVisible || item.after[at(x, y)] != item.before[at(x, y)];
                } else {
                    item.allVisible = false;
                }
            }
    }
}

struct SmoothFont::Impl {
    stbtt_fontinfo info{};
    float scale = 1;
};

SmoothFont::SmoothFont(FontFace face, float cellHeight) : impl_(std::make_unique<Impl>()) {
    const std::span<const std::uint8_t> data = face == FontFace::Bold ? embedded::fontBold() : embedded::fontRegular();
    if (!stbtt_InitFont(&impl_->info, data.data(), stbtt_GetFontOffsetForIndex(data.data(), 0)))
        throw std::runtime_error("cannot read the embedded font");
    // The same metrics as Font, at another size.
    impl_->scale = stbtt_ScaleForPixelHeight(&impl_->info, cellHeight);
    int ascent = 0, descent = 0, gap = 0;
    stbtt_GetFontVMetrics(&impl_->info, &ascent, &descent, &gap);
    baseline_ = static_cast<float>(ascent) * impl_->scale;
}

SmoothFont::~SmoothFont() = default;

const SmoothFont::Glyph& SmoothFont::glyph(char32_t c) const {
    const auto it = glyphs_.find(c);
    if (it != glyphs_.end()) return it->second;
    Glyph g;
    const int index = stbtt_FindGlyphIndex(&impl_->info, static_cast<int>(c));
    int advance = 0, bearing = 0;
    stbtt_GetGlyphHMetrics(&impl_->info, index, &advance, &bearing);
    g.advance = static_cast<float>(advance) * impl_->scale;
    int w = 0, h = 0, xoff = 0, yoff = 0;
    unsigned char* bitmap = stbtt_GetGlyphBitmap(&impl_->info, impl_->scale, impl_->scale, index, &w, &h, &xoff, &yoff);
    if (bitmap) {
        g.width = w;
        g.height = h;
        g.left = xoff;
        g.top = yoff;
        g.coverage.assign(bitmap, bitmap + static_cast<std::size_t>(w) * static_cast<std::size_t>(h));
        stbtt_FreeBitmap(bitmap, nullptr);
    }
    return glyphs_.emplace(c, std::move(g)).first->second;
}

} // namespace openday::render
