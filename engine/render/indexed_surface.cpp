#include "render/indexed_surface.hpp"

#include <algorithm>

namespace openday::render {

IndexedSurface::IndexedSurface(int width, int height, std::uint8_t fill) { resize(width, height, fill); }

void IndexedSurface::resize(int width, int height, std::uint8_t fill) {
    width_ = std::max(0, width);
    height_ = std::max(0, height);
    pixels_.assign(static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_), fill);
}

void IndexedSurface::fill(std::uint8_t index) { std::fill(pixels_.begin(), pixels_.end(), index); }

void IndexedSurface::blit(const formats::DbiSprite& sprite, int x, int y) {
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
