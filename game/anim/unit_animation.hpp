// Unit animation rules: clip names, facings, frame placement and the
// per-tick paths of moves and projectiles. docs/mechanics/unit-animation.md.
#pragma once

#include <string>
#include <vector>

#include "map/map_renderer.hpp"
#include "map/rules.hpp"

namespace openday::game {

// ANMSUNIT.FF entry for a unit action ("" when the unit type has no such
// clip). `color` is the unit's color value, `facing` 0-7.
std::string unitClipName(const UnitType& type, int color, UnitAction action, int facing);

// BUILDING\<ImageID>.SMK, or "" when the type has no explosion.
std::string buildingClipName(const BuildingType& type);

// Facing of one grid step (dx, dy in -1..1, not both 0).
int facingOfStep(int dx, int dy);
// Facing from `from` toward `to` (eight 45 degree sectors); 0 for the same tile.
int facingToward(Point from, Point to);

// Map pixel position of the top-left corner of a width x height frame
// centered on tile (x, y).
Point framePosition(int x, int y, int mapSize, int width, int height);
// The same for a building explosion, centered on the footprint.
Point buildingFramePosition(int x, int y, int sizeX, int sizeY, int mapSize, int width, int height);

// An 8-connected path from `from` to `to`, diagonal steps first. Only for
// tools: the game's path finding is not implemented yet.
std::vector<Point> straightPath(Point from, Point to);

// One tick of a move: frame top-left, the tile being entered and the facing.
struct SlideStep {
    Point pos;
    Point tile;
    int facing = 0;
};

// Frame positions of a move along `path` (start tile first), one per tick,
// for frames of the given size.
std::vector<SlideStep> slidePositions(const std::vector<Point>& path, int pixSpeed, int mapSize, int frameWidth,
                                      int frameHeight);

// Frame top-left positions of a projectile from tile `from` to tile `to`,
// one per tick. Empty when the projectile is not shown (diagonal neighbors).
std::vector<Point> projectilePath(Point from, Point to, const UnitType& type, int mapSize, int frameWidth,
                                  int frameHeight);

// Occlusion repair (unit-animation.md 8): the parts of the cells in front of
// a unit clip played at rest on `tile`, and of a move frame entering tile
// `entered` with `facing`.
std::vector<RefreshCell> refreshAtRest(Point tile);
std::vector<RefreshCell> refreshTranslate(Point entered, int facing);

// MINE\<ImageID>.SMK, or "" when the mine type has no explosion clip.
std::string mineClipName(const MineType& type);

} // namespace openday::game
