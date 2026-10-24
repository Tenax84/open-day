#include "info/overview.hpp"

#include <algorithm>
#include <set>

namespace openday::game {

std::vector<OverviewRow> overviewRows(const std::vector<int>& types) {
    std::vector<OverviewRow> rows;
    for (const int t : types) {
        const auto it = std::find_if(rows.begin(), rows.end(), [t](const OverviewRow& r) { return r.type == t; });
        if (it == rows.end()) rows.push_back({t, 1});
        else ++it->count;
    }
    return rows;
}

int blinkOffset(int phase) {
    switch (phase & 3) {
    case 0: return -255;
    case 2: return 255;
    default: return 0;
    }
}

Rgb blinkColor(Rgb c, int phase) {
    const int o = blinkOffset(phase);
    return {std::clamp(c.r + o, 0, 255), std::clamp(c.g + o, 0, 255), std::clamp(c.b + o, 0, 255)};
}

std::optional<Point> overviewTile(int px, int py, int pictureSize, int mapSize) {
    if (pictureSize <= 0 || mapSize <= 0 || px < 0 || py < 0 || px >= pictureSize || py >= pictureSize) return std::nullopt;
    const double s = static_cast<double>(pictureSize) / mapSize;
    const Point t{static_cast<int>(px / s), static_cast<int>(py / s)};
    if (t.x >= mapSize || t.y >= mapSize) return std::nullopt;
    return t;
}

std::vector<TileRect> groupTiles(const std::vector<Point>& tiles) {
    std::set<std::pair<int, int>> left; // (y, x): rows first
    for (const Point p : tiles) left.insert({p.y, p.x});
    std::vector<TileRect> out;
    while (!left.empty()) {
        const auto [y0, x0] = *left.begin();
        TileRect r{x0, y0, 1, 1};
        while (left.count({y0, r.x + r.w})) ++r.w;
        // Grow downwards while the whole row below is still free.
        for (;;) {
            bool full = true;
            for (int x = r.x; x < r.x + r.w && full; ++x) full = left.count({r.y + r.h, x}) > 0;
            if (!full) break;
            ++r.h;
        }
        for (int y = r.y; y < r.y + r.h; ++y)
            for (int x = r.x; x < r.x + r.w; ++x) left.erase({y, x});
        out.push_back(r);
    }
    return out;
}

} // namespace openday::game
