// 8-bit palettized software surface. The engine composes frames in palette
// indices (like the original 256-color display) and converts to RGBA only
// for presentation, so palette effects stay cheap.
#pragma once

#include <cstdint>
#include <vector>

#include "formats/dbi.hpp"
#include "formats/palette.hpp"

namespace openday::render {

class IndexedSurface {
public:
    IndexedSurface() = default;
    IndexedSurface(int width, int height, std::uint8_t fill = 0);

    int width() const { return width_; }
    int height() const { return height_; }
    std::uint8_t* row(int y) { return pixels_.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(width_); }
    const std::uint8_t* row(int y) const { return pixels_.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(width_); }
    std::uint8_t at(int x, int y) const { return row(y)[x]; }

    void resize(int width, int height, std::uint8_t fill = 0);
    void fill(std::uint8_t index);

    // Draws the opaque pixels of a sprite with its top-left corner at (x, y),
    // clipped to the surface.
    void blit(const formats::DbiSprite& sprite, int x, int y);

    // Writes width * height * 4 bytes of RGBA (alpha 255) into out.
    void toRgba(const formats::Palette& palette, std::uint8_t* out) const;
    std::vector<std::uint8_t> toRgba(const formats::Palette& palette) const;

private:
    int width_ = 0;
    int height_ = 0;
    std::vector<std::uint8_t> pixels_;
};

} // namespace openday::render
