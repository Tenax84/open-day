// Tactical movement rules: tile evaluation, path finding, cutting a path to
// the unit's AP, and the path preview. docs/mechanics/tactical-movement.md.
#pragma once

#include <string>
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

// Plans a move order for one unit.
PlannedMove planMove(const BattleState& state, int unit, Point target);

// Plans a group order (up to 64 units, in the given order). Units whose
// order cannot be carried out this turn are left out.
std::vector<PlannedMove> planGroupMove(const BattleState& state, const std::vector<int>& units, Point target);

// SYMBOLS.DBI marker of every step of a planned move (section 3.4).
std::vector<std::string> previewSprites(const PlannedMove& move);

} // namespace openday::game
