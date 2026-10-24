// The server side of automated units: request checks, anti-aircraft fire
// and the effect of each category. docs/mechanics/automated-units.md 4-6.
#include <algorithm>

#include "battle/automated.hpp"
#include "battle/server.hpp"

namespace openday::game {
namespace {

// value x (armor - acc) / armor, truncated.
int scaled(int value, const AutomatedType& t, int acc) {
    return t.armor > 0 ? static_cast<int>(static_cast<long long>(value) * (t.armor - acc) / t.armor) : value;
}

} // namespace

void BattleServer::handleAutomated(const AutomatedRequest& r) {
    // The original takes the current player as the owner whoever sent the
    // request (open question 2); OpenDay refuses requests outside the turn.
    if (r.player != state_.currentPlayer) return emit(RequestRejected{"not your turn"});
    const auto it = state_.rules().automated.find(r.typeId);
    const std::vector<int> usable = state_.usableAutomated(r.player);
    if (it == state_.rules().automated.end() || std::find(usable.begin(), usable.end(), r.typeId) == usable.end())
        return emit(RequestRejected{"not available"});
    const AutomatedType& type = it->second;
    if (r.player < 1 || r.player >= kPlayerSlots) return emit(RequestRejected{"invalid player"});
    const int budget = state_.deployBudget[static_cast<std::size_t>(r.player)];
    if (budget < type.cost) return emit(RequestRejected{"not enough tactical resource"});
    // The original does not repeat the client's target tests (4, step 3).
    if (static_cast<int>(r.points.size()) < automatedPoints(type)) return emit(RequestRejected{"invalid target"});
    const Point p1 = r.points[0];
    const Point p2 = r.points.size() > 1 ? r.points[1] : p1;
    const bool transport = type.kind() == AutomatedCategory::Transport;
    if (!automatedTargetOk(state_, r.player, type, p1, options_.coop) ||
        (transport && !automatedTargetOk(state_, r.player, type, p2, options_.coop, &p1)))
        return emit(RequestRejected{"invalid target"});
    callAutomated(type, r.player, p1, p2);
    // The cost is paid whatever happened (4, step 6).
    emit(DeployBudget{r.player, budget - type.cost});
}

void BattleServer::callAutomated(const AutomatedType& type, int owner, Point p1, Point p2) {
    AutomatedCall call;
    call.type = &type;
    call.owner = owner;
    const std::size_t before = out_.size();
    switch (type.kind()) {
    case AutomatedCategory::Destroy: automatedDestroy(call, p1); break;
    case AutomatedCategory::Repair: automatedRepair(call, p1); break;
    case AutomatedCategory::Build: automatedBridge(call, p1); break;
    case AutomatedCategory::Mine: automatedMines(call, p1); break;
    case AutomatedCategory::Drop: automatedDrop(call, p1); break;
    case AutomatedCategory::Transport: automatedTransport(call, p1, p2); break;
    default: return; // unknown category: nothing happens
    }
    if (out_.size() > before) updateTech();
    if (checkEnd()) return;
    if (owner != 0 && type.deployOnce) emit(AutomatedUsed{options_.faithfulAutomated ? -1 : owner, type.id});
}

bool BattleServer::antiAir(AutomatedCall& call, Point tile) {
    const AutomatedType& t = *call.type;
    // The original keeps the candidates of a transport's first pass (5.3).
    if (!options_.faithfulAutomated) call.candidates.clear();
    auto avgDamage = [&](int u) {
        const UnitType& ut = *state_.unit(u).type;
        return (ut.damageBase + ut.damageBase + ut.damageRandom) / 2;
    };
    // 1-2. Candidates, inserted ascending by average damage, then shot cost;
    // equal keys keep map order.
    for (int i = 0; i < static_cast<int>(state_.units().size()); ++i) {
        const BattleUnit& u = state_.unit(i);
        if (!u.alive || !state_.inside(u.pos) || !u.type->isAA || !u.type->canOverwatch ||
            u.availableAp() < u.type->shotAp || playerUnitSide(call.owner, u, options_.coop) != Side::Enemy)
            continue;
        const int range = u.type->range;
        if (tile.x < u.pos.x - range || tile.y < u.pos.y - range || tile.x > u.pos.x + u.type->sizeX - 1 + range ||
            tile.y > u.pos.y + u.type->sizeY - 1 + range)
            continue;
        const auto at = std::find_if(call.candidates.begin(), call.candidates.end(), [&](int c) {
            const int a = avgDamage(i), b = avgDamage(c);
            return a < b || (a == b && u.type->shotAp < state_.unit(c).type->shotAp);
        });
        call.candidates.insert(at, i);
    }
    // 3. No hit roll: the candidates fire in order until the armor is used up.
    for (std::size_t k = 0; k < call.candidates.size(); ++k) {
        if (call.acc >= t.armor) {
            call.candidates.resize(k);
            break;
        }
        call.acc += state_.unit(call.candidates[k]).type->damageAA;
    }
    const AutomatedOutcome outcome = call.acc == 0          ? AutomatedOutcome::Ok
                                     : call.acc < t.armor ? AutomatedOutcome::Damaged
                                                          : AutomatedOutcome::Destroyed;
    // 4-5.
    emit(AutomatedArrived{t.id, call.owner, static_cast<int>(outcome), tile, call.passes > 0});
    ++call.passes;
    if (t.armor > 0)
        for (const int c : call.candidates) {
            const BattleUnit& u = state_.unit(c);
            emit(UnitFiredAA{c, std::max(0, u.ap - u.type->shotAp)});
        }
    return outcome != AutomatedOutcome::Destroyed;
}

void BattleServer::automatedDestroy(AutomatedCall& call, Point target) {
    const AutomatedType& t = *call.type;
    if (!antiAir(call, target)) return;
    // 6.1: area damage of a shot with lo = hi = base: no hit roll, no
    // scatter, no difficulty scaling; one random number is still drawn.
    const int base = scaled(t.damage, t, call.acc);
    cause_ = {call.owner, t.id};
    const int damage = rollDamage(base, 0, false);
    damageArea(target, damage, std::max(1, t.damageArea), !t.armorDamage);
}

void BattleServer::automatedRepair(AutomatedCall& call, Point target) {
    const AutomatedType& t = *call.type;
    if (!antiAir(call, target)) return;
    const int a = std::max(1, t.damageArea), h = (a + 1) / 2;
    const double f = t.armor > 0 ? 1.0 - static_cast<double>(call.acc) / t.armor : 1.0;
    for (int x = target.x - a / 2; x < target.x - a / 2 + a; ++x)
        for (int y = target.y - a / 2; y < target.y - a / 2 + a; ++y) {
            const Point p{x, y};
            if (!state_.inside(p)) continue;
            const int d = chebyshev(target, p);
            auto amount = [&](int v) { return static_cast<int>((v - (v / h) * d) * f); };
            Repaired e;
            e.tile = p;
            int reported = 0, newAp = 0;
            // Any owner, enemies included (6.2).
            const int b = state_.buildingAt(p), u = state_.unitAt(p), w = state_.wallAt(p);
            if (t.armorInc > 0) {
                if (b >= 0) {
                    const BattleBuilding& bd = state_.building(b);
                    e.building = b;
                    e.buildingArmor = reported = std::min(bd.type->armor, bd.armor + amount(t.armorInc));
                }
                if (u >= 0) {
                    const BattleUnit& un = state_.unit(u);
                    e.unit = u;
                    e.unitArmor = reported = std::min(un.type->armor, un.armor + amount(t.armorInc));
                    e.unitAp = un.ap;
                }
                if (w >= 0) {
                    const BattleWall& wl = state_.wall(w);
                    const auto wt = state_.rules().walls.find(wl.type);
                    const int max = wt == state_.rules().walls.end() ? wl.armor : wt->second.armor;
                    e.wall = w;
                    e.wallArmor = reported = std::min(max, wl.armor + amount(t.armorInc));
                }
            }
            if (t.actionInc > 0 && u >= 0) {
                const BattleUnit& un = state_.unit(u);
                if (e.unit < 0) {
                    e.unit = u;
                    e.unitArmor = un.armor;
                }
                e.unitAp = newAp = std::min(un.type->actionPoints, un.ap + amount(t.actionInc));
            }
            if (reported > 0 || newAp > 0) emit(std::move(e));
        }
}

void BattleServer::automatedBridge(AutomatedCall& call, Point target) {
    if (!antiAir(call, target)) return;
    // A damaged builder still builds. The original does not re-check the
    // tile here (6.3); OpenDay skips an invalid one.
    if (!bridgePlacementOk(state_, target) || !state_.rules().roads.count(1)) return;
    emit(BridgeBuilt{1, call.owner, target});
}

void BattleServer::automatedMines(AutomatedCall& call, Point target) {
    const AutomatedType& t = *call.type;
    if (!antiAir(call, target)) return;
    const int r = scaled(t.mineDetectionRange, t, call.acc);
    const int n = state_.size();
    // The upper bounds are exclusive (6.4): a 2r x 2r square.
    const int x0 = std::max(0, target.x - r), x1 = std::min(n, target.x + r);
    const int y0 = std::max(0, target.y - r), y1 = std::min(n, target.y + r);
    std::vector<int> found;
    for (int i = 0; i < static_cast<int>(state_.mines().size()); ++i) {
        const BattleMine& m = state_.mine(i);
        if (m.alive && m.pos.x >= x0 && m.pos.x < x1 && m.pos.y >= y0 && m.pos.y < y1) found.push_back(i);
    }
    if (found.empty()) return;
    // The computer player clears hidden_AI; a human clears all four human flags.
    if (call.owner == 0) emit(MinesRevealed{0, found});
    else
        for (int p = 1; p < kPlayerSlots; ++p) emit(MinesRevealed{p, found});
}

void BattleServer::automatedDrop(AutomatedCall& call, Point target) {
    const AutomatedType& t = *call.type;
    if (!antiAir(call, target)) return;
    // unitTLDrop is not read: the computer player drops level 0, a human
    // his own level of the type (6.5).
    const int level = call.owner == 0 ? 0 : state_.techLevel(call.owner, t.dropUnit);
    const UnitType* type = state_.unitType(t.dropUnit, level);
    if (!type) type = state_.unitType(t.dropUnit, -1);
    if (!type) return;
    int n = t.dropCount;
    if (call.acc > 0) n /= 2;
    n = std::clamp(n, 0, static_cast<int>(t.dropSlots.size()));
    int nextId = 1;
    for (const BattleUnit& u : state_.units()) nextId = std::max(nextId, u.id + 1);
    for (int i = 0; i < n && !state_.over; ++i) {
        const DropSlot& slot = t.dropSlots[static_cast<std::size_t>(i)];
        const Point p{target.x + slot.dx, target.y + slot.dy};
        if (!state_.inside(p)) continue; // a failed slot: nothing is placed
        // An armed mine on the slot goes off and the slot fails.
        if (const int m = state_.mineAt(p); m >= 0 && state_.mine(m).armed) {
            explodeMine(m, call.owner);
            continue;
        }
        BattleUnit probe;
        probe.type = type;
        probe.owner = call.owner;
        if (!evaluateTile(state_, probe, p).enterable) continue;
        // Dropped units can act at once.
        emit(UnitAdded{nextId++, t.dropUnit, type->techLevel, call.owner, p, type->hasFacings ? (slot.facing & 7) : 0, {},
                       false});
    }
}

void BattleServer::automatedTransport(AutomatedCall& call, Point source, Point destination) {
    const AutomatedType& t = *call.type;
    const int unit = state_.unitAt(source); // any owner (6.6)
    if (unit < 0) return;
    if (!antiAir(call, source)) return; // shot down: the unit stays where it is
    {
        const BattleUnit& u = state_.unit(unit);
        emit(UnitTransported{unit, true, u.ap, u.armor, {-1, -1}});
    }
    const bool arrived = antiAir(call, destination);
    const BattleUnit& u = state_.unit(unit);
    if (arrived && evaluateTile(state_, u, destination).enterable) {
        const double f = t.armor > 0 ? 1.0 - static_cast<double>(call.acc) / t.armor : 1.0;
        const int armor = std::max(1, static_cast<int>(u.armor * f));
        // The unit spends all its available AP.
        emit(UnitTransported{unit, false, std::min(u.ap, u.negAp), armor, destination});
        if (const int m = state_.mineAt(destination); m >= 0 && state_.mine(m).armed) explodeMine(m, u.owner);
    } else {
        // Lost with the transport; not a kill (6.6 step 7).
        emit(UnitTransported{unit, false, 0, 0, {-1, -1}});
    }
}

} // namespace openday::game
