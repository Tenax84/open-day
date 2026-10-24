#include "battle/combat.hpp"

#include <algorithm>
#include <cstdlib>

namespace openday::game {

Side unitSide(const BattleUnit& a, const BattleUnit& b, bool coop) {
    const bool ca = a.type->isConvoy, cb = b.type->isConvoy;
    if (!ca && !cb) {
        if (a.owner == b.owner) return Side::Same;
        if (a.owner == 0 || b.owner == 0) return Side::Enemy;
        return coop ? Side::Ally : Side::Enemy;
    }
    if (ca && cb) return a.type->race == 0 && b.type->race == 0 ? Side::Ally : Side::Enemy;
    const BattleUnit& convoy = ca ? a : b;
    const BattleUnit& other = ca ? b : a;
    if (convoy.type->race != 0) return other.owner == 0 ? Side::Same : Side::Enemy; // alien convoy: AI side
    if (other.owner == 0) return Side::Enemy;
    return coop ? Side::Ally : Side::Enemy;
}

Side buildingSide(int player, const BattleBuilding& b, bool coop) {
    if (b.owner == player) return Side::Same;
    if (b.owner == 0 || player == 0) return Side::Enemy;
    return coop ? Side::Ally : Side::Enemy;
}

int chebyshev(Point a, Point b) { return std::max(std::abs(a.x - b.x), std::abs(a.y - b.y)); }

Point centerTile(const BattleUnit& u) {
    return {u.pos.x + (u.type->sizeX - 1) / 2, u.pos.y + (u.type->sizeY - 1) / 2};
}

std::vector<Point> fireLine(Point a, Point b) {
    // Always generated from the end with the smaller y (a on a tie).
    const bool swap = b.y < a.y;
    const Point p = swap ? b : a, q = swap ? a : b;
    const int dy = q.y - p.y, dx = q.x - p.x;
    const int sx = dx > 0 ? 1 : -1, ax = std::abs(dx);
    std::vector<Point> line{p};
    Point c = p;
    if (ax <= dy) {
        int e = 2 * ax - dy;
        for (int i = 0; i < dy; ++i) {
            ++c.y;
            if (e >= 0) {
                c.x += sx;
                e += 2 * ax - 2 * dy;
            } else {
                e += 2 * ax;
            }
            line.push_back(c);
        }
    } else {
        int e = 2 * dy - ax;
        for (int i = 0; i < ax; ++i) {
            c.x += sx;
            if (e >= 0) {
                ++c.y;
                e += 2 * dy - 2 * ax;
            } else {
                e += 2 * dy;
            }
            line.push_back(c);
        }
    }
    if (swap) std::reverse(line.begin(), line.end());
    return line;
}

namespace {

bool insideFootprint(Point p, Point pos, int sx, int sy) {
    return p.x >= pos.x && p.y >= pos.y && p.x < pos.x + sx && p.y < pos.y + sy;
}

// The shooter's own footprint never blocks (matters for 3 x 3 towers) *(OpenDay choice)*.
bool lineIsClear(const BattleState& state, int shooter, Point from, Point aim) {
    const std::vector<Point> line = fireLine(from, aim);
    for (std::size_t i = 1; i + 1 < line.size(); ++i) {
        const Point t = line[i];
        const TerrainType* terrain = state.terrainAt(t);
        if (terrain && !terrain->canFireThrough) return false;
        if (state.ruinBlocksFire(t)) return false;
        const int u = state.unitAt(t);
        if ((u >= 0 && u != shooter) || state.wallAt(t) >= 0) return false;
        const int b = state.buildingAt(t);
        if (b >= 0) {
            const BattleBuilding& bd = state.building(b);
            const int sx = bd.type->sizeX, sy = bd.type->sizeY;
            if (sx == 1 && sy == 1) return false;
            // A big building lets a shot pass to one of its other corners.
            const int right = bd.pos.x + sx - 1, bottom = bd.pos.y + sy - 1;
            const bool farCorner = (aim.x == right && aim.y == bd.pos.y) || (aim.x == bd.pos.x && aim.y == bottom) ||
                                   (aim.x == right && aim.y == bottom);
            if (!farCorner) return false;
        }
    }
    return true;
}

} // namespace

ShotCheck checkShot(const BattleState& state, int shooterIndex, Point target, bool coop) {
    const BattleUnit& shooter = state.unit(shooterIndex);
    return checkShotFrom(state, shooterIndex, shooter.pos, shooter.availableAp(), target, coop);
}

ShotCheck checkShotFrom(const BattleState& state, int shooterIndex, Point pos, int availableAp, Point target, bool coop,
                        bool geometryOnly) {
    ShotCheck r;
    const BattleUnit& shooter = state.unit(shooterIndex);
    const UnitType& t = *shooter.type;
    if (!shooter.alive || !state.inside(target) || insideFootprint(target, pos, t.sizeX, t.sizeY)) return r;
    if (!geometryOnly && !t.hasWeapon) return {ShotError::NoWeapon, {}};
    if (!geometryOnly && availableAp < t.shotAp) return {ShotError::NotEnoughAp, {}};

    const int building = state.buildingAt(target);
    if (shooter.owner != 0 && !geometryOnly) {
        if (building >= 0) {
            if (buildingSide(shooter.owner, state.building(building), coop) != Side::Enemy) return r;
        } else if (const int u = state.unitAt(target); u >= 0) {
            if (unitSide(shooter, state.unit(u), coop) != Side::Enemy) return r;
        }
    }

    std::vector<Point> candidates;
    if (building >= 0) {
        const BattleBuilding& b = state.building(building);
        for (int x = b.pos.x; x < b.pos.x + b.type->sizeX; ++x)
            for (int y = b.pos.y; y < b.pos.y + b.type->sizeY; ++y)
                if (state.inside({x, y})) candidates.push_back({x, y});
    } else {
        candidates.push_back(target);
    }
    const Point from{pos.x + (t.sizeX - 1) / 2, pos.y + (t.sizeY - 1) / 2}; // centre tile
    bool tooFar = false, tooClose = false;
    for (const Point a : candidates) {
        const int d = chebyshev(from, a);
        if (d > t.range) {
            tooFar = true;
            continue;
        }
        if (d <= t.minRange) {
            tooClose = true;
            continue;
        }
        if (t.indirect || lineIsClear(state, shooterIndex, from, a)) return {ShotError::None, a};
    }
    r.error = tooFar ? ShotError::OutOfRange : tooClose ? ShotError::TooClose : ShotError::NoLineOfFire;
    return r;
}

int hitChance(const BattleState& state, int shooterIndex, Point aim) {
    const BattleUnit& shooter = state.unit(shooterIndex);
    const int r = chebyshev(shooter.pos, aim); // from the top-left tile, as the original
    if (r <= 1) return 101;
    const auto& ranges = state.rules().ranges;
    const int band = r <= ranges[0] ? 0 : r <= ranges[1] ? 1 : 2;
    int c = shooter.type->hit[static_cast<std::size_t>(band)];
    const int target = state.unitAt(aim);
    if (target >= 0 && state.unit(target).owner == state.currentPlayer) c = c * state.unit(target).type->jammer / 100;
    return c;
}

} // namespace openday::game
