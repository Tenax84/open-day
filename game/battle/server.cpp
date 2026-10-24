#include "battle/server.hpp"

#include <algorithm>
#include <cstdlib>

#include "anim/unit_animation.hpp"

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
    emit(TurnStarted{state_.currentPlayer, state_.turn});
    return std::move(out_);
}

std::vector<Event> BattleServer::handle(const Request& request) {
    out_.clear();
    if (state_.over) {
        out_.push_back(RequestRejected{"the battle is over"});
        return std::move(out_);
    }
    std::visit(Overloaded{[&](const MoveRequest& r) { handleMove(r); }, [&](const ShootRequest& r) { handleShoot(r); },
                          [&](const EndTurnRequest& r) { handleEndTurn(r); }},
               request);
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
        if (!walk(o.unit, o.path) || state_.over) return;
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
        const BattleUnit& s = state_.unit(o.unit);
        while (true) {
            fire(o.unit, c.aim);
            if (!r.autofire || !s.alive || state_.over) break;
            if (s.type->apDamage) {
                const int t = state_.unitAt(r.target);
                if (t < 0 || state_.unit(t).availableAp() == 0) break;
            } else if (targetGone()) {
                break;
            }
            c = checkShot(state_, o.unit, r.target, options_.coop);
            if (!c.ok()) break;
        }
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

void BattleServer::handleEndTurn(const EndTurnRequest& r) {
    if (r.player != state_.currentPlayer) return emit(RequestRejected{"not your turn"});
    const auto& order = state_.players;
    auto it = std::find(order.begin(), order.end(), r.player);
    const int next = (it == order.end() || it + 1 == order.end()) ? order.front() : *(it + 1);
    int turn = state_.turn;
    if (r.player == 0) ++turn; // a round ends with the AI side's turn
    if (state_.turnLimit > 0 && turn > 0 && turn > state_.turnLimit) {
        emit(BattleOver{turn});
        return;
    }
    emit(TurnStarted{next, turn});
}

} // namespace openday::game
