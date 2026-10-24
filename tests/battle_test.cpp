// Tactical battle rules on synthetic maps: tile evaluation, path finding,
// AP cuts, line of fire, shot checks, damage, interruptions and turns.

#include "battle/battle_state.hpp"
#include "battle/combat.hpp"
#include "battle/messages.hpp"
#include "battle/movement.hpp"
#include "battle/server.hpp"

#include <cstdio>
#include <cstdlib>
#include <deque>
#include <string>
#include <variant>
#include <vector>

using namespace openday;
using namespace openday::game;

namespace {

int failures = 0;

#define CHECK(cond)                                                                       \
    do {                                                                                  \
        if (!(cond)) {                                                                    \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                                   \
        }                                                                                 \
    } while (0)

enum TypeId { kTank = 1, kTrooper, kChopper, kBoat, kArtillery, kParalyzer };

UnitType armed(int movement, int ap, int armor, int range, int shotAp, int base, int random) {
    UnitType t;
    t.image = "TEST";
    t.movement = movement;
    t.actionPoints = ap;
    t.armor = armor;
    t.hasWeapon = true;
    t.range = range;
    t.hit = {80, 60, 40};
    t.shotAp = shotAp;
    t.damageBase = base;
    t.damageRandom = random;
    t.canOverwatch = true;
    t.jammer = 100;
    t.ruinImage = "WRECK";
    return t;
}

GlobalRules makeRules() {
    GlobalRules r;
    // '.' plain (burns to ','), ',' burned plain, 'f' forest (blocks fire),
    // 'm' mountains, 'w' deep water.
    r.terrain = {{".", {TerrainClass::Plain, true, ","}},
                 {",", {TerrainClass::Plain, true, ""}},
                 {"f", {TerrainClass::Forest, false, ""}},
                 {"m", {TerrainClass::Mountains, false, ""}},
                 {"w", {TerrainClass::DeepWater, true, ""}}};
    r.roads = {{0, {"ROAL", 999}}, {1, {"ROAH", 10}}};
    r.walls = {{0, {"WALLC", 60}}};
    r.mines = {{0, {"MINE", false, 25, 10}}, {1, {"GMINE", false, 25, 10}}};
    r.ruins = {{"WRECK", {true, true}}, {"BLOCK", {true, false}}};
    r.ranges = {3, 6};
    r.damageEasy = 70;
    r.damageHard = 130;
    r.units[{kTank, 0}] = armed(1, 100, 50, 6, 30, 30, 10);
    UnitType trooper = armed(2, 80, 5, 4, 40, 3, 1);
    trooper.mineDetectionRange = 1;
    r.units[{kTrooper, 0}] = trooper;
    r.units[{kChopper, 0}] = armed(5, 200, 20, 5, 50, 10, 5);
    r.units[{kBoat, 0}] = armed(4, 200, 40, 6, 50, 20, 5);
    UnitType artillery = armed(1, 100, 20, 9, 50, 20, 5);
    artillery.indirect = true;
    artillery.minRange = 2;
    artillery.damageArea = 3;
    r.units[{kArtillery, 0}] = artillery;
    UnitType paralyzer = armed(1, 100, 20, 6, 30, 50, 0);
    paralyzer.apDamage = true;
    r.units[{kParalyzer, 0}] = paralyzer;
    return r;
}

const GlobalRules& rules() {
    static const GlobalRules r = makeRules();
    return r;
}

// Rows of terrain characters, row y = 0 first.
BattleState world(const std::vector<std::string>& rows) {
    TerrainMap t;
    t.size = static_cast<int>(rows.size());
    for (const std::string& row : rows)
        for (const char c : row) t.tiles.push_back(std::string(1, c));
    BattleState s(rules(), t);
    s.players = {0, 1, 2};
    s.currentPlayer = 1;
    return s;
}

std::vector<std::string> open(int n) { return std::vector<std::string>(static_cast<std::size_t>(n), std::string(static_cast<std::size_t>(n), '.')); }

int add(BattleState& s, int type, int owner, Point p) {
    const UnitType& t = rules().units.at({type, 0});
    return s.addUnit({100 + static_cast<int>(s.units().size()), &t, owner, t.actionPoints, 0, t.armor, p, 0});
}

BattleUnit owned(const UnitType& t, int owner) {
    BattleUnit u;
    u.type = &t;
    u.owner = owner;
    return u;
}

bool same(Point a, Point b) { return a.x == b.x && a.y == b.y; }

// Random numbers from a script; 0 when it runs out.
struct Script {
    std::deque<int> values;
    int operator()(int n) {
        if (values.empty()) return 0;
        const int v = values.front();
        values.pop_front();
        return v % n;
    }
};

template <class T> int count(const std::vector<Event>& events) {
    int n = 0;
    for (const Event& e : events) n += std::holds_alternative<T>(e) ? 1 : 0;
    return n;
}

template <class T> const T* first(const std::vector<Event>& events) {
    for (const Event& e : events)
        if (const T* t = std::get_if<T>(&e)) return t;
    return nullptr;
}

// The client's copy, built from the events alone, matches the server.
void checkReplica(BattleState replica, const std::vector<Event>& events, const BattleState& server) {
    for (const Event& e : events) applyEvent(replica, e);
    CHECK(replica.units().size() == server.units().size());
    for (std::size_t i = 0; i < server.units().size(); ++i) {
        const BattleUnit &a = replica.units()[i], &b = server.units()[i];
        CHECK(a.alive == b.alive && same(a.pos, b.pos) && a.ap == b.ap && a.negAp == b.negAp && a.armor == b.armor &&
              a.facing == b.facing);
    }
    CHECK(replica.terrain().tiles == server.terrain().tiles);
    CHECK(replica.currentPlayer == server.currentPlayer && replica.turn == server.turn && replica.over == server.over);
}

void testTiles() {
    BattleState s = world({".fmw.", ".....", ".....", ".....", "....."});
    const BattleUnit tank = owned(rules().units.at({kTank, 0}), 1);
    const BattleUnit trooper = owned(rules().units.at({kTrooper, 0}), 1);
    const BattleUnit chopper = owned(rules().units.at({kChopper, 0}), 1);
    const BattleUnit boat = owned(rules().units.at({kBoat, 0}), 1);

    TileEval e = evaluateTile(s, tank, {0, 0});
    CHECK(e.enterable && !e.flyOver && e.cost == 10);
    e = evaluateTile(s, tank, {1, 0});
    CHECK(!e.crossable());
    e = evaluateTile(s, trooper, {1, 0});
    CHECK(e.enterable && e.cost == 13);
    e = evaluateTile(s, chopper, {2, 0});
    CHECK(!e.enterable && e.flyOver);
    e = evaluateTile(s, chopper, {0, 0});
    CHECK(e.enterable && e.flyOver);
    CHECK(evaluateTile(s, boat, {3, 0}).enterable);
    CHECK(!evaluateTile(s, tank, {3, 0}).crossable());
    CHECK(!evaluateTile(s, tank, {-1, 0}).crossable());

    // Roads: a bridge for ground units, a barrier for boats; road costs.
    s.addRoad({0, 0, {3, 0}});
    s.addRoad({1, 0, {4, 0}});
    e = evaluateTile(s, tank, {3, 0});
    CHECK(e.enterable && e.cost == 7 && e.reason == BlockReason::Road);
    CHECK(evaluateTile(s, tank, {4, 0}).cost == 4);
    e = evaluateTile(s, boat, {3, 0});
    CHECK(!e.crossable() && e.reason == BlockReason::BridgeBlocksBoat);
    CHECK(evaluateTile(s, chopper, {3, 0}).enterable);

    // Obstacles only clear "enterable".
    add(s, kTank, 0, {0, 1});
    CHECK(evaluateTile(s, tank, {0, 1}).reason == BlockReason::Unit);
    CHECK(!evaluateTile(s, chopper, {0, 1}).enterable && evaluateTile(s, chopper, {0, 1}).flyOver);
    s.addWall({0, 0, 60, {1, 1}});
    CHECK(evaluateTile(s, tank, {1, 1}).reason == BlockReason::Wall);
    BattleMine mine{0, 0, {2, 1}};
    s.addMine(mine); // standard mine, known to everyone
    CHECK(evaluateTile(s, tank, {2, 1}).reason == BlockReason::Mine);
    CHECK(evaluateTile(s, chopper, {2, 1}).enterable);
    BattleMine hidden{1, 0, {3, 1}};
    hidden.hidden[1] = true;
    s.addMine(hidden);
    CHECK(evaluateTile(s, tank, {3, 1}).enterable);
    s.addRuin({"BLOCK", {4, 1}});
    s.addRuin({"WRECK", {4, 2}});
    CHECK(evaluateTile(s, tank, {4, 1}).reason == BlockReason::Ruin);
    CHECK(evaluateTile(s, tank, {4, 2}).enterable);
}

void testPaths() {
    BattleState s = world(open(6));
    const int tank = add(s, kTank, 1, {0, 0});
    std::vector<PathStep> p = findPath(s, tank, {3, 0});
    CHECK(p.size() == 3 && same(p[2].pos, {3, 0}) && p[0].facing == 6 && p[0].cost == 10);
    p = findPath(s, tank, {2, 2});
    CHECK(p.size() == 2 && p[0].facing == 7 && same(p[0].pos, {1, 1}));
    CHECK(findPath(s, tank, {0, 0}).empty());

    // Around a wall from (2,0) to (2,3).
    for (int y = 0; y < 4; ++y) s.addWall({0, 1, 60, {2, y}});
    p = findPath(s, tank, {4, 0});
    CHECK(!p.empty() && same(p.back().pos, {4, 0}));
    for (const PathStep& st : p) CHECK(st.pos.x != 2 || st.pos.y > 3);
    // Walled in completely: unreachable.
    for (int y = 4; y < 6; ++y) s.addWall({0, 1, 60, {2, y}});
    CHECK(findPath(s, tank, {4, 0}).empty());
    // An occupied target is refused.
    add(s, kTank, 1, {1, 3});
    CHECK(findPath(s, tank, {1, 3}).empty());

    // Teleport-free classes: a boat cannot reach land.
    BattleState w = world({"www", "w..", "www"});
    const int boat = add(w, kBoat, 1, {0, 0});
    CHECK(findPath(w, boat, {2, 0}).size() == 2);
    CHECK(findPath(w, boat, {1, 1}).empty());
}

void testApCut() {
    const std::vector<PathStep> walk = {{{1, 0}, 10, true, 6}, {{2, 0}, 10, true, 6}, {{3, 0}, 10, true, 6}};
    CHECK(affordableSteps(walk, 30) == 3);
    CHECK(affordableSteps(walk, 25) == 2);
    CHECK(affordableSteps(walk, 5) == 0);
    // A flyer over tiles it cannot stop on overruns its AP to the next landing tile.
    const std::vector<PathStep> fly = {
        {{1, 0}, 10, true, 6}, {{2, 0}, 10, false, 6}, {{3, 0}, 10, false, 6}, {{4, 0}, 10, true, 6}};
    CHECK(affordableSteps(fly, 15) == 1);
    CHECK(affordableSteps(fly, 25) == 4);
    CHECK(moveCost(fly, 4, 25) == 25);

    PlannedMove m;
    m.path = walk;
    m.affordable = 2;
    const std::vector<std::string> marks = previewSprites(m);
    CHECK(marks.size() == 3 && marks[0] == "PATH0000" && marks[1] == "PATH0000" && marks[2] == "PATH0005");
    m.affordable = 3;
    CHECK(previewSprites(m)[2] == "PATH0004");
    m.path = fly;
    m.affordable = 1;
    CHECK(previewSprites(m)[1] == "PATH0003");

    // Group orders spread over the tiles around a taken target.
    BattleState s = world(open(6));
    const int a = add(s, kTank, 1, {0, 0}), b = add(s, kTank, 1, {0, 1});
    const std::vector<PlannedMove> group = planGroupMove(s, {a, b}, {4, 4});
    CHECK(group.size() == 2 && same(group[0].target, {4, 4}) && !same(group[1].target, {4, 4}));
    CHECK(std::abs(group[1].target.x - 4) <= 1 && std::abs(group[1].target.y - 4) <= 1);
}

void testLineAndShots() {
    const std::vector<Point> line = fireLine({0, 0}, {3, 1});
    CHECK(line.size() == 4 && same(line[1], {1, 0}) && same(line[2], {2, 1}) && same(line[3], {3, 1}));
    const std::vector<Point> back = fireLine({3, 1}, {0, 0});
    CHECK(back.size() == 4 && same(back[0], {3, 1}) && same(back[1], {2, 1}) && same(back[2], {1, 0}));

    BattleState s = world(open(10));
    const int tank = add(s, kTank, 1, {0, 0});
    add(s, kTrooper, 0, {4, 0});
    add(s, kTrooper, 0, {7, 0});
    add(s, kTank, 1, {0, 3});
    CHECK(checkShot(s, tank, {4, 0}, true).ok());
    CHECK(checkShot(s, tank, {7, 0}, true).error == ShotError::OutOfRange);
    CHECK(checkShot(s, tank, {0, 3}, true).error == ShotError::InvalidTarget); // own unit
    CHECK(checkShot(s, tank, {5, 5}, true).ok());                              // empty ground
    CHECK(checkShot(s, tank, {0, 0}, true).error == ShotError::InvalidTarget);
    add(s, kTank, 1, {2, 0});
    CHECK(checkShot(s, tank, {4, 0}, true).error == ShotError::NoLineOfFire);

    // Indirect fire ignores blockers but not its exclusive minimum range.
    const int art = add(s, kArtillery, 1, {0, 9});
    add(s, kTank, 1, {1, 9});
    CHECK(checkShot(s, art, {2, 9}, true).error == ShotError::TooClose);
    CHECK(checkShot(s, art, {3, 9}, true).ok());
    s.unit(art).ap = 10;
    CHECK(checkShot(s, art, {3, 9}, true).error == ShotError::NotEnoughAp);

    // Forest blocks direct fire; the target tile itself does not count.
    BattleState f = world({".f..f", ".....", ".....", ".....", "....."});
    const int t2 = add(f, kTank, 1, {0, 0});
    CHECK(checkShot(f, t2, {2, 0}, true).error == ShotError::NoLineOfFire);
    CHECK(checkShot(f, t2, {1, 0}, true).ok());
    CHECK(checkShot(f, t2, {0, 4}, true).ok());

    // Hit chance bands: short up to 3, medium up to 6; adjacent always hits.
    BattleState h = world(open(10));
    const int shooter = add(h, kTank, 1, {0, 0});
    CHECK(hitChance(h, shooter, {1, 1}) > 100);
    CHECK(hitChance(h, shooter, {3, 0}) == 80);
    CHECK(hitChance(h, shooter, {6, 2}) == 60);
    CHECK(hitChance(h, shooter, {7, 0}) == 40);
}

void testDamage() {
    BattleState s = world(open(10));
    const int tank = add(s, kTank, 1, {0, 0});
    const int enemy = add(s, kTank, 0, {3, 0});
    const BattleState initial = s;
    BattleServer server(s, {}, 1);
    Script rng;
    server.random = [&](int n) { return rng(n); };

    // Hit (roll 0 <= 80), damage 30 + 1 + 9 = 40.
    rng.values = {0, 9};
    std::vector<Event> all = server.handle(ShootRequest{1, {{tank, {}}}, {3, 0}, false});
    const UnitShot* shot = first<UnitShot>(all);
    CHECK(shot && same(shot->impact, {3, 0}) && shot->ap == 70);
    CHECK(server.state().unit(enemy).armor == 10);
    // Miss (99 > 80), scatter direction 6 = (+1, 0), damage roll 0: the
    // shell burns the empty tile it lands on.
    rng.values = {99, 6, 0};
    std::vector<Event> ev = server.handle(ShootRequest{1, {{tank, {}}}, {3, 0}, false});
    shot = first<UnitShot>(ev);
    CHECK(shot && same(shot->impact, {4, 0}));
    CHECK(server.state().unit(enemy).armor == 10);
    CHECK(server.state().terrain().at(4, 0) == ",");
    all.insert(all.end(), ev.begin(), ev.end());
    // Third shot kills; the wreck stays on plain ground, which burns.
    rng.values = {0, 0};
    ev = server.handle(ShootRequest{1, {{tank, {}}}, {3, 0}, false});
    CHECK(!server.state().unit(enemy).alive);
    const UnitDestroyed* dead = first<UnitDestroyed>(ev);
    CHECK(dead && dead->wreck == "WRECK" && server.state().hasRuin({3, 0}));
    CHECK(server.state().unit(tank).ap == 10);
    all.insert(all.end(), ev.begin(), ev.end());
    // Out of AP now.
    ev = server.handle(ShootRequest{1, {{tank, {}}}, {3, 0}, false});
    CHECK(count<RequestRejected>(ev) == 1 && count<UnitShot>(ev) == 0);
    checkReplica(initial, all, server.state());

    // Area damage with falloff hits friend and foe; AP damage builds negAP.
    BattleState a = world(open(10));
    const int art = add(a, kArtillery, 1, {0, 0});
    const int t1 = add(a, kTrooper, 0, {5, 0});
    const int t2 = add(a, kTrooper, 0, {6, 1});
    const int own = add(a, kTrooper, 1, {4, 0});
    const int para = add(a, kParalyzer, 1, {0, 5});
    const int victim = add(a, kTank, 0, {3, 5});
    a.unit(victim).ap = 30;
    BattleServer s2(a, {}, 1);
    Script r2;
    s2.random = [&](int n) { return r2(n); };
    r2.values = {0, 4}; // hit, D = 20 + 1 + 4 = 25; ring: 25 - 12 = 13
    s2.handle(ShootRequest{1, {{art, {}}}, {5, 0}, false});
    CHECK(!s2.state().unit(t1).alive && !s2.state().unit(t2).alive && !s2.state().unit(own).alive);
    r2.values = {0, 0}; // D = 51: 30 AP taken, 21 carried over as negAP
    s2.handle(ShootRequest{1, {{para, {}}}, {3, 5}, false});
    CHECK(s2.state().unit(victim).ap == 0 && s2.state().unit(victim).negAp == 21 && s2.state().unit(victim).alive);

    // AI damage scaled by difficulty: hard (10) = 130 %.
    BattleState d = world(open(10));
    const int ai = add(d, kTank, 0, {0, 0});
    const int human = add(d, kTank, 1, {2, 0});
    d.currentPlayer = 0;
    CombatOptions hard;
    hard.difficulty = 10;
    BattleServer s3(d, hard, 1);
    Script r3;
    s3.random = [&](int n) { return r3(n); };
    r3.values = {0, 0}; // lo = 30 * 1.3 = 39 -> D = 40
    s3.handle(ShootRequest{0, {{ai, {}}}, {2, 0}, false});
    CHECK(s3.state().unit(human).armor == 10);
}

void testInterruptions() {
    // A hidden enemy mine is ignored by planning and goes off when entered.
    BattleState s = world(open(8));
    const int tank = add(s, kTank, 1, {0, 0});
    BattleMine mine{0, 0, {2, 0}};
    mine.hidden[1] = true;
    s.addMine(mine);
    const BattleState initial = s;
    BattleServer server(s, {}, 1);
    Script rng;
    server.random = [&](int n) { return rng(n); };
    const PlannedMove plan = planMove(server.state(), tank, {4, 0});
    CHECK(plan.path.size() == 4 && plan.affordable == 4);
    rng.values = {9}; // mine damage 25 + 1 + 9 = 35
    std::vector<Event> ev = server.handle(MoveRequest{1, {{tank, plan.path}}});
    const UnitMoved* moved = first<UnitMoved>(ev);
    CHECK(moved && moved->tiles.size() == 2 && moved->ap == 80);
    CHECK(count<MineExploded>(ev) == 1 && server.state().unit(tank).armor == 15);
    CHECK(!server.state().mine(0).alive);
    checkReplica(initial, ev, server.state());

    // A trooper detects a hidden mine one tile ahead and stops.
    BattleState d = world(open(8));
    const int trooper = add(d, kTrooper, 1, {0, 0});
    BattleMine m2{0, 0, {3, 0}};
    m2.hidden[1] = true;
    d.addMine(m2);
    BattleServer s2(d, {}, 1);
    ev = s2.handle(MoveRequest{1, {{trooper, planMove(s2.state(), trooper, {5, 0}).path}}});
    const MinesRevealed* seen = first<MinesRevealed>(ev);
    CHECK(seen && seen->player == 1 && seen->mines.size() == 1);
    CHECK(same(s2.state().unit(trooper).pos, {2, 0}) && !s2.state().mine(0).hidden[1]);
    CHECK(!evaluateTile(s2.state(), s2.state().unit(trooper), {3, 0}).enterable);

    // Reaction fire stops a move on the first tile the enemy can hit, and
    // goes on until the mover is destroyed.
    BattleState r = world(open(12));
    const int mover = add(r, kTank, 1, {0, 5});
    const int guard = add(r, kTank, 0, {8, 0});
    const BattleState rInitial = r;
    BattleServer s3(r, {}, 1);
    Script r3;
    s3.random = [&](int n) { return r3(n); }; // always hits, D = 31
    ev = s3.handle(MoveRequest{1, {{mover, planMove(s3.state(), mover, {5, 5}).path}}});
    moved = first<UnitMoved>(ev);
    CHECK(moved && moved->tiles.size() == 2 && same(moved->tiles.back(), {2, 5}));
    CHECK(count<UnitShot>(ev) == 2 && !s3.state().unit(mover).alive);
    CHECK(s3.state().unit(guard).ap == 40);
    checkReplica(rInitial, ev, s3.state());

    // A group order stops after the interrupted unit.
    BattleState g = world(open(8));
    const int g1 = add(g, kTank, 1, {0, 0});
    const int g2 = add(g, kTank, 1, {0, 1});
    BattleMine m3{0, 0, {2, 0}};
    m3.hidden[1] = true;
    g.addMine(m3);
    BattleServer s4(g, {}, 1);
    MoveRequest group{1, {}};
    for (const PlannedMove& m : planGroupMove(s4.state(), {g1, g2}, {4, 0})) group.orders.push_back({m.unit, m.path});
    CHECK(group.orders.size() == 2);
    ev = s4.handle(group);
    CHECK(count<UnitMoved>(ev) == 1 && count<MineExploded>(ev) == 1 && same(s4.state().unit(g2).pos, {0, 1}));
}

void testTurns() {
    BattleState s = world(open(12));
    const int a = add(s, kTank, 1, {0, 0});
    const int b = add(s, kTank, 2, {1, 0});
    const int c = add(s, kTank, 0, {11, 11}); // out of reaction range
    s.unit(c).negAp = 30;
    s.turnLimit = 1;
    startBattle(s);
    CHECK(s.players == (std::vector<int>{0, 1, 2}) && s.currentPlayer == 1);
    CHECK(s.unit(c).ap == 70 && s.unit(c).negAp == 0);
    const BattleState initial = s;
    BattleServer server(s, {}, 1);
    std::vector<Event> all = server.start();

    std::vector<Event> ev = server.handle(EndTurnRequest{2});
    CHECK(count<RequestRejected>(ev) == 1);
    ev = server.handle(MoveRequest{1, {{a, planMove(server.state(), a, {0, 3}).path}}});
    CHECK(server.state().unit(a).ap == 70);
    all.insert(all.end(), ev.begin(), ev.end());
    ev = server.handle(MoveRequest{1, {{b, planMove(server.state(), b, {1, 3}).path}}});
    CHECK(count<RequestRejected>(ev) == 1); // not player 1's unit

    auto endTurn = [&](int player) {
        std::vector<Event> e = server.handle(EndTurnRequest{player});
        all.insert(all.end(), e.begin(), e.end());
        return e;
    };
    ev = endTurn(1);
    CHECK(first<TurnStarted>(ev) && first<TurnStarted>(ev)->player == 2 && first<TurnStarted>(ev)->turn == 0);
    ev = endTurn(2);
    CHECK(first<TurnStarted>(ev) && first<TurnStarted>(ev)->player == 0 && first<TurnStarted>(ev)->turn == 0);
    ev = endTurn(0);
    CHECK(first<TurnStarted>(ev) && first<TurnStarted>(ev)->player == 1 && first<TurnStarted>(ev)->turn == 1);
    CHECK(server.state().unit(a).ap == 100); // refilled at its owner's turn
    endTurn(1);
    endTurn(2);
    ev = endTurn(0); // turn 2 > turn_qty 1
    CHECK(count<BattleOver>(ev) == 1 && server.state().over);
    CHECK(count<RequestRejected>(server.handle(EndTurnRequest{1})) == 1);
    checkReplica(initial, all, server.state());
}

void testSides() {
    UnitType convoyAlien, convoyHuman, plain;
    convoyAlien.isConvoy = convoyHuman.isConvoy = true;
    convoyAlien.race = 2;
    const BattleUnit h1 = owned(plain, 1), h2 = owned(plain, 2), ai = owned(plain, 0), ca = owned(convoyAlien, 3),
                     ch = owned(convoyHuman, 0);
    CHECK(unitSide(h1, h1, true) == Side::Same);
    CHECK(unitSide(h1, h2, true) == Side::Ally && unitSide(h1, h2, false) == Side::Enemy);
    CHECK(unitSide(h1, ai, true) == Side::Enemy);
    CHECK(unitSide(ca, ai, true) == Side::Same && unitSide(ca, h1, true) == Side::Enemy);
    CHECK(unitSide(ch, ai, true) == Side::Enemy && unitSide(ch, h2, true) == Side::Ally);
}

} // namespace

int main() {
    testTiles();
    testPaths();
    testApCut();
    testLineAndShots();
    testDamage();
    testInterruptions();
    testTurns();
    testSides();
    if (failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    std::puts("all battle tests passed");
    return EXIT_SUCCESS;
}
