// Rules of the two map screens that do not depend on drawing: the list rows,
// the selection blink, the click conversion and the grouping of marker tiles
// (docs/mechanics/tactical-interface.md 7, strategic-interface.md 4.2).
#pragma once

#include <optional>
#include <vector>

#include "map/map_renderer.hpp"
#include "map/rules.hpp"

namespace openday::game {

// One list row: a type and how many objects of it the owner has.
struct OverviewRow {
    int type = 0;
    int count = 0;
};
// Counts the types in the order in which they are first met.
std::vector<OverviewRow> overviewRows(const std::vector<int>& typesInOrder);

// The selection blink (7.1): phase 0 → -255 (black), 2 → +255 (white),
// 1 and 3 → 0; the offset is added to R, G and B, each clamped to 0 ... 255.
int blinkOffset(int phase);
Rgb blinkColor(Rgb c, int phase);

// A left click at picture pixel (px, py) of a picture `pictureSize` wide
// showing an n × n map: tile = trunc(pixel / s), s = pictureSize / n.
// nullopt outside the map.
std::optional<Point> overviewTile(int px, int py, int pictureSize, int mapSize);

// Tile rectangles covering a set of tiles: contiguous rectangular blocks,
// built greedily from the top-left (OpenDay's grouping; the original rule
// was not traced, tactical-interface.md 7.2).
struct TileRect {
    int x = 0, y = 0, w = 1, h = 1;
    Point centre() const { return {x + (w - 1) / 2, y + (h - 1) / 2}; }
};
std::vector<TileRect> groupTiles(const std::vector<Point>& tiles);

} // namespace openday::game
