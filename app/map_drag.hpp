// Pixel-by-pixel scrolling of the isometric map views: right-button drag and
// smooth edge scrolling (docs/mechanics/tactical-interface.md 3.6, OpenDay
// additions, each switched by an OpenDay option), and the original stepping;
// the exact minimap click and frame (2.6, also an OpenDay option).
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "map/map_renderer.hpp"
#include "ui/widgets.hpp"

namespace openday::app {

struct MapDrag {
    // A press that moves less than this (in screen pixels) stays a click.
    static constexpr int kThreshold = 4;

    bool held = false;  // the right button went down over the map
    bool moved = false; // past the threshold: the camera follows the cursor
    int startX = 0, startY = 0;
    float camX = 0, camY = 0; // the camera at the press

    void press(int x, int y, float cx, float cy) {
        held = true;
        moved = false;
        startX = x;
        startY = y;
        camX = cx;
        camY = cy;
    }

    // On a mouse move while held: true once dragging, with the new camera.
    bool follow(int x, int y, float& cx, float& cy) {
        if (!held) return false;
        if (!moved && std::abs(x - startX) < kThreshold && std::abs(y - startY) < kThreshold) return false;
        moved = true;
        cx = camX - static_cast<float>(x - startX);
        cy = camY - static_cast<float>(y - startY);
        return true;
    }

    // On the release: true when the press was a click, not a drag.
    bool release() {
        const bool click = held && !moved;
        held = moved = false;
        return click;
    }

    bool dragging() const { return held && moved; }
};

// Edge scrolling, also pixel by pixel: the camera glides at 10, 20 or 40 tiles
// per second (56 px across, 28 px down) by the scroll speed setting.
inline void edgeScrollBy(int dx, int dy, int speed, int ms, float& cx, float& cy) {
    static constexpr float kTilesPerSecond[3] = {10.0f, 20.0f, 40.0f};
    const float tiles = kTilesPerSecond[std::clamp(speed, 0, 2)] * static_cast<float>(ms) / 1000.0f;
    cx += static_cast<float>(dx * 56) * tiles;
    cy += static_cast<float>(dy * 28) * tiles;
}

// The original edge scrolling: one tile step at most every 100 / 50 / 0 ms
// by the scroll speed setting. `acc` collects the time; true = step now.
inline bool edgeScrollStep(int& acc, int speed, int ms) {
    static constexpr int kInterval[3] = {100, 50, 0};
    acc += ms;
    if (acc < kInterval[std::clamp(speed, 0, 2)]) return false;
    acc = 0;
    return true;
}

// Minimap points in tile units: pixel / scale, so tile (x, y) covers
// [x, x + 1) and its diamond's centre is (x + 0.5, y + 0.5).

// The camera that puts the minimap point (u, v) in the middle of the view.
inline void cameraOnMinimapPoint(double u, double v, int mapSize, int viewW, int viewH, float& cx, float& cy) {
    u -= 0.5;
    v -= 0.5;
    cx = static_cast<float>((u - v + mapSize - 1) * game::iso::kHalfStepX + 27 - viewW / 2);
    cy = static_cast<float>((u + v) * game::iso::kHalfStepY + 42 - viewH / 2);
}

// The minimap point in the middle of the view; the inverse of the above.
inline void minimapPointOfView(float cx, float cy, int mapSize, int viewW, int viewH, double& u, double& v) {
    const double a = (cx + viewW / 2 - 27) / game::iso::kHalfStepX - (mapSize - 1); // u - v
    const double b = (cy + viewH / 2 - 42) / game::iso::kHalfStepY;                 // u + v
    u = (a + b) / 2 + 0.5;
    v = (b - a) / 2 + 0.5;
}

// The viewport frame on a minimap image at `image` (`scale` px per tile):
// the original's square of `span` tiles from the view's top-left tile, or,
// with `exact`, the same square centred on the middle of the view.
// Clamped into the image.
inline ui::Rect minimapFrame(const ui::Rect& image, double scale, int mapSize, float cx, float cy, int viewW, int viewH, bool exact) {
    const int span = std::max(viewW / 54, viewH / 28);
    double left = 0, top = 0;
    if (exact) {
        minimapPointOfView(cx, cy, mapSize, viewW, viewH, left, top);
        left -= span / 2.0;
        top -= span / 2.0;
    } else {
        const game::Point tl = game::iso::tileAt(static_cast<int>(cx), static_cast<int>(cy), mapSize);
        left = tl.x;
        top = tl.y;
    }
    const int size = static_cast<int>(span * scale);
    ui::Rect v{image.x + static_cast<int>(std::floor(left * scale)), image.y + static_cast<int>(std::floor(top * scale)), size, size};
    if (exact) { // cut off what lies outside the image
        const int x1 = std::min(v.x + v.w, image.x + image.w), y1 = std::min(v.y + v.h, image.y + image.h);
        v.x = std::max(v.x, image.x);
        v.y = std::max(v.y, image.y);
        v.w = std::max(2, x1 - v.x);
        v.h = std::max(2, y1 - v.y);
    }
    v.x = std::clamp(v.x, image.x, image.x + image.w - 2);
    v.y = std::clamp(v.y, image.y, image.y + image.h - 2);
    v.w = std::min(v.w, image.x + image.w - v.x);
    v.h = std::min(v.h, image.y + image.h - v.y);
    return v;
}

} // namespace openday::app
