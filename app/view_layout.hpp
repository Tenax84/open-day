// The layout of the map screens (battle, city, continent and the map
// screens over them): the map view in the top-left corner, the side panel
// right of it and the message strip below it (docs/mechanics/
// tactical-interface.md 1.1). The panel and the strip keep their size, the
// view takes the rest, so with the resizable window (interface.md) the view
// grows with the window.
#pragma once

#include "app/app.hpp"

namespace openday::app {

struct ViewLayout {
    bool large = false; // the 1024 x 768 art
    int width = 640, height = 480, viewW = 504, viewH = 448;

    ViewLayout() = default;
    // At the standard sizes the original's view_w = floor((W - 131) / 56) x 56,
    // view_h = floor((H - 24) / 28) x 28 give the same view.
    ViewLayout(int w, int h, bool largeArt)
        : large(largeArt), width(w), height(h), viewW(w - panelWidth(largeArt)), viewH(h - stripHeight(largeArt)) {}

    static int panelWidth(bool largeArt) { return largeArt ? 184 : 136; }
    static int stripHeight(bool largeArt) { return largeArt ? 40 : 32; }
    // The standard size of the resolution.
    static ViewLayout standard(bool largeArt) { return largeArt ? ViewLayout(1024, 768, true) : ViewLayout(640, 480, false); }

    ui::Rect view() const { return {0, 0, viewW, viewH}; }
    ui::Rect panel() const { return {viewW, 0, width - viewW, height}; }
    ui::Rect strip() const { return {0, viewH, viewW, height - viewH}; }
};

// The side panel art `frame` at the panel; on a taller screen the large
// art's filler below its controls is repeated down to the bottom.
void drawSidePanel(App& app, render::IndexedSurface& s, int frame, const ViewLayout& layout);
// The message strip (frame 498); on a wider screen its middle, which lies
// under the message box, is repeated so that the right end stays at the
// right edge with the controls anchored there.
void drawMessageStrip(App& app, render::IndexedSurface& s, const ViewLayout& layout);

} // namespace openday::app
