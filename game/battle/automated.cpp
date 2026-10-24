#include "battle/automated.hpp"

#include <algorithm>
#include <cstdlib>

#include "battle/movement.hpp"

namespace openday::game {

Side playerUnitSide(int player, const BattleUnit& u, bool coop) {
    if (u.type->isConvoy) {
        if (u.type->race != 0) return player == 0 ? Side::Same : Side::Enemy; // alien convoy: AI side
        if (player == 0) return Side::Enemy;
        return coop ? Side::Ally : Side::Enemy;
    }
    if (u.owner == player) return Side::Same;
    if (u.owner == 0 || player == 0) return Side::Enemy;
    return coop ? Side::Ally : Side::Enemy;
}

std::vector<Point> findBridgeSites(int n, const std::function<TerrainClass(Point)>& cls) {
    auto at = [&](int x, int y) { return cls({x, y}); };
    auto waterOrBank = [](TerrainClass c) { return c == TerrainClass::DeepWater || c == TerrainClass::Riverside; };
    struct Candidate {
        Point start, end;
        int dx, dy;
    };
    std::vector<Candidate> found;
    for (int y = 0; y < n - 1; ++y)
        for (int x = 0; x < n - 1; ++x) {
            if (at(x, y) != TerrainClass::Riverside) continue;
            int dx = 0, dy = 0;
            if (waterOrBank(at(x + 1, y)) && at(x - 1, y) == TerrainClass::Plain) dx = 1;
            else if (waterOrBank(at(x, y + 1)) && at(x, y - 1) == TerrainClass::Plain) dy = 1;
            else continue;
            Point p{x + dx, y + dy};
            while (at(p.x, p.y) == TerrainClass::DeepWater) p = {p.x + dx, p.y + dy};
            if (at(p.x, p.y) != TerrainClass::Riverside || at(p.x + dx, p.y + dy) != TerrainClass::Plain) continue;
            found.push_back({{x, y}, p, dx, dy});
        }
    // Of two neighbouring candidates the second is dropped.
    std::vector<Candidate> kept;
    for (const Candidate& c : found) {
        const bool twin = std::any_of(kept.begin(), kept.end(), [&](const Candidate& k) {
            const bool startNear = std::abs(k.start.x - c.start.x) == 1 || std::abs(k.start.y - c.start.y) == 1;
            const bool endNear = std::abs(k.end.x - c.end.x) == 1 || std::abs(k.end.y - c.end.y) == 1;
            return startNear && endNear;
        });
        if (!twin) kept.push_back(c);
    }
    std::vector<Point> sites;
    auto isSite = [&](Point p) {
        return std::any_of(sites.begin(), sites.end(), [&](Point s) { return s.x == p.x && s.y == p.y; });
    };
    for (const Candidate& c : kept) {
        std::vector<Point> tiles;
        for (Point p = c.start;; p = {p.x + c.dx, p.y + c.dy}) {
            tiles.push_back(p);
            if (p.x == c.end.x && p.y == c.end.y) break;
        }
        if (std::none_of(tiles.begin(), tiles.end(), isSite)) sites.insert(sites.end(), tiles.begin(), tiles.end());
    }
    return sites;
}

bool bridgePlacementOk(const BattleState& s, Point t) {
    if (!s.inside(t) || !s.isBridgeSite(t)) return false;
    if (s.unitAt(t) >= 0 || s.buildingAt(t) >= 0 || s.wallAt(t) >= 0 || s.roadAt(t) >= 0) return false;
    const TerrainClass c = s.classAt(t);
    if (c == TerrainClass::Riverside) return true;
    if (c != TerrainClass::DeepWater) return false;
    static constexpr Point kOrtho[4] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    return std::any_of(std::begin(kOrtho), std::end(kOrtho), [&](Point d) { return s.roadAt({t.x + d.x, t.y + d.y}) >= 0; });
}

int automatedPoints(const AutomatedType& type) { return type.kind() == AutomatedCategory::Transport ? 2 : 1; }

bool automatedTargetOk(const BattleState& s, int player, const AutomatedType& type, Point t, bool coop, const Point* source) {
    if (player > 0 && player < kPlayerSlots && s.deployBudget[static_cast<std::size_t>(player)] < type.cost) return false;
    if (!s.inside(t)) return false;
    const int u = s.unitAt(t), b = s.buildingAt(t), w = s.wallAt(t);
    switch (type.kind()) {
    case AutomatedCategory::Destroy:
        if (b >= 0 && buildingSide(player, s.building(b), coop) == Side::Same) return false;
        return u < 0 || playerUnitSide(player, s.unit(u), coop) != Side::Same;
    case AutomatedCategory::Repair:
        if (u >= 0) return playerUnitSide(player, s.unit(u), coop) == Side::Same;
        if (b >= 0) return buildingSide(player, s.building(b), coop) == Side::Same;
        return w >= 0;
    case AutomatedCategory::Build: return bridgePlacementOk(s, t);
    case AutomatedCategory::Mine: {
        if (b >= 0 || u >= 0 || w >= 0) return false;
        const TerrainClass c = s.classAt(t);
        return c != TerrainClass::Mountains && c != TerrainClass::Forest;
    }
    case AutomatedCategory::Drop: return true;
    case AutomatedCategory::Transport: {
        if (!source) {
            if (u < 0) return false;
            const BattleUnit& unit = s.unit(u);
            return playerUnitSide(player, unit, coop) == Side::Same && unit.type->movement != 0 &&
                   unit.type->movement != 8 && !unit.type->isConvoy;
        }
        const int moved = s.unitAt(*source);
        if (moved < 0 || b >= 0 || u >= 0 || w >= 0) return false;
        return evaluateTile(s, s.unit(moved), t).enterable;
    }
    }
    return false;
}

std::string automatedClipName(const AutomatedType& type, bool second, AutomatedOutcome outcome) {
    if (type.image.empty()) return {};
    return "AUTOUNIT\\" + type.image + (second ? "1" : "0") + std::to_string(static_cast<int>(outcome)) + ".SMK";
}

} // namespace openday::game
