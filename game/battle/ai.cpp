#include "battle/ai.hpp"

#include <algorithm>
#include <cstdlib>
#include <iterator>

#include "battle/combat.hpp"

namespace openday::game {
namespace {

// Steps per action and turn (section 1.4). OpenDay counts evaluate, move and
// fire as one step each, so a cycle costs kCycle and an action gets three.
constexpr int kStepBudget = 10;
constexpr int kCycle = 3;

bool isMobile(const BattleUnit& u) { return u.type->movement != 0 && u.type->movement != 8; }
bool isAlien(const BattleUnit& u) { return u.type->race != 0; }
bool same(Point a, Point b) { return a.x == b.x && a.y == b.y; }

bool contains(const std::vector<Point>& v, Point p) {
    return std::any_of(v.begin(), v.end(), [&](Point q) { return same(p, q); });
}

// Stable insertion sort: each unit goes before the first unit already in
// the list for which before(unit, other) holds (section 1.3).
template <class Before> std::vector<int> insertionSort(const std::vector<int>& in, Before before) {
    std::vector<int> out;
    for (const int u : in) {
        auto it = std::find_if(out.begin(), out.end(), [&](int o) { return before(u, o); });
        out.insert(it, u);
    }
    return out;
}

} // namespace

TacticalAi::TacticalAi(BattleServer& server) : server_(server), state_(server.state_) {}

bool TacticalAi::over() const { return state_.over; }

bool TacticalAi::isEnemy(const BattleUnit& u) const {
    if (!u.alive) return false;
    if (u.type->isConvoy) return u.type->race == 0; // human convoys, whoever owns them
    return u.owner != 0;
}

bool TacticalAi::canFireNow(const BattleUnit& u) const { return u.type->hasWeapon && u.availableAp() >= u.type->shotAp; }

std::vector<int> TacticalAi::enemyUnits() const {
    std::vector<int> out;
    for (int i = 0; i < static_cast<int>(state_.units().size()); ++i)
        if (isEnemy(state_.unit(i))) out.push_back(i);
    return out;
}

// A legal shot as if the shooter stood on `from` (section 0).
bool TacticalAi::legalFrom(int shooter, Point from, Point target) {
    const Point home = state_.unit(shooter).pos;
    if (same(home, from)) return checkShot(state_, shooter, target, server_.options_.coop).ok();
    if (state_.unitAt(from) >= 0) return false;
    state_.moveUnit(shooter, from);
    const bool ok = checkShot(state_, shooter, target, server_.options_.coop).ok();
    state_.moveUnit(shooter, home);
    return ok;
}

// Could unit `target` shoot `shooter` if the shooter stood on `at`?
bool TacticalAi::targetCanHit(int target, int shooter, Point at) {
    const Point home = state_.unit(shooter).pos;
    if (!same(home, at)) {
        if (state_.unitAt(at) >= 0) return false;
        state_.moveUnit(shooter, at);
    }
    const bool ok = checkShot(state_, target, at, server_.options_.coop).ok();
    if (!same(home, at)) state_.moveUnit(shooter, home);
    return ok;
}

int TacticalAi::score(int shooter, int unit, int building, int steps, int weight) const {
    const BattleUnit& s = state_.unit(shooter);
    const UnitType& t = *s.type;
    const int shots = t.shotAp > 0 ? s.availableAp() / t.shotAp : 0;
    const int avgDamage = t.damageBase + t.damageRandom / 2;
    const double power = static_cast<double>(shots) * avgDamage;
    int armor = 1, category = 9;
    if (unit >= 0) {
        armor = state_.unit(unit).armor;
        category = state_.unit(unit).type->aiCategory;
    } else if (building >= 0) {
        armor = state_.building(building).armor;
        category = state_.building(building).type->aiCategory;
    }
    const double k = std::min(100.0, 100.0 * power / std::max(1, armor));
    double base = k / 100.0 * weight - std::min(500, 10 * steps);
    if (unit >= 0 && k > 99) base += state_.unit(unit).type->cost;
    const auto& prio = state_.rules().aiPriority;
    const int src = std::clamp(t.aiCategory, 0, 11), dst = std::clamp(category, 0, 11);
    double p = prio[static_cast<std::size_t>(src)][static_cast<std::size_t>(dst)];
    if (unit >= 0) {
        if (state_.unit(unit).type->typeId == s.order.enemyType) p *= 2;
        if (t.apDamage && !canFireNow(state_.unit(unit))) p = 0;
    } else if (t.apDamage) {
        p = 0;
    }
    return static_cast<int>(p / 100.0 * base);
}

void TacticalAi::consider(Candidate& best, Candidate c, Point shooterPos) const {
    c.found = true;
    if (best.found) {
        if (best.score > c.score + 1) return;
        if (best.score > c.score - 1 && !same(best.target, c.target)) {
            const bool closer = std::abs(best.target.x - shooterPos.x) < std::abs(c.target.x - shooterPos.x) ||
                                std::abs(best.target.y - shooterPos.y) < std::abs(c.target.y - shooterPos.y);
            if (closer) return;
        }
    }
    best = std::move(c);
}

TacticalAi::Candidate TacticalAi::evaluateUnits(int shooter, const PathMap& map, const std::vector<int>& targets) {
    Candidate best;
    const BattleUnit& s = state_.unit(shooter);
    if (!s.type->hasWeapon) return best;
    const int range = s.type->range;
    const int minRange = std::max(1, s.type->minRange);
    std::vector<int> rings{range};
    if (range > 1) rings.push_back(minRange);
    rings.push_back((range + minRange) / 2);
    for (const int t : targets) {
        const BattleUnit& x = state_.unit(t);
        if (!x.alive) continue;
        const int x0 = x.pos.x, y0 = x.pos.y, x1 = x.pos.x + x.type->sizeX - 1, y1 = x.pos.y + x.type->sizeY - 1;
        auto tryTile = [&](Point f) {
            if (!state_.inside(f)) return;
            // The shooter's own tile counts as a firing position with no
            // steps *(OpenDay: lets a unit in range fire without moving)*.
            const bool here = same(f, s.pos);
            if (!here && !map.reachable(f)) return;
            if (!legalFrom(shooter, f, x.pos)) return;
            Candidate c;
            c.target = x.pos;
            c.unit = t;
            c.firing = f;
            if (!here) c.path = map.pathTo(f);
            c.score = score(shooter, t, -1, static_cast<int>(c.path.size()), 500);
            consider(best, std::move(c), s.pos);
        };
        for (const int d : rings) {
            const int l = x0 - d, tp = y0 - d, r = x1 + d, b = y1 + d;
            for (int xx = l; xx <= r; ++xx) tryTile({xx, tp});
            for (int yy = tp; yy <= b; ++yy) tryTile({r, yy});
            for (int xx = r; xx >= l; --xx) tryTile({xx, b});
            for (int yy = b; yy >= tp; --yy) tryTile({l, yy});
        }
    }
    return best;
}

TacticalAi::Candidate TacticalAi::evaluateBuilding(int shooter, const PathMap& map, int building) {
    Candidate best;
    const BattleUnit& s = state_.unit(shooter);
    const BattleBuilding& b = state_.building(building);
    if (!s.type->hasWeapon || !b.alive) return best;
    const int l = b.pos.x, t = b.pos.y, r = l + b.type->sizeX, btm = t + b.type->sizeY;
    // Two of the four tiles have x and y swapped, as in the original (3.2).
    const Point tiles[4] = {{l - 1, t - 1}, {r + 1, btm + 1}, {t + 1, r - 1}, {btm - 1, l + 1}};
    for (const Point f : tiles) {
        if (!state_.inside(f)) continue;
        const bool here = same(f, s.pos);
        if (!here && !map.reachable(f)) continue;
        if (!legalFrom(shooter, f, b.pos)) continue;
        Candidate c;
        c.target = b.pos;
        c.building = building;
        c.firing = f;
        if (!here) c.path = map.pathTo(f);
        c.score = score(shooter, -1, building, static_cast<int>(c.path.size()), 1000);
        consider(best, std::move(c), s.pos);
    }
    return best;
}

bool TacticalAi::walkTo(int unit, const std::vector<PathStep>& path, std::size_t steps) {
    if (steps == 0) return true;
    const std::vector<PathStep> part(path.begin(), path.begin() + static_cast<std::ptrdiff_t>(steps));
    server_.walk(unit, part);
    server_.checkEnd();
    const BattleUnit& u = state_.unit(unit);
    return u.alive && same(u.pos, part.back().pos);
}

TacticalAi::Moved TacticalAi::moveToFire(int shooter, const Candidate& c) {
    const BattleUnit& s = state_.unit(shooter);
    const int ap = s.availableAp();
    const int shotAp = s.type->shotAp;
    // 0. Already able to fire.
    if (checkShot(state_, shooter, c.target, server_.options_.coop).ok()) return Moved::Arrived;
    const std::size_t cut = static_cast<std::size_t>(affordableSteps(c.path, ap));
    if (cut == 0) return Moved::Nothing;
    const std::vector<PathStep>& p = c.path;

    std::size_t end = 0;
    // 1. The first firing spot on the way that leaves more than one shot.
    int cost = 0;
    for (std::size_t i = 0; i < cut && end == 0; ++i) {
        cost += p[i].cost;
        if (p[i].enterable && ap - cost > shotAp && legalFrom(shooter, p[i].pos, c.target)) end = i + 1;
    }
    // 2. Stop short of the target's reach.
    if (end == 0 && c.unit >= 0 && canFireNow(state_.unit(c.unit)) && !targetCanHit(c.unit, shooter, s.pos)) {
        for (std::size_t i = 0; i < cut; ++i) {
            if (!targetCanHit(c.unit, shooter, p[i].pos)) continue;
            for (std::size_t j = i; j > 0 && end == 0; --j)
                if (p[j - 1].enterable) end = j;
            break;
        }
    }
    // 3. Advance while a shot remains (ends on the first tile below one shot).
    if (end == 0 && s.type->canOverwatch && ap >= shotAp) {
        cost = 0;
        for (std::size_t i = 0; i < cut && end == 0; ++i) {
            cost += p[i].cost;
            if (p[i].enterable && ap - cost < shotAp) end = i + 1;
        }
        if (end == 0) end = cut;
    }
    // 4. Plain approach.
    if (end == 0) end = cut;
    while (end > 0 && !p[end - 1].enterable) --end;
    if (end == 0) return Moved::Nothing;
    return walkTo(shooter, p, end) ? Moved::Arrived : Moved::Interrupted;
}

bool TacticalAi::targetExists(const Candidate& c) const {
    if (c.unit >= 0) return state_.unit(c.unit).alive;
    if (c.building >= 0) return state_.building(c.building).alive;
    return false;
}

bool TacticalAi::fireAtTarget(int shooter, const Candidate& c) {
    if (!targetExists(c) || !state_.unit(shooter).alive) return false;
    if (!server_.fireAt(shooter, c.target, true)) return false;
    server_.checkEnd();
    return !targetExists(c);
}

void TacticalAi::setOrder(int unit, ScriptedOrder order) { server_.emit(OrderChanged{unit, std::move(order)}); }

bool TacticalAi::findObject(int id, int& unit, int& building) const {
    unit = building = -1;
    for (int i = 0; i < static_cast<int>(state_.units().size()); ++i)
        if (state_.unit(i).alive && state_.unit(i).id == id) {
            unit = i;
            return true;
        }
    for (int i = 0; i < static_cast<int>(state_.buildings().size()); ++i)
        if (state_.building(i).alive && state_.building(i).id == id) {
            building = i;
            return true;
        }
    return false;
}

// --- Actions -------------------------------------------------------------

void TacticalAi::stationaryFire(int unit) {
    for (int budget = kStepBudget; budget >= kCycle && !over() && state_.unit(unit).alive; budget -= kCycle) {
        const BattleUnit& s = state_.unit(unit);
        Candidate best;
        for (const int t : enemyUnits()) {
            const BattleUnit& x = state_.unit(t);
            if (!checkShot(state_, unit, x.pos, server_.options_.coop).ok()) continue;
            Candidate c;
            c.target = x.pos;
            c.unit = t;
            c.firing = s.pos;
            c.score = score(unit, t, -1, 0, canFireNow(x) ? 1000 : 500);
            consider(best, std::move(c), s.pos);
        }
        if (!best.found || !fireAtTarget(unit, best)) return;
    }
}

void TacticalAi::moveToCenter(int unit, const PathMap& map) {
    const int n = state_.size();
    const int c0 = std::max(0, (n - 12) / 2), c1 = std::min(n, c0 + 12);
    for (int x = c0; x < c1; ++x)
        for (int y = c0; y < c1; ++y) {
            if (!map.reachable({x, y})) continue;
            const std::vector<PathStep> path = map.pathTo({x, y});
            std::size_t cut = static_cast<std::size_t>(affordableSteps(path, state_.unit(unit).availableAp()));
            while (cut > 0 && !path[cut - 1].enterable) --cut;
            walkTo(unit, path, cut);
            return;
        }
}

void TacticalAi::hunt(int unit, int& budget) {
    while (budget >= kCycle && !over() && state_.unit(unit).alive) {
        budget -= kCycle;
        if (state_.unit(unit).availableAp() < 4) return;
        const PathMap map(state_, unit);
        const Candidate best = evaluateUnits(unit, map, enemyUnits());
        if (!best.found) {
            // A counter-attack calls the transport instead (6.4).
            if (state_.counterAttack) callTransport(unit);
            else moveToCenter(unit, map);
            return;
        }
        const Moved m = moveToFire(unit, best);
        if (m == Moved::Nothing) return;
        if (m == Moved::Interrupted) continue;
        if (!fireAtTarget(unit, best)) return;
    }
}

void TacticalAi::escort(int unit, bool defend) {
    for (int budget = kStepBudget; budget >= kCycle && !over() && state_.unit(unit).alive;) {
        budget -= kCycle;
        const BattleUnit& u = state_.unit(unit);
        if (u.availableAp() < 4) return;
        if (u.order.ids.empty()) return setOrder(unit, {});
        int ou = -1, ob = -1;
        if (!findObject(u.order.ids.front(), ou, ob)) {
            ScriptedOrder o = u.order;
            o.ids.erase(o.ids.begin());
            setOrder(unit, o);
            continue;
        }
        Point anchor;
        int sx = 1, sy = 1;
        if (ou >= 0) {
            anchor = state_.unit(ou).pos;
            sx = state_.unit(ou).type->sizeX;
            sy = state_.unit(ou).type->sizeY;
        } else {
            anchor = state_.building(ob).pos;
            sx = state_.building(ob).type->sizeX;
            sy = state_.building(ob).type->sizeY;
        }
        // The area is not symmetric in the original (2.4, 2.5).
        const int near = defend ? 2 : 3, far = defend ? 3 : 4;
        auto inArea = [&](Point p) {
            return p.x >= anchor.x - near && p.y >= anchor.y - near && p.x <= anchor.x + sx - 1 + far &&
                   p.y <= anchor.y + sy - 1 + far;
        };
        if (inArea(u.pos)) {
            if (!defend) hunt(unit, budget);
            return;
        }
        const PathMap map(state_, unit);
        static constexpr Point kStep[8] = {{0, 1}, {-1, 1}, {-1, 0}, {-1, -1}, {0, -1}, {1, -1}, {1, 0}, {1, 1}};
        std::vector<PathStep> path;
        for (const Point d : kStep) {
            const Point t{anchor.x + d.x, anchor.y + d.y};
            if (!map.reachable(t)) continue;
            std::vector<PathStep> p = map.pathTo(t);
            if (path.empty() || p.size() < path.size()) path = std::move(p);
        }
        if (path.empty()) return;
        const std::size_t cut = static_cast<std::size_t>(affordableSteps(path, u.availableAp()));
        std::size_t end = 0;
        for (std::size_t i = 0; i < cut && end == 0; ++i)
            if (path[i].enterable && inArea(path[i].pos)) end = i + 1;
        if (end == 0) return;
        if (walkTo(unit, path, end)) return;
    }
}

void TacticalAi::patrol(int unit) {
    for (int budget = kStepBudget; budget >= kCycle && !over() && state_.unit(unit).alive;) {
        budget -= kCycle;
        const BattleUnit& u = state_.unit(unit);
        if (u.availableAp() <= 3) return;
        const std::vector<Point>& route = u.order.positions;
        if (route.empty()) return;
        const PathMap map(state_, unit);
        if (!contains(route, u.pos)) {
            std::vector<PathStep> path;
            for (const Point r : route) {
                if (!map.reachable(r)) continue;
                std::vector<PathStep> p = map.pathTo(r);
                if (path.empty() || p.size() < path.size()) path = std::move(p);
            }
            if (path.empty()) return;
            std::size_t cut = static_cast<std::size_t>(affordableSteps(path, u.availableAp()));
            while (cut > 0 && !path[cut - 1].enterable) --cut;
            if (cut == 0) return;
            walkTo(unit, path, cut);
            continue;
        }
        Candidate best = evaluateUnits(unit, map, enemyUnits());
        if (!best.found) return; // patrols do not walk their route by themselves
        if (!best.path.empty()) {
            if (!contains(route, best.path.front().pos)) return;
            // Up to and including the first stopping tile off the route.
            for (std::size_t i = 0; i < best.path.size(); ++i)
                if (best.path[i].enterable && !contains(route, best.path[i].pos)) {
                    best.path.resize(i + 1);
                    break;
                }
        }
        const Moved m = moveToFire(unit, best);
        if (m == Moved::Nothing) return;
        if (m == Moved::Interrupted) continue;
        if (!fireAtTarget(unit, best)) return;
    }
}

void TacticalAi::attack(int unit) {
    for (int budget = kStepBudget; budget >= kCycle && !over() && state_.unit(unit).alive;) {
        budget -= kCycle;
        const BattleUnit& u = state_.unit(unit);
        if (u.availableAp() <= 3) return;
        if (u.order.ids.empty()) return setOrder(unit, {});
        int tu = -1, tb = -1;
        if (!findObject(u.order.ids.front(), tu, tb)) {
            ScriptedOrder o = u.order;
            o.ids.erase(o.ids.begin());
            setOrder(unit, o);
            continue;
        }
        const PathMap map(state_, unit);
        const Candidate best = tu >= 0 ? evaluateUnits(unit, map, {tu}) : evaluateBuilding(unit, map, tb);
        if (!best.found) return;
        const Moved m = moveToFire(unit, best);
        if (m == Moved::Nothing) return;
        if (m == Moved::Interrupted) continue;
        if (!fireAtTarget(unit, best)) return;
        ScriptedOrder o = state_.unit(unit).order; // target destroyed: next id
        if (!o.ids.empty()) o.ids.erase(o.ids.begin());
        setOrder(unit, o);
    }
}

void TacticalAi::gotoGroup(const std::vector<int>& units, bool fast) {
    std::vector<int> group;
    for (const int i : units) {
        const BattleUnit& u = state_.unit(i);
        if (!u.alive) continue;
        if (u.order.positions.empty()) {
            setOrder(i, {});
            continue;
        }
        const Point dest = u.order.positions.front();
        if (same(u.pos, dest)) {
            ScriptedOrder o = u.order;
            o.positions.erase(o.positions.begin());
            if (o.positions.empty()) o = {};
            setOrder(i, o);
        } else if (u.availableAp() > 3) {
            group.push_back(i);
        }
    }
    PlanContext context;
    for (const int i : group) {
        if (over()) return;
        const BattleUnit& u = state_.unit(i);
        if (!u.alive) continue;
        Point dest;
        if (!groupTile(state_, u, u.order.positions.front(), context, dest)) continue;
        const std::vector<PathStep> path = findPath(state_, i, dest, &context);
        const int reserve = !fast && u.type->canOverwatch ? u.type->shotAp : 0;
        std::size_t cut = static_cast<std::size_t>(affordableSteps(path, std::max(0, u.availableAp() - reserve)));
        while (cut > 0 && !path[cut - 1].enterable) --cut;
        if (cut == 0) continue;
        context.vacated.push_back(u.pos);
        context.claimed.push_back(dest);
        walkTo(i, path, cut);
    }
    for (const int i : group) {
        const BattleUnit& u = state_.unit(i);
        if (!u.alive || u.order.positions.empty()) continue;
        const Point w = u.order.positions.front();
        if (std::abs(u.pos.x - w.x) <= 1 && std::abs(u.pos.y - w.y) <= 1) {
            ScriptedOrder o = u.order;
            o.positions.erase(o.positions.begin());
            if (o.positions.empty()) o = {};
            setOrder(i, o);
        }
    }
}

// --- The turn --------------------------------------------------------------

// --- Automated units (section 6) ---------------------------------------------

int TacticalAi::draw(int n) { return n > 0 ? server_.random(n) : 0; }

bool TacticalAi::callAutomated(int typeId, Point p1, Point p2) {
    const auto it = state_.rules().automated.find(typeId);
    if (it == state_.rules().automated.end()) return false;
    const AutomatedType& t = it->second;
    // Human types other than the bombers and the nuke are dropped.
    if (t.race == 0 && typeId != 4 && typeId != 5 && typeId != 6) return false;
    server_.callAutomated(t, 0, p1, p2);
    return true;
}

bool TacticalAi::dropTarget(Point& target) {
    // From a random position of the map's unit list, the first enemy unit
    // further down (no wrap-around) with armor <= 30, grav or ground.
    std::vector<int> onMap;
    for (int i = 0; i < static_cast<int>(state_.units().size()); ++i)
        if (state_.unit(i).alive) onMap.push_back(i);
    const int start = draw(static_cast<int>(onMap.size()));
    for (std::size_t k = static_cast<std::size_t>(start); k < onMap.size(); ++k) {
        const BattleUnit& u = state_.unit(onMap[k]);
        if (isEnemy(u) && u.armor <= 30 && (u.type->category == 0 || u.type->category == 1)) {
            target = u.pos;
            return true;
        }
    }
    return false;
}

void TacticalAi::randomCall() {
    const BattleState::AiSupport& ai = state_.aiSupport;
    if (!ai.present || over()) return;
    const int chance = state_.counterAttack ? 25 : ai.chance;
    if (draw(100) >= chance || ai.types.empty()) return;
    const int id = ai.types[static_cast<std::size_t>(draw(static_cast<int>(ai.types.size())))];
    const auto it = state_.rules().automated.find(id);
    if (it == state_.rules().automated.end()) return;
    const AutomatedType& t = it->second;
    if (!state_.counterAttack) {
        const bool owned = std::any_of(state_.buildings().begin(), state_.buildings().end(), [&](const BattleBuilding& b) {
            return b.alive && b.owner == 0 && b.type->id == t.prerequisite;
        });
        if (!owned) return;
    }
    Point target{-1, -1};
    bool found = false;
    const int count = static_cast<int>(state_.units().size());
    switch (id) {
    case 9: // mine detection, counter-attacks only
        if (!state_.counterAttack) return;
        found = true;
        if (draw(100) < 50) {
            target = {0, 0};
            for (const BattleMine& m : state_.mines())
                if (m.alive && !m.hidden[0]) {
                    target = m.pos;
                    break;
                }
        } else {
            const int x = draw(state_.size());
            target = {x, draw(state_.size())};
        }
        break;
    case 10: { // medic: the most damaged alien mobile unit
        int best = 0;
        for (int i = 0; i < count; ++i) {
            const BattleUnit& u = state_.unit(i);
            if (!u.alive || u.owner != 0 || !isAlien(u) || !isMobile(u)) continue;
            if (u.type->armor - u.armor > best) {
                best = u.type->armor - u.armor;
                target = u.pos;
                found = true;
            }
        }
        break;
    }
    case 11:
    case 12:
    case 13: { // fighters, gene bomb: the enemy unit with the most armor
        int best = 0;
        for (int i = 0; i < count; ++i) {
            const BattleUnit& u = state_.unit(i);
            if (isEnemy(u) && u.armor > best) {
                best = u.armor;
                target = u.pos;
                found = true;
            }
        }
        break;
    }
    case 14:
    case 19:
    case 20:
    case 21:
    case 22:
    case 23:
    case 24:
    case 25: found = dropTarget(target); break;
    default: return; // transports and bridges are not called this way
    }
    if (!found || t.cost > server_.aiAutomatedBudget_) return;
    server_.aiAutomatedBudget_ -= t.cost;
    callAutomated(id, target, target);
}

void TacticalAi::emergencyDrop() {
    if (draw(100) >= 67 || state_.turn >= 5 || over()) return;
    // Trooper, Grenadier, Assault Trooper, LAV, Light Tank (by their
    // UNIT TYPE ID LOOKUP symbols).
    static constexpr int kTypes[5] = {2, 56, 58, 8, 9};
    std::vector<int> onMap;
    for (int i = 0; i < static_cast<int>(state_.units().size()); ++i)
        if (state_.unit(i).alive) onMap.push_back(i);
    int enemies = 0;
    for (const int i : onMap)
        if (isEnemy(state_.unit(i))) ++enemies;
    // The original's count is one too high when the list does not end with
    // an enemy unit (6.3); kept.
    if (onMap.empty() || !isEnemy(state_.unit(onMap.back()))) ++enemies;
    const int calls = state_.oneOffDeployment > 1200 ? 3 : state_.oneOffDeployment > 700 ? 2 : state_.oneOffDeployment > 300 ? 1 : 0;
    if (enemies <= 30 || calls == 0 || state_.rules().automated.size() < 20) return;
    const AutomatedType& drop = std::next(state_.rules().automated.begin(), 19)->second;
    for (const int type : kTypes) {
        int ofType = 0;
        for (const int i : onMap)
            if (isEnemy(state_.unit(i)) && state_.unit(i).type->typeId == type) ++ofType;
        if (ofType * 10 <= enemies * 6) continue; // share above 60 %
        for (int c = 0; c < calls && !over(); ++c) {
            // At no net cost: the budget is raised by the cost first.
            server_.aiAutomatedBudget_ += drop.cost;
            Point target;
            if (!dropTarget(target) || drop.cost > server_.aiAutomatedBudget_) continue;
            server_.aiAutomatedBudget_ -= drop.cost;
            callAutomated(drop.id, target, target);
        }
    }
}

void TacticalAi::scheduledCalls(int turn) {
    auto& rows = state_.scheduledAutomated;
    for (std::size_t i = 0; i < rows.size() && !over();) {
        if (rows[i].turn != turn) {
            ++i;
            continue;
        }
        const BattleState::ScheduledAutomated row = rows[i];
        rows.erase(rows.begin() + static_cast<std::ptrdiff_t>(i));
        callAutomated(row.type, row.start, row.dest);
    }
}

void TacticalAi::callTransport(int unit) {
    const BattleState::AiSupport& ai = state_.aiSupport;
    const BattleUnit& u = state_.unit(unit);
    if (!ai.present || u.availableAp() < 10 || u.type->movement == 4 || u.type->movement == 6) return;
    if (std::find(ai.types.begin(), ai.types.end(), 7) == ai.types.end()) return;
    const auto it = state_.rules().automated.find(7);
    if (it == state_.rules().automated.end() || it->second.cost > server_.aiAutomatedBudget_) return;
    server_.aiAutomatedBudget_ -= it->second.cost; // even if no tile is found
    BattleUnit probe;
    probe.type = u.type;
    probe.owner = 0;
    const Point from = u.pos;
    for (const int e : enemyUnits()) {
        const BattleUnit& enemy = state_.unit(e);
        std::vector<Point> tiles;
        for (int y = enemy.pos.y - 10; y <= enemy.pos.y + enemy.type->sizeY - 1 + 10; ++y)
            for (int x = enemy.pos.x - 10; x <= enemy.pos.x + enemy.type->sizeX - 1 + 10; ++x) tiles.push_back({x, y});
        for (std::size_t i = 0; i < tiles.size(); ++i)
            std::swap(tiles[i], tiles[static_cast<std::size_t>(draw(static_cast<int>(tiles.size())))]);
        for (const Point p : tiles)
            if (evaluateTile(state_, probe, p).enterable) {
                callAutomated(7, from, p);
                return;
            }
    }
}

void TacticalAi::playTurn() {
    const int count = static_cast<int>(state_.units().size());
    auto ai = [&](int i) { return state_.unit(i).alive && state_.unit(i).owner == 0; };

    // Phase 0: order expiry (the Probability roll is made by the server at
    // the start of the turn), then the order list.
    for (int i = 0; i < count; ++i)
        if (ai(i) && isMobile(state_.unit(i)) && state_.unit(i).order.order != 0 &&
            state_.unit(i).order.turn < state_.turn)
            setOrder(i, {});
    std::vector<int> ordered;
    for (int i = 0; i < count; ++i)
        if (ai(i) && isMobile(state_.unit(i)) && state_.unit(i).order.order != 0 && state_.unit(i).availableAp() > 4)
            ordered.push_back(i);
    ordered = insertionSort(ordered, [&](int a, int b) { return state_.unit(a).order.order < state_.unit(b).order.order; });
    const int turn = state_.turn;

    // Phase 2: a random automated unit; phase 3: the emergency drop.
    randomCall();
    emergencyDrop();
    if (over()) return;

    // Phases 4-5: stationary fire, before anything moves.
    std::vector<int> stationary;
    for (int i = 0; i < count; ++i)
        if (ai(i) && isMobile(state_.unit(i)) && isAlien(state_.unit(i)) && state_.unit(i).availableAp() > 4)
            stationary.push_back(i);
    for (int i = 0; i < count; ++i)
        if (ai(i) && !isMobile(state_.unit(i))) stationary.push_back(i);
    stationary = insertionSort(stationary, [&](int a, int b) {
        const BattleUnit &x = state_.unit(a), &y = state_.unit(b);
        if (!isMobile(x)) return true;
        if (!isMobile(y)) return false;
        if (x.type->apDamage) return true;
        if (y.type->apDamage) return false;
        return x.type->jammer > 0 && x.type->range < y.type->range;
    });
    for (const int i : stationary) {
        if (over()) return;
        if (state_.unit(i).alive) stationaryFire(i);
    }

    // Phase 6: the scenario's scheduled automated units; phase 7: another
    // random call.
    scheduledCalls(turn);
    randomCall();
    if (over()) return;

    // Phases 8-9: units without orders hunt.
    std::vector<int> free;
    for (int i = 0; i < count; ++i)
        if (ai(i) && isMobile(state_.unit(i)) && isAlien(state_.unit(i)) && state_.unit(i).order.order == 0)
            free.push_back(i);
    free = insertionSort(free, [&](int a, int b) {
        const BattleUnit &x = state_.unit(a), &y = state_.unit(b);
        if (x.type->apDamage) return true;
        if (y.type->apDamage) return false;
        return x.type->range > y.type->range; // only the range decides (1.3)
    });
    if (!enemyUnits().empty() || state_.counterAttack)
        for (const int i : free) {
            if (over()) return;
            int budget = kStepBudget;
            if (state_.unit(i).alive) hunt(i, budget);
        }

    // Phases 10-12: scripted orders.
    std::vector<int> gotoUnits, fastUnits;
    for (const int i : ordered) {
        if (state_.unit(i).order.order == 1) gotoUnits.push_back(i);
        if (state_.unit(i).order.order == 2) fastUnits.push_back(i);
    }
    if (!gotoUnits.empty() && !over()) gotoGroup(gotoUnits, false);
    if (!fastUnits.empty() && !over()) gotoGroup(fastUnits, true);
    for (const int i : ordered) {
        if (over()) return;
        if (!state_.unit(i).alive) continue;
        switch (state_.unit(i).order.order) {
        case 3: escort(i, false); break;
        case 4: escort(i, true); break;
        case 5: patrol(i); break;
        case 6: attack(i); break;
        default: break;
        }
    }
}

} // namespace openday::game
