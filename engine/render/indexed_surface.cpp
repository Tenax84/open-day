#include "render/indexed_surface.hpp"

#include <algorithm>

#include "render/image_layer.hpp"

namespace openday::render {

IndexedSurface::IndexedSurface(int width, int height, std::uint8_t fill) { resize(width, height, fill); }

void IndexedSurface::resize(int width, int height, std::uint8_t fill) {
    width_ = std::max(0, width);
    height_ = std::max(0, height);
    pixels_.assign(static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_), fill);
}

void IndexedSurface::fill(std::uint8_t index) { std::fill(pixels_.begin(), pixels_.end(), index); }

void IndexedSurface::blit(const formats::DbiSprite& sprite, int x, int y) {
    if (ImageLayer* layer = ImageLayer::active())
        layer->record(*this, sprite, {0, 0, sprite.width, sprite.height}, x, y, {0, 0, width_, height_});
    const int x0 = std::max(0, -x), x1 = std::min(sprite.width, width_ - x);
    const int y0 = std::max(0, -y), y1 = std::min(sprite.height, height_ - y);
    for (int sy = y0; sy < y1; ++sy) {
        const std::size_t srcRow = static_cast<std::size_t>(sy) * static_cast<std::size_t>(sprite.width);
        std::uint8_t* dst = row(y + sy) + x;
        for (int sx = x0; sx < x1; ++sx) {
            const std::size_t src = srcRow + static_cast<std::size_t>(sx);
            if (sprite.opaque[src]) dst[sx] = sprite.pixels[src];
        }
    }
}

void IndexedSurface::blit(const formats::DbiSprite& sprite, int x, int y, int mode, int cx, int cy, int cw, int ch) {
    mode &= 15;
    if (mode > 5) mode = 0; // unknown modes draw normally
    if (mode == 0 && cx <= 0 && cy <= 0 && cw >= width_ && ch >= height_) {
        blit(sprite, x, y);
        return;
    }
    if (mode == 0)
        if (ImageLayer* layer = ImageLayer::active())
            layer->record(*this, sprite, {0, 0, sprite.width, sprite.height}, x, y, {cx, cy, cw, ch});
    // The checkerboard color of the odd pixels; -1 = leave them out.
    int odd = -1;
    switch (mode) {
    case 1: odd = 0; break;
    case 3: odd = 249; break;
    case 4: odd = 250; break;
    case 5: odd = 79; break;
    default: break;
    }
    const int left = std::max(0, cx), top = std::max(0, cy);
    const int right = std::min(width_, cx + cw), bottom = std::min(height_, cy + ch);
    const int x0 = std::max(0, left - x), x1 = std::min(sprite.width, right - x);
    const int y0 = std::max(0, top - y), y1 = std::min(sprite.height, bottom - y);
    for (int sy = y0; sy < y1; ++sy) {
        const std::size_t srcRow = static_cast<std::size_t>(sy) * static_cast<std::size_t>(sprite.width);
        std::uint8_t* dst = row(y + sy) + x;
        for (int sx = x0; sx < x1; ++sx) {
            const std::size_t src = srcRow + static_cast<std::size_t>(sx);
            if (!sprite.opaque[src]) continue;
            if (mode == 0 || (sx + sy) % 2 == 0) dst[sx] = sprite.pixels[src];
            else if (odd >= 0) dst[sx] = static_cast<std::uint8_t>(odd);
        }
    }
}

void IndexedSurface::toRgba(const formats::Palette& palette, std::uint8_t* out) const {
    for (std::uint8_t index : pixels_) {
        const formats::Rgb& c = palette[index];
        *out++ = c.r;
        *out++ = c.g;
        *out++ = c.b;
        *out++ = 255;
    }
}

std::vector<std::uint8_t> IndexedSurface::toRgba(const formats::Palette& palette) const {
    std::vector<std::uint8_t> out(pixels_.size() * 4);
    toRgba(palette, out.data());
    return out;
}

} // namespace openday::render
