// Text for the user interface. The original draws Arial with Windows GDI,
// without anti-aliasing (docs/mechanics/interface.md, "Text rendering");
// OpenDay rasterizes an embedded metric-compatible font the same way: every
// glyph pixel is either fully set or not.
#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "render/indexed_surface.hpp"

namespace openday::render {

enum class FontFace { Regular, Bold };

enum class Align { Left, Center, Right };

class Font {
public:
    // `cellHeight` is the line height in pixels (the GDI "logical height").
    Font(FontFace face, int cellHeight);
    ~Font();
    Font(const Font&) = delete;
    Font& operator=(const Font&) = delete;

    int lineHeight() const { return cellHeight_; }
    int width(std::string_view utf8) const;

    // Draws one line with its cell's top-left at (x, y).
    void draw(IndexedSurface& target, int x, int y, std::string_view utf8, std::uint8_t color) const;
    // Draws one line inside a box, horizontally aligned and vertically centred.
    void drawIn(IndexedSurface& target, int x, int y, int w, int h, std::string_view utf8, std::uint8_t color,
                Align align = Align::Center) const;
    // Breaks text into lines no wider than `maxWidth` at spaces; "\n" and
    // "##" (the scenario texts' line break) start a new line.
    std::vector<std::string> wrap(std::string_view utf8, int maxWidth) const;
    // Draws wrapped text from the top of the box; returns the lines drawn.
    int drawWrapped(IndexedSurface& target, int x, int y, int w, int h, std::string_view utf8, std::uint8_t color) const;

private:
    struct Glyph {
        int advance = 0;
        int left = 0, top = 0; // offset of the bitmap from the pen position on the baseline
        int width = 0, height = 0;
        std::vector<std::uint8_t> mask; // 0/1
    };
    const Glyph& glyph(char32_t c) const;

    struct Impl;
    std::unique_ptr<Impl> impl_;
    int cellHeight_;
    int baseline_ = 0; // from the top of the cell
    mutable std::unordered_map<char32_t, Glyph> glyphs_;
};

// Decodes UTF-8 (invalid bytes count as Latin-1 characters).
std::u32string decodeUtf8(std::string_view s);
std::string encodeUtf8(char32_t c);

} // namespace openday::render
