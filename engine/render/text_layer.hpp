// Sharp text (docs/mechanics/interface.md, "Sharp text"): the text drawn into
// a frame is recorded, so that it can be drawn again, anti-aliased, at the
// resolution the frame is shown at. The frame itself keeps the pixel text
// (screenshots and the debug server see it unchanged); `resolve` finds where
// each text is still visible and takes its pixels out of a copy for display.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "render/font.hpp"
#include "render/indexed_surface.hpp"

namespace openday::render {

class TextLayer {
public:
    struct Item {
        FontFace face = FontFace::Regular;
        int cellHeight = 0;
        int x = 0, y = 0; // the cell's top-left in frame pixels
        int width = 0;    // of the pixel text
        std::u32string text;
        std::uint8_t color = 0;
        // The frame rectangle the pixel text may touch (padded) and, over
        // it, the pixels before and after it was drawn.
        int boxX = 0, boxY = 0, boxW = 0, boxH = 0;
        std::vector<std::uint8_t> before, after;
        // After resolve: 1 where the frame pixel is still the text's;
        // whether all of the box is, and whether any pixel of the letters.
        std::vector<std::uint8_t> visible;
        bool allVisible = false, anyVisible = false;
    };

    // While a layer is active, text drawn into its attached surfaces is
    // recorded (Font::draw asks).
    static TextLayer* active();
    static void setActive(TextLayer* layer);

    void clear();
    // Texts drawn into `surface` are recorded; its (0, 0) is at (ox, oy) of
    // the frame.
    void attach(const IndexedSurface& surface, int ox, int oy);
    void detach(const IndexedSurface& surface);
    bool attached(const IndexedSurface& surface) const;

    // Called by Font::draw around drawing the pixel text whose possible
    // pixels lie in the surface rectangle (x0, y0)-(x1, y1).
    void beginItem(const IndexedSurface& surface, int x0, int y0, int x1, int y1);
    void endItem(const IndexedSurface& surface, FontFace face, int cellHeight, int x, int y, int width, std::u32string text,
                 std::uint8_t color);

    // Works out where each text is still visible in `frame` (the finished
    // frame or a copy) and puts back there what was under it.
    void resolve(IndexedSurface& frame);

    const std::vector<Item>& items() const { return items_; }

private:
    struct Attachment {
        const IndexedSurface* surface;
        int ox, oy;
    };
    const Attachment* find(const IndexedSurface& surface) const;
    void snapshot(const IndexedSurface& surface, const Attachment& a, Item& item, std::vector<std::uint8_t>& out) const;

    std::vector<Attachment> attached_;
    std::vector<Item> items_;
    Item pending_;
};

// The interface font at any (fractional) pixel height, anti-aliased: what
// the sharp text is drawn with.
class SmoothFont {
public:
    struct Glyph {
        float advance = 0;
        int left = 0, top = 0; // of the bitmap, from the pen position on the baseline
        int width = 0, height = 0;
        std::vector<std::uint8_t> coverage; // 0-255
    };

    SmoothFont(FontFace face, float cellHeight);
    ~SmoothFont();
    SmoothFont(const SmoothFont&) = delete;
    SmoothFont& operator=(const SmoothFont&) = delete;

    float baseline() const { return baseline_; }
    const Glyph& glyph(char32_t c) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    float baseline_ = 0;
    mutable std::unordered_map<char32_t, Glyph> glyphs_;
};

} // namespace openday::render
