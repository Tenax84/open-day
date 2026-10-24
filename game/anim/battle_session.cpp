#include "anim/battle_session.hpp"

namespace openday::game {

std::unique_ptr<BattleSession> BattleSession::load(const std::filesystem::path& scenario, const GlobalRules& rules,
                                                   render::AnimationCache* clips, std::uint32_t seed, CombatOptions options,
                                                   const std::function<void(BattleState&)>& prepare) {
    const data::SqliteDb db(scenario);
    TerrainMap terrain = loadTerrainMap(db);
    MapEntities entities;
    std::vector<MapSprite> objects = loadMapObjects(db, rules, terrain, &entities);
    BattleState state = loadBattle(db, rules, terrain);
    startBattle(state);
    if (prepare) prepare(state);
    return std::make_unique<BattleSession>(scenario.stem().string(), std::move(terrain), std::move(objects),
                                           std::move(entities), std::move(state), rules, clips, seed, options);
}

BattleSession::BattleSession(std::string name, TerrainMap terrain, std::vector<MapSprite> objects, MapEntities entities,
                             BattleState state, const GlobalRules& rules, render::AnimationCache* clips,
                             std::uint32_t seed, CombatOptions options)
    : name_(std::move(name)), entities_(std::move(entities)), scene_(std::move(terrain), std::move(objects)),
      animations_(scene_, entities_, rules, clips), server_(state, options, seed), view_(std::move(state), scene_, animations_) {}

void BattleSession::apply(const std::vector<Event>& events) {
    if (log)
        for (const Event& e : events) log(e);
    for (const Event& e : events) {
        view_.apply({e});
        if (const auto* t = std::get_if<TurnStarted>(&e)) {
            const int player = t->player;
            animations_.schedule([this, player] { displayedPlayer_ = player; });
        } else if (std::holds_alternative<BattleOver>(e)) {
            animations_.schedule([this] { displayedOver_ = true; });
        }
    }
}

void BattleSession::begin() { apply(server_.start()); }

std::string BattleSession::send(const Request& request) {
    const std::vector<Event> events = server_.handle(request);
    apply(events);
    std::string refused;
    for (const Event& e : events)
        if (const auto* r = std::get_if<RequestRejected>(&e)) refused = r->reason;
    return refused;
}

MoveRequest moveOrder(const BattleState& s, const std::vector<int>& units, Point target) {
    MoveRequest r{s.currentPlayer, {}};
    for (const PlannedMove& m : planGroupMove(s, units, target)) {
        UnitOrder o{m.unit, m.path};
        o.path.resize(static_cast<std::size_t>(m.affordable));
        r.orders.push_back(std::move(o));
    }
    return r;
}

ShootRequest shootOrder(const BattleState& s, const std::vector<int>& units, Point target, bool autofire) {
    // Every selected unit gets a move-to-fire path (possibly empty), planned
    // with the group claims (tactical-interface.md 3.2, tactical-movement.md 3.7).
    ShootRequest r{s.currentPlayer, {}, target, autofire};
    PlanContext context;
    for (const int u : units) {
        if (r.orders.size() >= 64) break;
        UnitOrder o{u, {}};
        if (planMoveToFire(s, u, target, &context, o.path)) {
            context.vacated.push_back(s.unit(u).pos);
            context.claimed.push_back(o.path.back().pos);
        }
        r.orders.push_back(std::move(o));
    }
    return r;
}

bool isEnemyTarget(const BattleState& s, Point t) {
    const int player = s.currentPlayer;
    if (const int b = s.buildingAt(t); b >= 0) return buildingSide(player, s.building(b), true) == Side::Enemy;
    const int u = s.unitAt(t);
    if (u < 0) return false;
    for (const BattleUnit& mine : s.units())
        if (mine.alive && mine.owner == player) return unitSide(mine, s.unit(u), true) == Side::Enemy;
    return s.unit(u).owner != player;
}

} // namespace openday::game
