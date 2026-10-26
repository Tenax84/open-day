// HD interface (docs/mechanics/interface.md, "HD interface"): the pictures
// of a frame that have a high-resolution version (the interface atlas) are
// recorded where they are drawn, so that the version can be drawn over the
// scaled frame. The frame keeps the original pixels; `resolve` finds where
// each picture is still visible (not drawn over by something later).
#pragma once

#include <functional>
#include <vector>

#include "formats/dbi.hpp"
#include "render/indexed_surface.hpp"

namespace openday::render {

class ImageLayer {
public:
    struct Rect {
        int x = 0, y = 0, w = 0, h = 0;
    };
    struct Item {
        int key = 0;
        const formats::DbiSprite* sprite = nullptr;
        Rect source; // the part of the sprite drawn
        int x = 0, y = 0; // where its top-left landed in the frame
        // After resolve: frame rectangles where the picture is visible.
        std::vector<Rect> visible;
        bool allVisible = false;
    };

    // While a layer is active, pictures drawn into its attached surfaces
    // are recorded (IndexedSurface::blit, ui::drawTiled ask).
    static ImageLayer* active();
    static void setActive(ImageLayer* layer);

    // The key of a picture with a high-resolution version, or -1.
    void setKeys(std::function<int(const formats::DbiSprite&)> keyOf) { keyOf_ = std::move(keyOf); }

    void clear();
    // Pictures drawn into `surface` are recorded; its (0, 0) is at (ox, oy)
    // of the frame.
    void attach(const IndexedSurface& surface, int ox, int oy);
    void detach(const IndexedSurface& surface);
    bool attached(const IndexedSurface& surface) const;

    // The `source` part of `sprite` drawn into `surface` with its top-left
    // at (x, y), clipped to the surface rectangle `clip`.
    void record(const IndexedSurface& surface, const formats::DbiSprite& sprite, Rect source, int x, int y, Rect clip);

    // Works out where each picture is still visible in `frame`.
    void resolve(const IndexedSurface& frame);

    const std::vector<Item>& items() const { return items_; }

private:
    struct Attachment {
        const IndexedSurface* surface;
        int ox, oy;
    };

    std::function<int(const formats::DbiSprite&)> keyOf_;
    std::vector<Attachment> attached_;
    std::vector<Item> items_;
};

} // namespace openday::render
