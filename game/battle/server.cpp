#include "battle/server.hpp"

#include <algorithm>
#include <cstdlib>

#include "anim/unit_animation.hpp"
#include "battle/ai.hpp"
#include "battle/objectives.hpp"

namespace openday::game {
namespace {

constexpr Point kStep[8] = {{0, 1}, {-1, 1}, {-1, 0}, {-1, -1}, {0, -1}, {1, -1}, {1, 0}, {1, 1}};

enum Movement { kGround = 1, kTrooper, kGrav, kBoat, kAir, kUnderwater, kTeleport, kNoneWater, kGravFly };

template <class... Fs> struct Overloaded : Fs... {
    using Fs::operator()...;
};
template <class... Fs> Overloaded(Fs...) -> Overloaded<Fs...>;

// Does an armed mine of `mineType` go off under a unit of this movement
// class? (tactical-movement.md section 5.3)
bool mineTriggers(int movement, int mineType, bool finalTile) {
    switch (movement) {
    case kGround:
    case kTrooper: return true;
    case kGrav: return mineType == 1;
    case kAir:
    case kUnderwater: return finalTile;
    case kTeleport:
    case kGravFly: return mineType == 1 && finalTile;
    default: return false;
    }
}

const char* shotErrorText(ShotError e) {
    switch (e) {
    case ShotError::NoWeapon: return "no weapon";
    case ShotError::NotEnoughAp: return "not enough AP";
    case ShotError::OutOfRange: return "out of range";
    case ShotError::TooClose: return "too close";
    case ShotError::NoLineOfFire: return "no line of fire";
    default: return "invalid target";
    }
}

} // namespace

BattleServer::BattleServer(BattleState state, CombatOptions options, std::uint32_t seed)
    : state_(std::move(state)), options_(options), rng_(seed) {
    random = [this](int n) { return static_cast<int>(rng_() % static_cast<std::uint32_t>(n)); };
}

void BattleServer::emit(Event e) {
    applyEvent(state_, e);
    out_.push_back(std::move(e));
}

std::vector<Event> BattleServer::start() {
    out_.clear();
    // The pointer starts before the first player: at slot 0, or in death
    // match at slot 1 or 2 at random (battle-flow.md 1.3).
    state_.turn = 1;
    state_.currentPlayer = state_.computerPlayer ? 0 : 1 + random(2);
    beginNextTurn();
    runComputerTurns();
    return std::move(out_);
}

bool BattleServer::checkEnd() {
    if (state_.over) return true;
    const ObjectiveResult r = checkObjective(state_);
    if (!r.decided) return false;
    emit(BattleOver{state_.turn, r.success, r.loser});
    return true;
}

void BattleServer::beginNextTurn() {
    if (checkEnd()) return;
    const std::vector<int>& order = state_.players;
    if (order.empty()) return;
    int turn = state_.turn;
    auto next = [&](int current) {
        const auto it = std::upper_bound(order.begin(), order.end(), current);
        return it == order.end() ? order.front() : *it;
    };
    int player = next(state_.currentPlayer);
    if (player == 0 && !state_.computerPlayer) {
        ++turn; // the round counter steps over the missing computer player
        player = next(player);
    }
    // The computer player's scripted orders may lapse (Probability).
    if (player == 0 && state_.computerPlayer) {
        for (int i = 0; i < static_cast<int>(state_.units().size()); ++i) {
            const BattleUnit& u = state_.unit(i);
            if (!u.alive) continue;
            const int r = random(100);
            if (u.order.order != 0 && u.order.probability < r) emit(OrderChanged{i, {}});
        }
    }
    emit(TurnStarted{player, turn});
}

void BattleServer::placeReinforcements() {
    const int size = state_.size();
    const std::vector<Reinforcement> pending = state_.reinforcements;
    for (const Reinforcement& r : pending) {
        if (r.turn > state_.turn) continue;
        const UnitType* type = state_.unitType(r.type, -1);
        if (!type) continue;
        BattleUnit probe;
        probe.type = type;
        if (!evaluateTile(state_, probe, r.pos).enterable) continue; // tried again next round
        UnitAdded e{r.unitId, r.type, -1, 0, r.pos, facingToward(r.pos, {size / 2, size / 2}), {}};
        if (r.order) {
            // The order follows from the objective kind (battle-flow.md 4.1).
            const Objective& o = state_.objective;
            int order = 0;
            switch (o.kind) {
            case 0: order = o.ids.empty() ? 5 : 3; break;
            case 1:
            case 3: order = 5; break;
            case 2:
            case 5: order = 6; break;
            case 4: order = 3; break;
            default: break;
            }
            e.order = {order, 99, 100, 65535};
        }
        emit(std::move(e));
    }
}

std::vector<Event> BattleServer::handle(const Request& request) {
    out_.clear();
    if (state_.over) {
        out_.push_back(RequestRejected{"the battle is over"});
        return std::move(out_);
    }
    const bool deploying = state_.deploying;
    std::visit(Overloaded{[&](const MoveRequest& r) { deploying ? emit(RequestRejected{"deployment"}) : handleMove(r); },
                          [&](const ShootRequest& r) { deploying ? emit(RequestRejected{"deployment"}) : handleShoot(r); },
                          [&](const EndTurnRequest& r) { deploying ? emit(RequestRejected{"deployment"}) : handleEndTurn(r); },
                          [&](const DeployRequest& r) { handleDeploy(r); },
                          [&](const UndeployRequest& r) { handleUndeploy(r); },
                          [&](const CheatRequest& r) {
                              // Kind 7 (counter-attack) ignores the flags.
                              if (state_.objective.kind == 7) return emit(RequestRejected{"not in a counter-attack"});
                              emit(BattleOver{state_.turn, r.win, 5});
                          }},
               request);
    runComputerTurns();
    return std::move(out_);
}

void BattleServer::handleMove(const MoveRequest& r) {
    if (r.player != state_.currentPlayer) return emit(RequestRejected{"not your turn"});
    const std::size_t count = std::min<std::size_t>(r.orders.size(), 64);
    for (std::size_t i = 0; i < count; ++i) {
        const UnitOrder& o = r.orders[i];
        if (o.unit < 0 || o.unit >= static_cast<int>(state_.units().size()) || !state_.unit(o.unit).alive ||
            state_.unit(o.unit).owner != r.player) {
            emit(RequestRejected{"not your unit"});
            return;
        }
        // The rest of a group order is dropped after an interruption.
        const bool complete = walk(o.unit, o.path);
        if (checkEnd() || !complete) return;
    }
}

bool BattleServer::walk(int unitIndex, const std::vector<PathStep>& requested) {
    if (requested.empty()) return true;
    const BattleUnit& u = state_.unit(unitIndex);
    const Point start = u.pos;
    const int movement = u.type->movement;

    // Check the path against the server's map and price it ourselves.
    std::vector<PathStep> path;
    Point prev = start;
    for (const PathStep& s : requested) {
        const TileEval e = evaluateTile(state_, u, s.pos);
        const bool adjacent = std::abs(s.pos.x - prev.x) <= 1 && std::abs(s.pos.y - prev.y) <= 1;
        if (!e.crossable() || (!adjacent && movement != kTeleport) || (s.pos.x == prev.x && s.pos.y == prev.y)) {
            emit(RequestRejected{"path blocked"});
            return false;
        }
        path.push_back({s.pos, e.cost, e.enterable, facingOfStep(s.pos.x - prev.x, s.pos.y - prev.y)});
        prev = s.pos;
    }
    if (movement == kTeleport) {
        if (path.size() != 1) {
            emit(RequestRejected{"path blocked"});
            return false;
        }
        path[0].facing = roll(8);
    }
    const int available = u.availableAp();
    path.resize(static_cast<std::size_t>(affordableSteps(path, available)));
    if (path.empty() || !path.back().enterable) {
        emit(RequestRejected{"not enough AP"});
        return false;
    }

    // Reaction-fire zones, built once at the start (section 5.1).
    struct Zone {
        int unit;
        int left, top, right, bottom;
    };
    std::vector<Zone> zones;
    if (u.type->jammer > 0)
        for (int i = 0; i < static_cast<int>(state_.units().size()); ++i) {
            const BattleUnit& z = state_.unit(i);
            if (!z.alive || !z.type->canOverwatch || z.availableAp() < z.type->shotAp ||
                unitSide(z, u, options_.coop) != Side::Enemy)
                continue;
            const int range = z.type->range;
            zones.push_back({i, z.pos.x - range, z.pos.y - range, z.pos.x + z.type->sizeX - 1 + range,
                             z.pos.y + z.type->sizeY - 1 + range});
        }

    enum class Stop { None, Reaction, Detection, Mine };
    std::vector<int> detected;
    int triggered = -1;
    auto check = [&](Point p, bool finalTile) {
        for (const Zone& z : zones)
            if (p.x >= z.left && p.x <= z.right && p.y >= z.top && p.y <= z.bottom &&
                state_.unit(z.unit).alive && checkShot(state_, z.unit, p, options_.coop).ok())
                return Stop::Reaction;
        if (const int m = u.type->mineDetectionRange; m > 0 && u.owner >= 0 && u.owner < kPlayerSlots) {
            detected.clear();
            for (int i = 0; i < static_cast<int>(state_.mines().size()); ++i) {
                const BattleMine& mine = state_.mine(i);
                if (mine.alive && mine.hidden[static_cast<std::size_t>(u.owner)] && chebyshev(mine.pos, p) <= m)
                    detected.push_back(i);
            }
            if (!detected.empty()) return Stop::Detection;
        }
        const int mine = state_.mineAt(p);
        if (mine >= 0 && state_.mine(mine).armed && mineTriggers(movement, state_.mine(mine).type, finalTile)) {
            triggered = mine;
            return Stop::Mine;
        }
        return Stop::None;
    };

    // The unit is moved along on the server's map so that line-of-fire
    // checks see it where it is; the event below makes the move final.
    Stop stop = check(start, false);
    std::size_t walked = 0;
    while (stop == Stop::None && walked < path.size()) {
        state_.moveUnit(unitIndex, path[walked].pos);
        ++walked;
        // Moves are only interrupted on tiles the unit can stop on *(OpenDay choice)*.
        if (path[walked - 1].enterable) stop = check(path[walked - 1].pos, walked == path.size());
    }
    state_.moveUnit(unitIndex, start);

    if (walked > 0) {
        UnitMoved m;
        m.unit = unitIndex;
        for (std::size_t i = 0; i < walked; ++i) m.tiles.push_back(path[i].pos);
        m.facing = path[walked - 1].facing;
        const std::vector<PathStep> done(path.begin(), path.begin() + static_cast<std::ptrdiff_t>(walked));
        m.ap = state_.unit(unitIndex).ap - moveCost(done, static_cast<int>(walked), available);
        emit(std::move(m));
    }

    switch (stop) {
    case Stop::None: return true;
    case Stop::Reaction: reactionFire(unitIndex); break;
    case Stop::Detection: emit(MinesRevealed{state_.unit(unitIndex).owner, detected}); break;
    case Stop::Mine: {
        const BattleMine mine = state_.mine(triggered);
        const MineType& type = state_.rules().mines.at(mine.type);
        emit(MineExploded{triggered});
        damageTile(mine.pos, rollDamage(type.damageBase, type.damageRandom, false), type.apDamage);
        const int owner = state_.unit(unitIndex).owner;
        if (owner >= 0 && owner < kPlayerSlots) {
            MinesRevealed seen{owner, {}};
            for (const Point d : kStep) {
                const int m = state_.mineAt({mine.pos.x + d.x, mine.pos.y + d.y});
                if (m >= 0 && state_.mine(m).hidden[static_cast<std::size_t>(owner)]) seen.mines.push_back(m);
            }
            if (!seen.mines.empty()) emit(std::move(seen));
        }
        break;
    }
    }
    return false;
}

void BattleServer::reactionFire(int mover) {
    bool fired = true;
    while (fired && state_.unit(mover).alive) {
        fired = false;
        for (int i = 0; i < static_cast<int>(state_.units().size()) && state_.unit(mover).alive; ++i) {
            const BattleUnit& z = state_.unit(i);
            if (!z.alive || !z.type->canOverwatch || z.availableAp() < z.type->shotAp ||
                unitSide(z, state_.unit(mover), options_.coop) != Side::Enemy)
                continue;
            const ShotCheck c = checkShot(state_, i, state_.unit(mover).pos, options_.coop);
            if (!c.ok()) continue;
            fire(i, c.aim);
            fired = true;
        }
    }
}

void BattleServer::handleShoot(const ShootRequest& r) {
    if (r.player != state_.currentPlayer) return emit(RequestRejected{"not your turn"});
    const int targetUnit = state_.unitAt(r.target);
    const int targetBuilding = state_.buildingAt(r.target);
    auto targetGone = [&] {
        if (targetBuilding >= 0) return !state_.building(targetBuilding).alive;
        if (targetUnit >= 0) return !state_.unit(targetUnit).alive;
        return false;
    };
    const std::size_t count = std::min<std::size_t>(r.orders.size(), 64);
    for (std::size_t i = 0; i < count && !state_.over; ++i) {
        const UnitOrder& o = r.orders[i];
        if (o.unit < 0 || o.unit >= static_cast<int>(state_.units().size()) || !state_.unit(o.unit).alive ||
            state_.unit(o.unit).owner != r.player) {
            emit(RequestRejected{"not your unit"});
            return;
        }
        if (targetGone()) return;
        ShotCheck c = checkShot(state_, o.unit, r.target, options_.coop);
        if (!c.ok() && !o.path.empty()) {
            if (!walk(o.unit, o.path)) return;
            c = checkShot(state_, o.unit, r.target, options_.coop);
        }
        if (!c.ok()) {
            emit(RequestRejected{shotErrorText(c.error)});
            continue;
        }
        fireAt(o.unit, r.target, r.autofire);
        if (checkEnd()) return;
    }
}

bool BattleServer::fireAt(int shooter, Point target, bool repeat) {
    ShotCheck c = checkShot(state_, shooter, target, options_.coop);
    if (!c.ok()) return false;
    const int targetUnit = state_.unitAt(target);
    const int targetBuilding = state_.buildingAt(target);
    auto targetGone = [&] {
        if (targetBuilding >= 0) return !state_.building(targetBuilding).alive;
        if (targetUnit >= 0) return !state_.unit(targetUnit).alive;
        return false;
    };
    const BattleUnit& s = state_.unit(shooter);
    while (true) {
        fire(shooter, c.aim);
        if (!repeat || !s.alive || state_.over) break;
        if (s.type->apDamage) {
            const int t = state_.unitAt(target);
            if (t < 0 || state_.unit(t).availableAp() == 0) break;
        } else if (targetGone()) {
            break;
        }
        c = checkShot(state_, shooter, target, options_.coop);
        if (!c.ok()) break;
    }
    return true;
}

void BattleServer::runComputerTurns() {
    if (!options_.computerAi || !state_.computerPlayer || state_.deploying || state_.turn < 1) return;
    // Without a human participant the computer player would play alone forever.
    const bool humans = std::any_of(state_.players.begin(), state_.players.end(), [](int p) { return p != 0; });
    for (int guard = 0; humans && guard < 100 && !state_.over && state_.currentPlayer == 0; ++guard) {
        TacticalAi(*this).playTurn();
        if (!state_.over) handleEndTurn(EndTurnRequest{0});
    }
}

void BattleServer::fire(int shooterIndex, Point aim) {
    const BattleUnit& s = state_.unit(shooterIndex);
    const UnitType& t = *s.type;
    bool hit = true;
    if (chebyshev(s.pos, aim) > 1) hit = random(100) <= hitChance(state_, shooterIndex, aim);
    Point impact = aim;
    if (!hit) {
        const Point d = kStep[random(8)];
        const Point p{aim.x + d.x, aim.y + d.y};
        if (state_.inside(p)) impact = p;
    }
    const int damage = rollDamage(t.damageBase, t.damageRandom, options_.aiDamage && s.owner == 0);
    emit(UnitShot{shooterIndex, t.hasFacings ? facingToward(s.pos, aim) : s.facing, aim, impact, s.ap - t.shotAp});
    damageArea(impact, damage, t.damageArea, t.apDamage);
}

int BattleServer::rollDamage(int base, int randomPart, bool scaled) {
    int lo = base, hi = base + randomPart;
    if (scaled) {
        const int f = state_.rules().damageEasy +
                      (state_.rules().damageHard - state_.rules().damageEasy) * options_.difficulty / 10;
        lo = lo * f / 100;
        hi = hi * f / 100;
    }
    const int span = hi - lo > 0 ? hi - lo : 1;
    return lo + 1 + random(span); // the draw is made even when span is 1
}

void BattleServer::damageArea(Point impact, int damage, int area, bool apDamage) {
    const int half = area / 2, k = (area + 1) / 2;
    for (int x = impact.x - half; x < impact.x - half + area; ++x)
        for (int y = impact.y - half; y < impact.y - half + area; ++y) {
            const Point p{x, y};
            if (!state_.inside(p)) continue;
            damageTile(p, damage - (damage / k) * chebyshev(impact, p), apDamage);
        }
}

void BattleServer::damageTile(Point tile, int damage, bool apDamage) {
    bool marked = false, mineOrUnitDestroyed = false;
    // 1. Mines are destroyed by any hit, without exploding.
    if (const int m = state_.mineAt(tile); m >= 0) {
        emit(MineDestroyed{m});
        marked = mineOrUnitDestroyed = true;
    }
    // 2. Building.
    if (const int b = state_.buildingAt(tile); b >= 0 && !apDamage && state_.building(b).type->destructible) {
        const int armor = std::max(0, state_.building(b).armor - damage);
        emit(BuildingDamaged{b, armor});
        if (armor == 0) {
            destroyBuilding(b);
            marked = true;
        }
    }
    // 3. Wall.
    if (const int w = state_.wallAt(tile); w >= 0 && !apDamage) {
        const int armor = std::max(0, state_.wall(w).armor - damage);
        emit(WallDamaged{w, armor});
        if (armor == 0) {
            emit(WallDestroyed{w});
            marked = true;
        }
    }
    // 4. Unit.
    if (const int u = state_.unitAt(tile); u >= 0) {
        const BattleUnit& unit = state_.unit(u);
        if (apDamage) {
            int ap = unit.ap, negAp = unit.negAp;
            if (ap >= damage) {
                ap -= damage;
            } else {
                negAp = std::min(unit.type->actionPoints, negAp + damage - ap);
                ap = 0;
            }
            emit(UnitDamaged{u, unit.armor, ap, negAp});
        } else {
            const int armor = std::max(0, unit.armor - damage);
            emit(UnitDamaged{u, armor, unit.ap, unit.negAp});
            if (armor == 0) {
                damage = unit.type->armor; // the rest of the tile takes the wreck's armor
                destroyUnit(u);
                marked = mineOrUnitDestroyed = true;
            }
        }
    }
    // 5. Road.
    if (const int r = state_.roadAt(tile); r >= 0) {
        const bool bridge = state_.classAt(tile) == TerrainClass::DeepWater;
        if ((state_.unitAt(tile) < 0 || bridge || mineOrUnitDestroyed) && !apDamage &&
            damage >= state_.rules().roads.at(state_.roads()[static_cast<std::size_t>(r)].type).armor) {
            emit(RoadDestroyed{r});
            marked = true;
            if (const int u = state_.unitAt(tile); u >= 0 && bridge) {
                emit(UnitDamaged{u, 0, state_.unit(u).ap, state_.unit(u).negAp});
                destroyUnit(u);
            }
        }
    }
    // 6. Terrain burning.
    const bool empty = state_.buildingAt(tile) < 0 && state_.unitAt(tile) < 0 && state_.wallAt(tile) < 0 &&
                       state_.roadAt(tile) < 0 && state_.mineAt(tile) < 0;
    if ((empty || marked) && !apDamage) burn(tile);
}

void BattleServer::burn(Point tile) {
    const TerrainType* t = state_.terrainAt(tile);
    if (t && !t->burned.empty()) emit(TerrainBurned{tile, t->burned});
}

void BattleServer::destroyUnit(int unitIndex) {
    const BattleUnit& u = state_.unit(unitIndex);
    std::string wreck = u.type->ruinImage;
    if (wreck == "UBRUIXXX" || state_.classAt(u.pos) != TerrainClass::Plain || state_.hasRuin(u.pos)) wreck.clear();
    emit(UnitDestroyed{unitIndex, wreck});
}

void BattleServer::destroyBuilding(int buildingIndex) {
    const BattleBuilding& b = state_.building(buildingIndex);
    const BuildingType& t = *b.type;
    BuildingDestroyed e{buildingIndex, {}};
    std::vector<Point> footprint;
    for (int x = b.pos.x; x < b.pos.x + t.sizeX; ++x)
        for (int y = b.pos.y; y < b.pos.y + t.sizeY; ++y)
            if (state_.inside({x, y})) footprint.push_back({x, y});
    if (t.hasRuin) {
        std::vector<Point> tiles;
        for (const Point p : footprint)
            if (state_.classAt(p) == TerrainClass::Plain && !state_.hasRuin(p)) tiles.push_back(p);
        for (std::size_t i = tiles.size(); i > 1; --i)
            std::swap(tiles[i - 1], tiles[static_cast<std::size_t>(random(static_cast<int>(i)))]);
        static constexpr char kRace[] = {'H', 'T', 'B', 'M'};
        const std::string prefix = std::string("B") + kRace[std::clamp(t.race, 0, 3)] + "RUI";
        const int sprites = state_.rules().ruinCount(prefix);
        const int n = std::clamp(t.sizeX * t.sizeY / 2, 1, 7);
        for (int i = 0; i < n && i < static_cast<int>(tiles.size()) && sprites > 0; ++i)
            e.rubble.push_back({prefix + threeDigits(random(sprites)), tiles[static_cast<std::size_t>(i)]});
    }
    emit(std::move(e));
    for (const Point p : footprint) burn(p);
}

void BattleServer::handleDeploy(const DeployRequest& r) {
    if (!state_.deploying) return emit(RequestRejected{"the battle has started"});
    if (r.player < 1 || r.player >= kPlayerSlots) return emit(RequestRejected{"invalid player"});
    int budget = state_.deployBudget[static_cast<std::size_t>(r.player)];
    int nextId = 1;
    for (const BattleUnit& u : state_.units()) nextId = std::max(nextId, u.id + 1);
    const int size = state_.size();
    bool placed = false;
    for (const DeployEntry& e : r.entries) {
        const UnitType* type = state_.unitType(e.typeId, e.techLevel);
        // Skipped: unknown or not offered, too expensive, outside the zone,
        // or a tile the unit cannot stand on.
        if (!type || std::find(state_.purchasable.begin(), state_.purchasable.end(), e.typeId) == state_.purchasable.end())
            continue;
        if (type->cost > budget || !state_.inDeployZone(r.player, e.pos)) continue;
        BattleUnit probe;
        probe.type = type;
        probe.owner = r.player;
        if (!evaluateTile(state_, probe, e.pos).enterable) continue;
        budget -= type->cost;
        deployed_.push_back(static_cast<int>(state_.units().size()));
        emit(UnitAdded{nextId++, e.typeId, e.techLevel, r.player, e.pos, facingToward(e.pos, {size / 2, size / 2}), {}});
        placed = true;
    }
    if (placed) emit(DeployBudget{r.player, budget});
    else emit(RequestRejected{"cannot deploy there"});
}

void BattleServer::handleUndeploy(const UndeployRequest& r) {
    if (!state_.deploying) return emit(RequestRejected{"the battle has started"});
    if (std::find(deployed_.begin(), deployed_.end(), r.unit) == deployed_.end() || !state_.unit(r.unit).alive ||
        state_.unit(r.unit).owner != r.player)
        return emit(RequestRejected{"not a deployed unit"});
    const int refund = state_.unit(r.unit).type->cost;
    emit(UnitRemoved{r.unit});
    emit(DeployBudget{r.player, state_.deployBudget[static_cast<std::size_t>(r.player)] + refund});
}

void BattleServer::handleEndTurn(const EndTurnRequest& r) {
    if (r.player != state_.currentPlayer) return emit(RequestRejected{"not your turn"});
    if (r.player == 0 && state_.computerPlayer) placeReinforcements();
    if (r.player == 0) ++state_.turn; // round counter step; sent with the next TurnStarted
    beginNextTurn();
}

} // namespace openday::game
