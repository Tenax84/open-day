#include "anim/unit_animation.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <numbers>

namespace openday::game {
namespace {

constexpr char kActionLetter[kUnitActionCount] = {'L', 'M', 'N', 'S', 'T', 'U', 'P', 'I', 'E'};

// Rounded a * i / n for n > 0.
int lerp(int a, int b, int i, int n) {
    const long long num = static_cast<long long>(b - a) * i;
    const long long q = (std::llabs(num) * 2 + n) / (2LL * n);
    return a + static_cast<int>(num < 0 ? -q : q);
}

// The points of the straight line p -> q, one per pixel along the longer axis.
std::vector<Point> rasterize(Point p, Point q) {
    const int n = std::max(std::abs(q.x - p.x), std::abs(q.y - p.y)) + 1;
    std::vector<Point> out;
    out.reserve(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i)
        out.push_back(n == 1 ? p : Point{lerp(p.x, q.x, i, n - 1), lerp(p.y, q.y, i, n - 1)});
    return out;
}

} // namespace

std::string unitClipName(const UnitType& type, int color, UnitAction action, int facing) {
    const auto a = static_cast<std::size_t>(action);
    if (!type.clips[a] || type.image.empty()) return {};
    const bool single = action == UnitAction::Projectile || action == UnitAction::Impact || action == UnitAction::Explosion;
    int c = color;
    if (single) c = color == 0 ? 0 : 1;
    else if (type.race == 0 && color == 0 && !type.isConvoy) c = 1;
    const int d = single ? 0 : (facing & 7);
    const std::string cs = std::to_string(c);
    return type.image + "\\P" + cs + "\\" + type.image + cs + kActionLetter[a] + std::to_string(d) + ".SMK";
}

std::string buildingClipName(const BuildingType& type) {
    return type.hasExplosion && !type.image.empty() ? "BUILDING\\" + type.image + ".SMK" : std::string();
}

int facingOfStep(int dx, int dy) {
    // Index (dy + 1) * 3 + (dx + 1).
    static constexpr int kFacing[9] = {3, 4, 5, 2, 0, 6, 1, 0, 7};
    return kFacing[(std::clamp(dy, -1, 1) + 1) * 3 + std::clamp(dx, -1, 1) + 1];
}

int facingToward(Point from, Point to) {
    const int dx = from.x - to.x, dy = from.y - to.y;
    if (dx == 0 && dy == 0) return 0;
    double a = std::atan2(static_cast<double>(dy), static_cast<double>(dx)) * 180.0 / std::numbers::pi;
    if (a < 0) a += 360.0;
    const int s = (static_cast<int>(a) + 22) % 360;
    static constexpr int kSector[8] = {2, 3, 4, 5, 6, 7, 0, 1};
    return kSector[s / 45];
}

Point framePosition(int x, int y, int mapSize, int width, int height) {
    const Point o = iso::tileOrigin(x, y, mapSize);
    return {o.x + 27 - width / 2, o.y + 42 - height / 2};
}

Point buildingFramePosition(int x, int y, int sizeX, int sizeY, int mapSize, int width, int height) {
    const Point o = iso::tileOrigin(x, y, mapSize);
    const int cx = 27 + 14 * (sizeX - sizeY);
    const int cy = 28 + 7 * (sizeX + sizeY - 2);
    return {o.x + cx - width / 2, o.y + cy - height / 2};
}

std::vector<Point> straightPath(Point from, Point to) {
    std::vector<Point> path{from};
    Point p = from;
    while (p.x != to.x || p.y != to.y) {
        p.x += (to.x > p.x) - (to.x < p.x);
        p.y += (to.y > p.y) - (to.y < p.y);
        path.push_back(p);
    }
    return path;
}

std::vector<SlideStep> slidePositions(const std::vector<Point>& path, int pixSpeed, int mapSize, int frameWidth,
                                      int frameHeight) {
    std::vector<SlideStep> out;
    const int s = std::max(1, pixSpeed);
    for (std::size_t i = 1; i < path.size(); ++i) {
        const Point a = path[i - 1], b = path[i];
        const int facing = facingOfStep(b.x - a.x, b.y - a.y);
        const std::vector<Point> line = rasterize(framePosition(a.x, a.y, mapSize, frameWidth, frameHeight),
                                                  framePosition(b.x, b.y, mapSize, frameWidth, frameHeight));
        for (std::size_t k = static_cast<std::size_t>(s); k + 1 < line.size(); k += static_cast<std::size_t>(s))
            out.push_back({line[k], b, facing});
    }
    return out;
}

std::vector<Point> projectilePath(Point from, Point to, const UnitType& type, int mapSize, int frameWidth,
                                  int frameHeight) {
    const int gx = std::abs(to.x - from.x), gy = std::abs(to.y - from.y);
    if ((gx == 0 && gy == 0) || (gx == 1 && gy == 1)) return {};
    const Point o0 = iso::tileOrigin(from.x, from.y, mapSize), o1 = iso::tileOrigin(to.x, to.y, mapSize);
    std::vector<Point> line = rasterize({o0.x + 27, o0.y + 14}, {o1.x + 27, o1.y + 14});
    const int n = static_cast<int>(line.size());
    if (type.projectileCurve != 0)
        for (int i = 0; i < n; ++i) {
            const double lift = std::sin(std::numbers::pi * (i + 1) / n) * n * type.projectileCurve / 100.0;
            line[static_cast<std::size_t>(i)].y -= static_cast<int>(lift);
        }
    const std::size_t skip = static_cast<std::size_t>(std::clamp(type.projectileOffset, 0, n));
    std::vector<Point> out;
    const int speed = std::max(1, type.projectileSpeed);
    for (std::size_t i = skip; i < line.size(); ++i) {
        const Point p = line[i];
        if (!out.empty()) {
            const Point last{out.back().x + frameWidth / 2, out.back().y + frameHeight / 2};
            if (std::max(std::abs(p.x - last.x), std::abs(p.y - last.y)) < speed) continue;
        }
        out.push_back({p.x - frameWidth / 2, p.y - frameHeight / 2});
    }
    return out;
}

} // namespace openday::game
