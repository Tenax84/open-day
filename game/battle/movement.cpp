#include "battle/movement.hpp"

#include <algorithm>
#include <climits>
#include <cstdlib>

#include "battle/combat.hpp"

namespace openday::game {
namespace {

// Grid step of each facing (docs/mechanics/unit-animation.md, section 2).
constexpr Point kStep[8] = {{0, 1}, {-1, 1}, {-1, 0}, {-1, -1}, {0, -1}, {1, -1}, {1, 0}, {1, 1}};

enum Movement { kNoneGround = 0, kGround, kTrooper, kGrav, kBoat, kAir, kUnderwater, kTeleport, kNoneWater, kGravFly };

bool contains(const std::vector<Point>& v, Point p) {
    return std::any_of(v.begin(), v.end(), [&](Point q) { return q.x == p.x && q.y == p.y; });
}

bool isWater(TerrainClass c) {
    return c == TerrainClass::ShallowWater || c == TerrainClass::DeepWater || c == TerrainClass::Riverside;
}

// Section 1.1. Cells: '-' blocked, 'E' enterable (cost 10), 'T' enterable
// at cost 13, 'F' fly-over only, 'B' enterable and fly-over.
TileEval baseValue(int movement, TerrainClass terrain) {
    static constexpr const char* kTable[7] = {
        // movement class 0123456789
        "---E-F-E-B", // shallow water
        "---EEFBE-B", // deep water
        "---E-F-E-B", // riverside
        "-EEE-B-E-B", // plain
        "-----F---F", // mountains
        "--T--F---F", // forest
        "-----F---F", // crack
    };
    TileEval e;
    const int t = static_cast<int>(terrain);
    if (t < 0 || t > 6 || movement < 0 || movement > 9) return e;
    const char cell = kTable[t][movement];
    e.enterable = cell == 'E' || cell == 'T' || cell == 'B';
    e.flyOver = cell == 'F' || cell == 'B';
    if (cell == 'T') e.cost = 13;
    if (e.enterable) e.reason = BlockReason::Free;
    return e;
}

} // namespace

TileEval evaluateTile(const BattleState& state, const BattleUnit& unit, Point tile, const PlanContext* context) {
    if (!state.inside(tile) || unit.type->sizeX != 1 || unit.type->sizeY != 1) return {};
    const int movement = unit.type->movement;
    const TerrainClass terrain = state.classAt(tile);
    TileEval e = baseValue(movement, terrain);

    // 1.2 Roads and bridges.
    const int road = state.roadTypeAt(tile);
    if (road >= 0) {
        const bool water = isWater(terrain);
        switch (movement) {
        case kGround:
        case kTrooper:
            if (water && !e.enterable) {
                e.enterable = true;
                e.reason = BlockReason::Road;
            }
            e.cost = road == 0 ? 7 : 4;
            break;
        case kGrav:
        case kAir:
        case kGravFly:
        case kTeleport:
            if (water && !e.enterable) {
                e.enterable = true;
                e.reason = BlockReason::Road;
            }
            break;
        case kBoat:
            if (water) {
                e.enterable = e.flyOver = false;
                e.reason = BlockReason::BridgeBlocksBoat;
            }
            break;
        case kUnderwater:
            if (water) {
                e.enterable = false;
                e.reason = BlockReason::BridgeBlocksBoat;
            }
            break;
        default:
            break;
        }
    }

    // 1.3 Obstacles: they clear "enterable" only.
    auto block = [&](BlockReason why) {
        if (!e.enterable) return;
        e.enterable = false;
        e.reason = why;
    };
    const int other = state.unitAt(tile);
    if (other >= 0 && !(context && contains(context->vacated, tile))) block(BlockReason::Unit);
    if (context && contains(context->claimed, tile)) block(BlockReason::Unit);
    if (state.wallAt(tile) >= 0) block(BlockReason::Wall);
    if (state.buildingAt(tile) >= 0) block(BlockReason::Building);
    const int mine = state.mineAt(tile);
    if (mine >= 0) {
        const BattleMine& m = state.mine(mine);
        const bool known = unit.owner < 0 || unit.owner >= kPlayerSlots || !m.hidden[static_cast<std::size_t>(unit.owner)];
        const bool hovers = movement == kGrav || movement == kAir || movement == kGravFly || movement == kTeleport;
        if (m.armed && known && (m.type == 1 || !hovers)) block(BlockReason::Mine);
    }
    if (state.ruinBlocksMove(tile)) block(BlockReason::Ruin);
    return e;
}

PathMap::PathMap(const BattleState& state, int unitIndex, const PlanContext* context, bool aiSearch)
    : state_(&state), context_(context ? *context : PlanContext{}) {
    const BattleUnit& unit = state.unit(unitIndex);
    start_ = unit.pos;
    n_ = state.size();
    const int movement = unit.type->movement;
    eval_.resize(static_cast<std::size_t>(n_ * n_));
    for (int y = 0; y < n_; ++y)
        for (int x = 0; x < n_; ++x) eval_[at(x, y)] = evaluateTile(state, unit, {x, y}, &context_);
    value_.assign(eval_.size(), 0);
    if (movement == kNoneGround || movement == kNoneWater || !state.inside(start_)) return;
    teleport_ = movement == kTeleport;
    if (teleport_) return;

    // 3.1 Values: start = INT_MAX, unreached = 0, otherwise INT_MAX minus
    // the planning cost. Relaxed by sweeps until nothing changes.
    value_[at(start_.x, start_.y)] = INT_MAX;
    bool changed = false;
    auto update = [&](int x, int y) {
        const TileEval& t = eval_[at(x, y)];
        if (!t.crossable()) return;
        int best = 0, bestFacing = -1;
        for (int f = 0; f < 8; ++f) {
            const int nx = x + kStep[f].x, ny = y + kStep[f].y;
            if (nx < 0 || ny < 0 || nx >= n_ || ny >= n_) continue;
            const int v = value_[at(nx, ny)];
            if (v > best) {
                best = v;
                bestFacing = f;
            }
        }
        if (bestFacing < 0) return;
        const int candidate = best - (t.cost + (bestFacing & 1));
        if (candidate > value_[at(x, y)]) {
            value_[at(x, y)] = candidate;
            changed = true;
        }
    };
    const int n = n_;
    // The sweep area: the usable area 1 ... n-2, or for the computer player
    // a window around the unit with a round cap (section 3.1, AI variant).
    int x0 = 1, y0 = 1, x1 = n - 2, y1 = n - 2;
    int rounds = INT_MAX;
    if (aiSearch && movement != kBoat) {
        rounds = kAiSearchRounds;
        if (aiSearchWindowed(n, start_)) {
            auto place = [n](int lo, int hi, int& a, int& b) {
                if (lo < 1) {
                    hi += 1 - lo;
                    lo = 1;
                }
                if (hi > n - 2) {
                    lo -= hi - (n - 2);
                    hi = n - 2;
                }
                a = std::max(lo, 1);
                b = std::min(hi, n - 2);
            };
            place(start_.x - 20, start_.x + 21, x0, x1);
            place(start_.y - 20, start_.y + 21, y0, y1);
        }
    }
    do {
        changed = false;
        for (int x = x0; x <= x1; ++x)
            for (int y = y0; y <= y1; ++y) update(x, y);
        for (int y = y1; y >= y0; --y)
            for (int x = x1; x >= x0; --x) update(x, y);
        for (int x = 0; x < n; ++x) update(x, 0);
        for (int x = 0; x < n; ++x) update(x, n - 1);
        for (int y = 0; y < n; ++y) update(n - 1, y);
        for (int y = 0; y < n; ++y) update(0, y);
    } while (changed && --rounds > 0);
}

bool aiSearchWindowed(int n, Point unit) {
    if (n < kAiSearchMinSize) return false;
    const int c = (n - 12) / 2;
    const bool central = unit.x >= c && unit.x < c + 12 && unit.y >= c && unit.y < c + 12;
    return !central;
}

bool PathMap::reachable(Point target) const {
    if (!state_->inside(target) || (target.x == start_.x && target.y == start_.y)) return false;
    const TileEval& e = eval_[at(target.x, target.y)];
    if (!e.enterable) return false;
    if (teleport_) return true;
    return value_[at(target.x, target.y)] > 0;
}

std::vector<PathStep> PathMap::pathTo(Point target) const {
    if (!reachable(target)) return {};
    const TileEval& dest = eval_[at(target.x, target.y)];
    // 3.6 Teleport: one step, no search. The facing is chosen by the server.
    if (teleport_) return {{target, dest.cost, true, 0}};

    // 3.3 Walk back from the target to the neighbor with the largest value;
    // on equal values a later neighbor wins only if it is closer to the
    // start in x or in y.
    const Point start = start_;
    std::vector<Point> tiles{target};
    Point cur = target;
    while (cur.x != start.x || cur.y != start.y) {
        int bestFacing = -1;
        Point best;
        for (int f = 0; f < 8; ++f) {
            const Point p{cur.x + kStep[f].x, cur.y + kStep[f].y};
            if (!state_->inside(p)) continue;
            const int v = value_[at(p.x, p.y)];
            if (v <= value_[at(cur.x, cur.y)]) continue;
            if (bestFacing < 0 || v > value_[at(best.x, best.y)]) {
                bestFacing = f;
                best = p;
            } else if (v == value_[at(best.x, best.y)] &&
                       (std::abs(p.x - start.x) < std::abs(best.x - start.x) ||
                        std::abs(p.y - start.y) < std::abs(best.y - start.y))) {
                bestFacing = f;
                best = p;
            }
        }
        if (bestFacing < 0) return {};
        tiles.push_back(best);
        cur = best;
    }
    std::reverse(tiles.begin(), tiles.end());

    std::vector<PathStep> path;
    for (std::size_t i = 1; i < tiles.size(); ++i) {
        const Point d{tiles[i].x - tiles[i - 1].x, tiles[i].y - tiles[i - 1].y};
        int facing = 0;
        for (int f = 0; f < 8; ++f)
            if (kStep[f].x == d.x && kStep[f].y == d.y) facing = f;
        const TileEval& e = eval_[at(tiles[i].x, tiles[i].y)];
        path.push_back({tiles[i], e.cost, e.enterable, facing});
    }
    return path;
}

const TileEval& PathMap::tile(Point p) const { return eval_[at(p.x, p.y)]; }

std::vector<PathStep> findPath(const BattleState& state, int unitIndex, Point target, const PlanContext* context) {
    if (!state.inside(target)) return {};
    return PathMap(state, unitIndex, context).pathTo(target);
}

bool groupTile(const BattleState& state, const BattleUnit& unit, Point target, const PlanContext& context, Point& out,
               bool human) {
    if (evaluateTile(state, unit, target, &context).enterable) {
        out = target;
        return true;
    }
    // A human order needs a clicked tile the unit could stop on if nobody
    // had claimed it; the computer player's Goto searches in both cases.
    if (human) {
        const PlanContext unclaimed{context.vacated, {}};
        if (!evaluateTile(state, unit, target, &unclaimed).enterable) return false;
    }
    // The nearest free tile around the target by squared distance, ties in
    // x-then-y scan order: the orthogonal neighbors, then the diagonals.
    static constexpr Point kAround[8] = {{-1, 0}, {0, -1}, {0, 1}, {1, 0}, {-1, -1}, {-1, 1}, {1, -1}, {1, 1}};
    for (const Point d : kAround) {
        const Point p{target.x + d.x, target.y + d.y};
        if (evaluateTile(state, unit, p, &context).enterable) {
            out = p;
            return true;
        }
    }
    return false;
}

int affordableSteps(const std::vector<PathStep>& path, int availableAp) {
    if (path.empty() || path[0].cost > availableAp) return 0;
    int left = availableAp;
    for (std::size_t i = 0; i < path.size(); ++i) {
        left -= path[i].cost;
        if (left >= 0) continue;
        // The AP ran out on step i. Stop on the previous tile, or, over a
        // fly-over tile, go on to the next tile the unit can stop on.
        if (path[i - 1].enterable) return static_cast<int>(i);
        for (std::size_t j = i; j < path.size(); ++j)
            if (path[j].enterable) return static_cast<int>(j + 1);
        return static_cast<int>(path.size());
    }
    return static_cast<int>(path.size());
}

int moveCost(const std::vector<PathStep>& path, int steps, int availableAp) {
    int sum = 0;
    for (int i = 0; i < steps && i < static_cast<int>(path.size()); ++i) sum += path[static_cast<std::size_t>(i)].cost;
    return std::min(sum, availableAp);
}

int movementBudget(const BattleUnit& u, bool overwatch) {
    int budget = u.availableAp();
    if (overwatch && u.type->canOverwatch && u.type->shotAp <= budget) budget -= u.type->shotAp;
    return std::max(budget, 0);
}

PlannedMove planMove(const BattleState& state, int unit, Point target, bool overwatch) {
    PlannedMove m;
    m.unit = unit;
    m.target = target;
    m.path = findPath(state, unit, target);
    m.affordable = affordableSteps(m.path, movementBudget(state.unit(unit), overwatch));
    return m;
}

std::vector<PlannedMove> planGroupOrders(const BattleState& state, const std::vector<std::pair<int, Point>>& entries,
                                         bool overwatch, bool repeat) {
    std::vector<PlannedMove> out;
    PlanContext context;
    std::vector<std::pair<int, Point>> remaining;
    for (const auto& e : entries)
        if (std::none_of(remaining.begin(), remaining.end(), [&](const auto& r) { return r.first == e.first; }))
            remaining.push_back(e);
    bool progress = true;
    while (progress && !remaining.empty()) {
        progress = false;
        std::vector<std::pair<int, Point>> left;
        for (const auto& [unit, target] : remaining) {
            const BattleUnit& u = state.unit(unit);
            Point dest;
            if (out.size() >= 64 || !groupTile(state, u, target, context, dest, true)) {
                left.push_back({unit, target});
                continue;
            }
            PlannedMove m;
            m.unit = unit;
            m.target = dest;
            m.path = findPath(state, unit, dest, &context);
            m.affordable = affordableSteps(m.path, movementBudget(u, overwatch));
            if (m.affordable == 0) {
                left.push_back({unit, target});
                continue;
            }
            // The tile where the unit really stops this turn is claimed.
            context.vacated.push_back(u.pos);
            context.claimed.push_back(m.path[static_cast<std::size_t>(m.affordable - 1)].pos);
            out.push_back(std::move(m));
            progress = true;
        }
        remaining = std::move(left);
        if (!repeat) break;
    }
    return out;
}

std::vector<PlannedMove> planGroupMove(const BattleState& state, const std::vector<int>& units, Point target,
                                       bool overwatch) {
    std::vector<std::pair<int, Point>> entries;
    for (const int u : units) entries.push_back({u, target});
    return planGroupOrders(state, entries, overwatch, true);
}

bool planMoveToFire(const BattleState& state, int unit, Point target, const PlanContext* context,
                    std::vector<PathStep>& path) {
    path.clear();
    const BattleUnit& u = state.unit(unit);
    if (!u.alive || !state.inside(target)) return false;
    // The target's footprint.
    Point lo = target, hi = target;
    if (const int b = state.buildingAt(target); b >= 0) {
        lo = state.building(b).pos;
        hi = {lo.x + state.building(b).type->sizeX - 1, lo.y + state.building(b).type->sizeY - 1};
    }
    const int range = u.type->range, minRange = u.type->minRange;
    const int ap = u.availableAp();
    const PathMap map(state, unit, context);
    int best = INT_MAX;
    for (int x = lo.x - range; x <= hi.x + range; ++x)
        for (int y = lo.y - range; y <= hi.y + range; ++y) {
            const Point c{x, y};
            if (x >= lo.x - minRange && x <= hi.x + minRange && y >= lo.y - minRange && y <= hi.y + minRange) continue;
            if (!map.reachable(c)) continue;
            std::vector<PathStep> p = map.pathTo(c);
            int cost = 0;
            for (const PathStep& s : p) cost += s.cost;
            if (p.empty() || cost >= ap || cost >= best) continue;
            if (!checkShotFrom(state, unit, c, ap - cost, target, true).ok()) continue;
            best = cost;
            path = std::move(p);
        }
    return best != INT_MAX;
}

std::vector<std::string> previewSprites(const PlannedMove& move) {
    std::vector<std::string> out;
    const int last = static_cast<int>(move.path.size()) - 1;
    for (int i = 0; i <= last; ++i) {
        const bool affordable = i < move.affordable;
        int n = 0;
        if (i == last) n = affordable ? 4 : 5;
        else if (move.path[static_cast<std::size_t>(i)].enterable) n = affordable ? 0 : 1;
        else n = affordable ? 2 : 3;
        out.push_back("PATH000" + std::to_string(n));
    }
    return out;
}

} // namespace openday::game
