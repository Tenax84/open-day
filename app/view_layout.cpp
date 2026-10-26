#include "app/view_layout.hpp"

#include <algorithm>

namespace openday::app {
namespace {

// The rows of the large panel art below the 640 x 480 panel: filler only
// (tactical-interface.md 1.1).
constexpr int kFillerTop = 480;
// The strip's right end with the CHAT and MENU slots, and the piece repeated
// left of it (inside the message box, 64 ... view_w - 135, at both sizes).
constexpr int kStripRight = 140, kStripRepeat = 60;

} // namespace

void drawSidePanel(App& app, render::IndexedSurface& s, int frame, const ViewLayout& l) {
    const ui::UiContext& ui = app.ui(l.large ? 1 : 0);
    const ui::Rect p = l.panel();
    ui::drawFrame(s, ui, frame, p);
    const formats::DbiSprite* art = ui.atlas ? ui.atlas->get(frame) : nullptr;
    const int top = art ? art->height : kFillerTop;
    if (p.h <= top) return;
    // The filler of the large art, also under the small panel (as wide as it).
    const ui::UiContext& large = app.ui(1);
    const formats::DbiSprite* filler = large.atlas ? large.atlas->get(frame) : nullptr;
    const ui::Rect below{p.x, top, p.w, p.h - top};
    if (!filler || filler->height <= kFillerTop) {
        for (int y = below.y; y < std::min(s.height(), below.y + below.h); ++y)
            std::fill(s.row(y) + below.x, s.row(y) + std::min(s.width(), below.x + below.w), ui.black);
        return;
    }
    ui::drawTiled(s, *filler, {0, kFillerTop, std::min(p.w, filler->width), filler->height - kFillerTop}, below);
}

void drawMessageStrip(App& app, render::IndexedSurface& s, const ViewLayout& l) {
    const ui::UiContext& ui = app.ui(l.large ? 1 : 0);
    const ui::Rect r = l.strip();
    const formats::DbiSprite* art = ui.atlas ? ui.atlas->get(498) : nullptr;
    if (!art || r.w <= art->width || art->width < kStripRight + kStripRepeat) {
        ui::drawFrame(s, ui, 498, r);
        return;
    }
    const int split = art->width - kStripRight;
    ui::drawTiled(s, *art, {0, 0, split, art->height}, {r.x, r.y, split, r.h});
    ui::drawTiled(s, *art, {split - kStripRepeat, 0, kStripRepeat, art->height}, {r.x + split, r.y, r.w - kStripRight - split, r.h});
    ui::drawTiled(s, *art, {split, 0, kStripRight, art->height}, {r.x + r.w - kStripRight, r.y, kStripRight, r.h});
}

} // namespace openday::app
