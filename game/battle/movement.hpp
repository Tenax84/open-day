// Tactical movement rules: tile evaluation, path finding, cutting a path to
// the unit's AP, and the path preview. docs/mechanics/tactical-movement.md.
#pragma once

#include <string>
#include <utility>
#include <vector>

#include "battle/battle_state.hpp"

namespace openday::game {

// Why a tile cannot be entered (section 1.3). Feedback only.
enum class BlockReason { Free = 0, Terrain = 1, Unit = 2, Wall = 3, Building = 4, Mine = 5, BridgeBlocksBoat = 6, Ruin = 7, Road = 8 };

struct TileEval {
    bool enterable = false; // the unit may stop here
    bool flyOver = false;   // the unit may pass over (or under) without stopping
    int cost = 10;          // AP for entering the tile
    BlockReason reason = BlockReason::Terrain;

    bool crossable() const { return enterable || flyOver; }
};

// The computer player's path search (section 3.1, AI variant).
inline constexpr int kAiSearchMinSize = 57; // maps from this size use the window and the random draw
inline constexpr int kAiSearchRounds = 7;
// Does a unit at `unit` (top-left tile) on an n x n map get the 42 x 42
// window (outside the central 12 x 12 square of a large map)?
bool aiSearchWindowed(int n, Point unit);

// Adjustments for planning one unit of a group order (section 3.5).
struct PlanContext {
    std::vector<Point> vacated; // tiles earlier units of the group leave: their units do not block
    std::vector<Point> claimed; // destinations of earlier units of the group
};

TileEval evaluateTile(const BattleState& state, const BattleUnit& unit, Point tile, const PlanContext* context = nullptr);

struct PathStep {
    Point pos;
    int cost = 0;          // AP charged for this step
    bool enterable = true; // false: fly-over tile
    int facing = 0;        // direction of the step
};

// The path search of section 3.1 for one unit, done once and then asked
// for any number of destinations.
class PathMap {
public:
    // `aiSearch`: the computer player's restricted search (section 3.1, AI
    // variant: a 42 x 42 window on large maps and at most 7 rounds; boats
    // excepted). The random draw that goes with it is the caller's.
    PathMap(const BattleState& state, int unit, const PlanContext* context = nullptr, bool aiSearch = false);
    // The unit can stop on `target` and get there (not its own tile).
    bool reachable(Point target) const;
    // Path to `target` (start tile excluded); empty when not reachable.
    std::vector<PathStep> pathTo(Point target) const;
    const TileEval& tile(Point p) const;

private:
    std::size_t at(int x, int y) const { return static_cast<std::size_t>(y * n_ + x); }

    const BattleState* state_;
    PlanContext context_;
    Point start_;
    int n_ = 0;
    bool teleport_ = false;
    std::vector<TileEval> eval_;
    std::vector<int> value_;
};

// Destination of one unit of a group order: `target`, or else the nearest
// free tile around it (section 3.5). False when there is none. `human`: a
// target the unit cannot stop on (claims aside) gives no order; otherwise
// (the computer player's Goto) the neighbors are tried in that case too.
bool groupTile(const BattleState& state, const BattleUnit& unit, Point target, const PlanContext& context, Point& out,
               bool human = false);

// Shortest path from the unit's tile to `target` (start tile excluded), not
// limited by AP. Empty when the target is not enterable or not reachable.
// Teleport units get one step straight to the target.
std::vector<PathStep> findPath(const BattleState& state, int unit, Point target, const PlanContext* context = nullptr);

// Number of leading steps the unit walks this turn with `availableAp`
// (section 2, "When AP runs short"); 0 = it does not move.
int affordableSteps(const std::vector<PathStep>& path, int availableAp);

// AP charged for walking the first `steps` steps, clamped to `availableAp`.
int moveCost(const std::vector<PathStep>& path, int steps, int availableAp);

struct PlannedMove {
    int unit = -1;
    Point target;
    std::vector<PathStep> path; // full path to the target
    int affordable = 0;         // steps walked this turn
};

// AP a human move planner may spend (section 2, "Keeping AP for a shot"):
// with the Overwatch option on, one shot's worth is kept for units with
// canOverwatch that can afford it.
int movementBudget(const BattleUnit& unit, bool overwatch);

// Plans a move order for one unit.
PlannedMove planMove(const BattleState& state, int unit, Point target, bool overwatch = false);

// Plans a human group order (section 3.5): (unit, clicked tile) entries in
// order, up to 64 units, each unit once; paths cut to the movement budget.
// With `repeat` the pass is repeated over the units left out while the
// previous pass gave someone an order (two-click move); MOVE ALL makes one
// pass. Units without an order this turn are left out.
std::vector<PlannedMove> planGroupOrders(const BattleState& state, const std::vector<std::pair<int, Point>>& entries,
                                         bool overwatch, bool repeat);
// The two-click group move: every unit toward `target`, with repeat passes.
std::vector<PlannedMove> planGroupMove(const BattleState& state, const std::vector<int>& units, Point target,
                                       bool overwatch = false);

// The firing tile of a human move-to-fire order (section 3.7): the cheapest
// path (cost strictly below the available AP) to a candidate tile from
// which a shot at `target` is legal with the AP left. False if none.
bool planMoveToFire(const BattleState& state, int unit, Point target, const PlanContext* context,
                    std::vector<PathStep>& path);

// SYMBOLS.DBI marker of every step of a planned move (section 3.4).
std::vector<std::string> previewSprites(const PlannedMove& move);

} // namespace openday::game
