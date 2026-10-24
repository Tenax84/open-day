// Tactical battle rules on synthetic maps: tile evaluation, path finding,
// AP cuts, line of fire, shot checks, damage, interruptions and turns.

#include "battle/automated.hpp"
#include "battle/battle_state.hpp"
#include "battle/combat.hpp"
#include "battle/messages.hpp"
#include "battle/movement.hpp"
#include "battle/objectives.hpp"
#include "battle/server.hpp"

#include <cstdio>
#include <cstdlib>
#include <deque>
#include <map>
#include <tuple>
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

enum TypeId { kTank = 1, kTrooper, kChopper, kBoat, kArtillery, kParalyzer, kAlienTank, kFlak, kLightFlak };

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
                 {"w", {TerrainClass::DeepWater, true, ""}},
                 {"r", {TerrainClass::Riverside, true, ""}}};
    r.roads = {{0, {"ROAL", 999}}, {1, {"ROAH", 10}}};
    r.walls = {{0, {"WALLC", 60}}};
    r.mines = {{0, {"MINE", false, 25, 10}}, {1, {"GMINE", false, 25, 10}}};
    r.ruins = {{"WRECK", {true, true}}, {"BLOCK", {true, false}}};
    r.ranges = {3, 6};
    r.damageEasy = 70;
    r.damageHard = 130;
    r.units[{kTank, 0}] = armed(1, 100, 50, 6, 30, 30, 10);
    r.units[{kTank, 0}].cost = 50;
    r.units[{kTank, 0}].typeId = kTank;
    UnitType trooper = armed(2, 80, 5, 4, 40, 3, 1);
    trooper.mineDetectionRange = 1;
    trooper.cost = 20;
    trooper.typeId = kTrooper;
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
    UnitType alien = armed(1, 100, 50, 6, 30, 30, 10);
    alien.race = 2;
    alien.typeId = kAlienTank;
    r.units[{kAlienTank, 0}] = alien;
    for (auto& row : r.aiPriority) row.fill(50);

    // Anti-aircraft units: average damage 20 and 10.
    UnitType flak = armed(1, 100, 100, 6, 30, 15, 10);
    flak.race = 1;
    flak.isAA = true;
    flak.damageAA = 30;
    flak.typeId = kFlak;
    r.units[{kFlak, 0}] = flak;
    UnitType light = armed(1, 100, 100, 6, 30, 5, 10);
    light.race = 1;
    light.isAA = true;
    light.damageAA = 25;
    light.typeId = kLightFlak;
    r.units[{kLightFlak, 0}] = light;

    // Automated units as in the shipped data (automated-units.md 1.3).
    auto automated = [&](int id, int race, AutomatedCategory cat, int cost, int armor) -> AutomatedType& {
        AutomatedType& a = r.automated[id];
        a.id = id;
        a.race = race;
        a.category = static_cast<int>(cat);
        a.cost = cost;
        a.armor = armor;
        a.image = "TEST";
        return a;
    };
    automated(0, 0, AutomatedCategory::Transport, 30, 40).image = "HHTR";
    automated(1, 0, AutomatedCategory::Build, 10, 60);
    automated(2, 0, AutomatedCategory::Mine, 10, 70).mineDetectionRange = 5;
    AutomatedType& medic = automated(3, 0, AutomatedCategory::Repair, 100, 40);
    medic.armorInc = 200;
    medic.damageArea = 3;
    medic.deployOnce = true;
    AutomatedType& bomber = automated(4, 0, AutomatedCategory::Destroy, 150, 40);
    bomber.damage = 150;
    bomber.armorDamage = true;
    bomber.damageArea = 5;
    AutomatedType& gas = automated(5, 0, AutomatedCategory::Destroy, 40, 40);
    gas.damage = 300;
    gas.damageArea = 3;
    automated(7, 1, AutomatedCategory::Transport, 40, 60);
    for (const auto& [id, unit, race] : {std::tuple{14, int(kAlienTank), 2}, std::tuple{15, int(kTrooper), 0}}) {
        AutomatedType& drop = automated(id, race, AutomatedCategory::Drop, 90, 40);
        drop.dropUnit = unit;
        drop.dropCount = 4;
        drop.dropSlots = {DropSlot{-1, -2, 6}, DropSlot{2, 2, 2}, DropSlot{-1, 1, 4}, DropSlot{2, -2, 0}, DropSlot{0, 0, 0}};
    }
    AutomatedType& tauranMedic = automated(10, 1, AutomatedCategory::Repair, 300, 60);
    tauranMedic.armorInc = tauranMedic.actionInc = 200;
    tauranMedic.damageArea = 1;
    tauranMedic.prerequisite = 250;
    AutomatedType& ap = automated(26, 0, AutomatedCategory::Repair, 200, 40);
    ap.actionInc = 450;
    ap.damageArea = 1;
    ap.deployOnce = true;
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

// Every slot played by requests, as in the viewer's hot-seat mode.
CombatOptions hotSeat() {
    CombatOptions o;
    o.computerAi = false;
    return o;
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
    s.turnLimit = 2;
    startBattle(s);
    CHECK(s.players == (std::vector<int>{0, 1, 2}));
    CHECK(s.unit(c).ap == 70 && s.unit(c).negAp == 0);
    const BattleState initial = s;
    BattleServer server(s, hotSeat(), 1);
    // Humans first in slot order, the computer player last; T starts at 1.
    std::vector<Event> all = server.start();
    CHECK(first<TurnStarted>(all) && first<TurnStarted>(all)->player == 1 && first<TurnStarted>(all)->turn == 1);

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
    auto started = [](const std::vector<Event>& e, int player, int turn) {
        const TurnStarted* t = first<TurnStarted>(e);
        return t && t->player == player && t->turn == turn;
    };
    CHECK(started(endTurn(1), 2, 1));
    CHECK(started(endTurn(2), 0, 1));
    CHECK(started(endTurn(0), 1, 2));
    CHECK(server.state().unit(a).ap == 100); // refilled at its owner's turn
    endTurn(1);
    endTurn(2);
    ev = endTurn(0); // T = 3 > turn_qty 2: the turn limit ends the battle
    const BattleOver* over = first<BattleOver>(ev);
    CHECK(over && !over->success && server.state().over && count<TurnStarted>(ev) == 0);
    CHECK(count<RequestRejected>(server.handle(EndTurnRequest{1})) == 1);
    checkReplica(initial, all, server.state());

    // Death match: no computer player; slot 0 is skipped, the round counter
    // still steps there.
    BattleState d = world(open(6));
    add(d, kTank, 1, {0, 0});
    add(d, kTank, 2, {5, 5});
    d.computerPlayer = false;
    d.objective.kind = 10;
    d.turnLimit = 10;
    startBattle(d);
    BattleServer dm(d, {}, 1);
    Script rng;
    dm.random = [&](int n) { return rng(n); };
    rng.values = {0}; // pointer at slot 1: player 2 moves first
    CHECK(started(dm.start(), 2, 1));
    CHECK(started(dm.handle(EndTurnRequest{2}), 1, 2));
    BattleServer dm2(d, {}, 1);
    dm2.random = [&](int n) { return rng(n); };
    rng.values = {1}; // pointer at slot 2: wraps over slot 0, T is already 2
    CHECK(started(dm2.start(), 1, 2));
}

// Runs the objective check on a state at turn t.
ObjectiveResult checkAt(BattleState& s, int t) {
    s.turn = t;
    return checkObjective(s);
}

void testObjectives() {
    // Destroy: listed units and every road / mine in the zone.
    BattleState d = world(open(8));
    add(d, kTank, 1, {0, 0});
    const int target = add(d, kTank, 0, {5, 5});
    d.objective = {0, 0, {d.unit(target).id}, {{3, 3}}};
    d.turnLimit = 5;
    d.addMine({0, 0, {3, 3}});
    CHECK(!checkAt(d, 1).decided);
    d.removeUnit(target);
    CHECK(!checkAt(d, 1).decided);
    d.removeMine(0);
    ObjectiveResult r = checkAt(d, 1);
    CHECK(r.decided && r.success);
    d.objective.ids = {999999};
    CHECK(checkAt(d, 1).success); // an unknown id counts as gone

    // Turn limit: lost after turn_qty rounds.
    BattleState l = world(open(8));
    add(l, kTank, 0, {5, 5});
    l.objective = {6, 0, {}, {}};
    l.turnLimit = 3;
    CHECK(!checkAt(l, 3).decided);
    r = checkAt(l, 4);
    CHECK(r.decided && !r.success);

    // Hold Position, N = 2: counted once per round, one empty round tolerated.
    BattleState h = world(open(8));
    const int holder = add(h, kTank, 1, {0, 0});
    h.objective = {1, 2, {}, {{2, 2}}};
    h.turnLimit = 10;
    CHECK(!checkAt(h, 1).decided && h.holdCount == 0);
    h.moveUnit(holder, {2, 2});
    checkAt(h, 2);
    checkAt(h, 2);
    CHECK(h.holdCount == 1);
    checkAt(h, 3);
    h.moveUnit(holder, {0, 0});
    checkAt(h, 4); // one empty round: kept
    CHECK(h.holdCount == 2);
    checkAt(h, 5); // a second one: lost
    CHECK(h.holdCount == 0);
    h.moveUnit(holder, {2, 2});
    checkAt(h, 6);
    checkAt(h, 7);
    r = checkAt(h, 8);
    CHECK(r.decided && r.success && h.holdCount == 3);

    // Protect: an AI unit in the zone in the last round loses; surviving it wins.
    BattleState p = world(open(8));
    const int intruder = add(p, kTank, 0, {4, 4});
    p.objective = {3, 0, {}, {{4, 4}}};
    p.turnLimit = 5;
    CHECK(!checkAt(p, 4).decided);
    r = checkAt(p, 5);
    CHECK(r.decided && !r.success);
    p.removeUnit(intruder);
    CHECK(!checkAt(p, 5).decided);
    CHECK(checkAt(p, 6).success);

    // Escort / Intercept / Keep.
    BattleState e = world(open(8));
    const int truck = add(e, kTank, 0, {0, 0});
    e.objective = {2, 0, {e.unit(truck).id}, {{7, 7}}};
    e.turnLimit = 10;
    CHECK(!checkAt(e, 1).decided);
    e.moveUnit(truck, {7, 7});
    CHECK(checkAt(e, 1).success);
    e.objective.kind = 4; // the same unit in the zone now loses
    r = checkAt(e, 1);
    CHECK(r.decided && !r.success);
    e.removeUnit(truck);
    CHECK(checkAt(e, 1).success);
    e.objective.kind = 5; // Keep: losing a listed object loses
    r = checkAt(e, 1);
    CHECK(r.decided && !r.success);

    // Destroy All Except: destroying an exempt unit loses.
    BattleState x = world(open(8));
    const int tower = add(x, kTank, 0, {1, 1});
    const int other = add(x, kTank, 0, {2, 2});
    x.objective = {6, 0, {x.unit(tower).id}, {}};
    x.turnLimit = 10;
    CHECK(!checkAt(x, 1).decided);
    x.removeUnit(other);
    CHECK(checkAt(x, 1).success);
    x.removeUnit(tower);
    CHECK(!checkAt(x, 1).success);

    // Death match, two cities.
    BattleState c = world(open(8));
    BuildingType hq;
    hq.id = 130;
    c.addBuilding({1, &hq, 1, 100, {0, 0}});
    const int hq2 = c.addBuilding({2, &hq, 2, 100, {5, 5}});
    c.objective = {8, 0, {}, {}};
    c.turnLimit = 10;
    CHECK(!checkAt(c, 1).decided);
    c.removeBuilding(hq2);
    r = checkAt(c, 1);
    CHECK(r.decided && r.loser == 2);
}

void testReinforcements() {
    BattleState s = world(open(8));
    add(s, kTank, 1, {0, 0});
    const int blocker = add(s, kTank, 0, {6, 6});
    s.reinforcements = {{500, kTank, {7, 7}, 1, true}, {501, kTank, {6, 6}, 1, false}, {502, kTank, {5, 7}, 3, false}};
    s.objective = {1, 3, {}, {{1, 1}}};
    s.turnLimit = 20;
    startBattle(s);
    BattleServer server(s, hotSeat(), 1);
    std::vector<Event> all = server.start();
    auto endTurn = [&](int player) {
        std::vector<Event> e = server.handle(EndTurnRequest{player});
        all.insert(all.end(), e.begin(), e.end());
        return e;
    };
    endTurn(1);
    // End of the computer player's turn in round 1: the free tile is used,
    // the occupied one waits, round 3's row is not due.
    std::vector<Event> ev = endTurn(0);
    CHECK(count<UnitAdded>(ev) == 1);
    const UnitAdded* added = first<UnitAdded>(ev);
    CHECK(added && added->id == 500 && added->order.order == 5); // Hold Position -> patrol
    CHECK(server.state().units().size() == 3 && server.state().reinforcements.size() == 2);
    // The blocker leaves; next round the waiting row is placed.
    endTurn(1);
    {
        BattleState& st = const_cast<BattleState&>(server.state()); // test shortcut: clear the tile
        st.moveUnit(blocker, {3, 6});
    }
    ev = endTurn(0);
    CHECK(count<UnitAdded>(ev) == 1 && first<UnitAdded>(ev)->id == 501);
    endTurn(1);
    ev = endTurn(0);
    CHECK(count<UnitAdded>(ev) == 1 && first<UnitAdded>(ev)->id == 502 && server.state().reinforcements.empty());

    // Probability: orders lapse at the start of the computer player's turn.
    BattleState o = world(open(8));
    add(o, kTank, 1, {0, 0});
    const int keeps = add(o, kTank, 0, {7, 7});
    const int drops = add(o, kTank, 0, {7, 6});
    o.unit(keeps).order = {1, 99, 100, 65535};
    o.unit(drops).order = {1, 99, 30, 65535};
    o.turnLimit = 20;
    startBattle(o);
    BattleServer os(o, hotSeat(), 1);
    Script rng;
    os.random = [&](int n) { return rng(n); };
    os.start();
    rng.values = {0, 99, 99}; // one draw per unit, in unit order
    ev = os.handle(EndTurnRequest{1});
    CHECK(count<OrderChanged>(ev) == 1 && first<OrderChanged>(ev)->unit == drops);
    CHECK(os.state().unit(keeps).order.order == 1 && os.state().unit(drops).order.order == 0);
}


// Runs the computer player's first turn: player 1 ends its turn at once.
std::vector<Event> aiTurn(BattleServer& server) {
    server.start();
    return server.handle(EndTurnRequest{1});
}

void testAi() {
    // A free alien unit walks toward the enemy and fires.
    BattleState h = world(open(14));
    add(h, kTrooper, 1, {12, 0}); // range 4: cannot react at 6
    const int hunter = add(h, kAlienTank, 0, {0, 0});
    h.turnLimit = 20;
    startBattle(h);
    const BattleState hInitial = h;
    BattleServer s1(h, {}, 1);
    std::vector<Event> all = s1.start();
    std::vector<Event> ev = s1.handle(EndTurnRequest{1});
    all.insert(all.end(), ev.begin(), ev.end());
    const UnitMoved* moved = first<UnitMoved>(ev);
    const UnitShot* shot = first<UnitShot>(ev);
    CHECK(moved && moved->unit == hunter && shot && shot->unit == hunter);
    // The AI ended its turn: player 1 again, round 2.
    const TurnStarted* next = nullptr;
    for (const Event& e : ev)
        if (const TurnStarted* t = std::get_if<TurnStarted>(&e)) next = t;
    CHECK(next && next->player == 1 && next->turn == 2 && s1.state().currentPlayer == 1);
    checkReplica(hInitial, all, s1.state());

    // Within range already: it fires from where it stands (stationary fire).
    BattleState st = world(open(10));
    add(st, kTank, 1, {5, 0});
    const int gunner = add(st, kAlienTank, 0, {0, 0});
    startBattle(st);
    BattleServer s2(st, {}, 1);
    ev = aiTurn(s2);
    CHECK(count<UnitShot>(ev) >= 1 && first<UnitShot>(ev)->unit == gunner);
    CHECK(same(s2.state().unit(gunner).pos, {0, 0}) || count<UnitMoved>(ev) > 0);
    CHECK(count<UnitShot>(ev) == 3); // autofire: 100 AP / 30 per shot

    // Goto: walks to its waypoint, which is then done.
    BattleState g = world(open(16));
    add(g, kTank, 1, {15, 15});
    const int walker = add(g, kAlienTank, 0, {0, 0});
    g.unit(walker).order = {1, 99, 100, 65535, {{4, 0}}, {}};
    startBattle(g);
    BattleServer s3(g, {}, 1);
    ev = aiTurn(s3);
    CHECK(same(s3.state().unit(walker).pos, {4, 0}) && s3.state().unit(walker).order.order == 0);

    // An expired order is dropped; the unit then hunts like a free unit.
    BattleState x = world(open(16));
    add(x, kTank, 1, {15, 15});
    const int late = add(x, kAlienTank, 0, {0, 0});
    x.unit(late).order = {5, 0, 100, 65535, {{0, 0}, {1, 0}}, {}};
    startBattle(x);
    BattleServer s4(x, {}, 1);
    ev = aiTurn(s4);
    CHECK(first<OrderChanged>(ev) && first<OrderChanged>(ev)->order.order == 0);

    // Defend: a defender inside its area stays put.
    BattleState d = world(open(16));
    add(d, kTank, 1, {15, 15});
    const int guard = add(d, kAlienTank, 0, {3, 3});
    BuildingType depot;
    depot.sizeX = depot.sizeY = 2;
    d.addBuilding({1000001, &depot, 0, 100, {4, 4}});
    d.unit(guard).order = {4, 99, 100, 65535, {}, {1000001}};
    startBattle(d);
    BattleServer s5(d, {}, 1);
    aiTurn(s5);
    CHECK(same(s5.state().unit(guard).pos, {3, 3}));

    // Without a human participant the computer player does not play alone.
    BattleState alone = world(open(8));
    add(alone, kAlienTank, 0, {0, 0});
    startBattle(alone);
    BattleServer s6(alone, {}, 1);
    ev = s6.start();
    CHECK(first<TurnStarted>(ev) && first<TurnStarted>(ev)->player == 0 && !s6.state().over);
}

void testDeployment() {
    BattleState s = world(open(8));
    add(s, kAlienTank, 0, {7, 7});
    s.deployZone[1] = {{0, 0}, {1, 0}, {2, 0}, {0, 1}};
    s.purchasable = {kTank, kTrooper};
    s.deployBudget[1] = 110;
    s.deploying = true;
    startBattle(s);
    const BattleState initial = s;
    BattleServer server(s, {}, 1);
    std::vector<Event> all;
    auto send = [&](const Request& r) {
        std::vector<Event> e = server.handle(r);
        all.insert(all.end(), e.begin(), e.end());
        return e;
    };
    const int tankCost = rules().units.at({kTank, 0}).cost;
    // Orders are refused while deploying, and the computer player waits.
    std::vector<Event> ev = send(EndTurnRequest{0});
    CHECK(count<RequestRejected>(ev) == 1 && count<UnitMoved>(ev) == 0);
    // Outside the zone: refused.
    ev = send(DeployRequest{1, {{kTank, -1, {5, 5}}}});
    CHECK(count<RequestRejected>(ev) == 1 && count<UnitAdded>(ev) == 0);
    // Inside: placed, facing the map centre, paid for.
    ev = send(DeployRequest{1, {{kTank, -1, {0, 0}}, {kTrooper, -1, {1, 0}}}});
    CHECK(count<UnitAdded>(ev) == 2);
    const DeployBudget* budget = first<DeployBudget>(ev);
    CHECK(budget && budget->budget == 110 - tankCost - rules().units.at({kTrooper, 0}).cost);
    CHECK(server.state().unit(1).owner == 1 && server.state().unit(1).facing == 7);
    // An occupied tile and an unknown type are skipped.
    ev = send(DeployRequest{1, {{kTank, -1, {0, 0}}, {kParalyzer, -1, {2, 0}}}});
    CHECK(count<UnitAdded>(ev) == 0);
    // Taken back: refunded. Scenario units cannot be taken back.
    ev = send(UndeployRequest{1, 1});
    CHECK(count<UnitRemoved>(ev) == 1 && server.state().deployBudget[1] == 110 - rules().units.at({kTrooper, 0}).cost);
    CHECK(count<RequestRejected>(send(UndeployRequest{1, 0})) == 1);
    // Starting the battle ends the deployment.
    std::vector<Event> start = server.start();
    all.insert(all.end(), start.begin(), start.end());
    CHECK(!server.state().deploying && count<RequestRejected>(send(DeployRequest{1, {{kTank, -1, {2, 0}}}})) == 1);
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


// Automated units: docs/mechanics/automated-units.md.
void testAutomatedCalls() {
    // Cost, target tests, DeployOnce and a bomber without AA.
    BattleState s = world(open(12));
    const int tank = add(s, kTank, 1, {2, 2});
    const int enemy = add(s, kAlienTank, 0, {8, 8});
    const int neighbour = add(s, kAlienTank, 0, {3, 2});
    s.unit(tank).armor = 10;
    s.unit(neighbour).armor = 5;
    s.automated[1] = {4, 3};
    s.deployBudget[1] = 350;
    const BattleState initial = s;
    BattleServer server(s, {}, 1);
    std::vector<Event> all;
    auto send = [&](const Request& r) {
        std::vector<Event> e = server.handle(r);
        all.insert(all.end(), e.begin(), e.end());
        return e;
    };
    CHECK(count<RequestRejected>(send(AutomatedRequest{1, 4, {{2, 2}}})) == 1);  // own unit
    CHECK(count<RequestRejected>(send(AutomatedRequest{1, 5, {{8, 8}}})) == 1);  // not in the list
    CHECK(count<RequestRejected>(send(AutomatedRequest{2, 4, {{8, 8}}})) == 1);  // not his turn
    CHECK(server.state().deployBudget[1] == 350);
    std::vector<Event> ev = send(AutomatedRequest{1, 4, {{8, 8}}});
    const AutomatedArrived* arrived = first<AutomatedArrived>(ev);
    CHECK(arrived && arrived->outcome == 1 && arrived->typeId == 4 && same(arrived->tile, {8, 8}) && !arrived->second);
    // D = 150 + 1 at the centre: the alien tank (50) is destroyed; the kill
    // is credited to the caller and the bomber.
    const UnitDestroyed* dead = first<UnitDestroyed>(ev);
    CHECK(dead && dead->unit == enemy && dead->cause.attacker == 1 && dead->cause.automatedType == 4);
    CHECK(server.state().deployBudget[1] == 200);
    // The medic heals the centre fully and the ring by 200 - 100, any owner;
    // it can be called once.
    ev = send(AutomatedRequest{1, 3, {{2, 2}}});
    CHECK(server.state().unit(tank).armor == 50 && server.state().unit(neighbour).armor == 50);
    CHECK(count<AutomatedUsed>(ev) == 1 && server.state().deployBudget[1] == 100);
    const std::vector<int> usable = server.state().usableAutomated(1);
    CHECK(usable.size() == 1 && usable[0] == 4);
    CHECK(count<RequestRejected>(send(AutomatedRequest{1, 3, {{2, 2}}})) == 1);
    // Not enough tactical resource left for a second bomber.
    ev = send(AutomatedRequest{1, 4, {{9, 9}}});
    CHECK(count<RequestRejected>(ev) == 1 && count<AutomatedArrived>(ev) == 0);
    checkReplica(initial, all, server.state());

    // The clip names (8.1).
    CHECK(automatedClipName(rules().automated.at(0), true, AutomatedOutcome::Damaged) == "AUTOUNIT\\HHTR12.SMK");
}

void testAntiAir() {
    // Only the light flak (25) is in range: damaged (25 < 40), the bomb is
    // scaled by (40 - 25) / 40: trunc(150 * 15 / 40) = 56, D = 57.
    BattleState s = world(open(16));
    add(s, kTank, 1, {0, 0});
    const int light = add(s, kLightFlak, 0, {8, 8});
    add(s, kFlak, 0, {15, 15}); // 7 tiles away: out of range
    s.automated[1] = {4};
    s.deployBudget[1] = 1000;
    const BattleState initial = s;
    BattleServer server(s, {}, 1);
    std::vector<Event> all = server.handle(AutomatedRequest{1, 4, {{8, 8}}});
    const AutomatedArrived* arrived = first<AutomatedArrived>(all);
    CHECK(arrived && arrived->outcome == 2);
    CHECK(count<UnitFiredAA>(all) == 1 && first<UnitFiredAA>(all)->unit == light && first<UnitFiredAA>(all)->ap == 70);
    CHECK(server.state().unit(light).armor == 100 - 57);
    checkReplica(initial, all, server.state());

    // Shot down: the weakest units fire first until the armor is used up
    // (25 + 25 >= 40), the stronger flak does not fire; no effect.
    BattleState d = world(open(16));
    add(d, kTank, 1, {0, 0});
    const int strong = add(d, kFlak, 0, {8, 9});
    const int l1 = add(d, kLightFlak, 0, {8, 8});
    const int l2 = add(d, kLightFlak, 0, {9, 8});
    d.automated[1] = {4};
    d.deployBudget[1] = 1000;
    BattleServer s2(d, {}, 1);
    std::vector<Event> ev = s2.handle(AutomatedRequest{1, 4, {{8, 8}}});
    CHECK(first<AutomatedArrived>(ev) && first<AutomatedArrived>(ev)->outcome == 3);
    std::vector<int> fired;
    for (const Event& e : ev)
        if (const UnitFiredAA* f = std::get_if<UnitFiredAA>(&e)) fired.push_back(f->unit);
    CHECK(fired.size() == 2 && fired[0] == l1 && fired[1] == l2);
    CHECK(s2.state().unit(strong).ap == 100 && s2.state().unit(l1).armor == 100);
    CHECK(count<UnitDamaged>(ev) == 0 && s2.state().deployBudget[1] == 850); // paid anyway

    // AP damage (gas, area 3): 301 at the centre.
    BattleState g = world(open(10));
    add(g, kTank, 1, {0, 0});
    const int target = add(g, kAlienTank, 0, {5, 5});
    g.automated[1] = {5};
    g.deployBudget[1] = 100;
    BattleServer s3(g, {}, 1);
    s3.handle(AutomatedRequest{1, 5, {{5, 5}}});
    CHECK(s3.state().unit(target).ap == 0 && s3.state().unit(target).negAp == 100 && s3.state().unit(target).armor == 50);
}

void testDrops() {
    // Four troopers with the slot facings; a hidden armed mine on slot 1
    // explodes and that slot fails.
    BattleState s = world(open(12));
    add(s, kTank, 1, {0, 0});
    BattleMine mine;
    mine.pos = {5, 4}; // slot 1: (-1, -2) from (6, 6)
    mine.hidden = {false, true, true, true, true};
    s.addMine(mine);
    s.automated[1] = {15};
    s.deployBudget[1] = 500;
    const BattleState initial = s;
    BattleServer server(s, {}, 1);
    std::vector<Event> all = server.handle(AutomatedRequest{1, 15, {{6, 6}}});
    CHECK(count<MineExploded>(all) == 1 && count<UnitAdded>(all) == 3);
    std::vector<const UnitAdded*> added;
    for (const Event& e : all)
        if (const UnitAdded* a = std::get_if<UnitAdded>(&e)) added.push_back(a);
    CHECK(added.size() == 3 && same(added[0]->pos, {8, 8}) && added[0]->facing == 2 && added[0]->owner == 1);
    CHECK(same(added[1]->pos, {5, 7}) && added[1]->facing == 4 && same(added[2]->pos, {8, 4}) && added[2]->facing == 0);
    // Dropped units can act at once.
    const BattleUnit& dropped = server.state().units().back();
    CHECK(dropped.type->typeId == kTrooper && dropped.ap == dropped.type->actionPoints);
    checkReplica(initial, all, server.state());

    // Damaged by AA: half the units.
    BattleState d = world(open(12));
    add(d, kTank, 1, {0, 0});
    add(d, kLightFlak, 0, {10, 10});
    d.automated[1] = {15};
    d.deployBudget[1] = 500;
    BattleServer s2(d, {}, 1);
    std::vector<Event> ev = s2.handle(AutomatedRequest{1, 15, {{6, 6}}});
    CHECK(first<AutomatedArrived>(ev) && first<AutomatedArrived>(ev)->outcome == 2 && count<UnitAdded>(ev) == 2);
}

void testSupport() {
    // Bridge: a river row ". . r w w r . ." gives one bridge site; deep
    // water needs a road next to it.
    std::vector<std::string> rows = open(8);
    rows[3] = "..rwwr..";
    BattleState s = world(rows);
    s.bridgeSites = findBridgeSites(s.size(), [&](Point p) { return s.classAt(p); });
    CHECK(s.bridgeSites.size() == 4 && s.isBridgeSite({3, 3}) && !s.isBridgeSite({3, 2}));
    add(s, kTank, 1, {0, 0});
    s.automated[1] = {1, 2};
    s.deployBudget[1] = 100;
    BattleMine mine;
    mine.pos = {6, 6};
    mine.hidden = {false, true, true, true, true};
    s.addMine(mine);
    const BattleState initial = s;
    BattleServer server(s, {}, 1);
    std::vector<Event> all = server.handle(AutomatedRequest{1, 1, {{3, 3}}});
    CHECK(count<RequestRejected>(all) == 1);
    std::vector<Event> ev = server.handle(AutomatedRequest{1, 1, {{2, 3}}});
    all.insert(all.end(), ev.begin(), ev.end());
    CHECK(count<BridgeBuilt>(ev) == 1 && server.state().roadTypeAt({2, 3}) == 1);
    CHECK(automatedTargetOk(server.state(), 1, rules().automated.at(1), {3, 3}, true));
    // Mine detection: a 10 x 10 square (x - 5 ... x + 4) for every human slot.
    ev = server.handle(AutomatedRequest{1, 2, {{1, 1}}});
    all.insert(all.end(), ev.begin(), ev.end());
    CHECK(count<MinesRevealed>(ev) == 0); // (6, 6) is outside x - 5 ... x + 4
    ev = server.handle(AutomatedRequest{1, 2, {{3, 3}}});
    all.insert(all.end(), ev.begin(), ev.end());
    CHECK(count<MinesRevealed>(ev) == 1 && !server.state().mine(0).hidden[1] && !server.state().mine(0).hidden[4]);
    CHECK(server.state().mine(0).alive);
    checkReplica(initial, all, server.state());

    // Transport: picked up and put down with no AP left.
    BattleState t = world(open(12));
    const int tank = add(t, kTank, 1, {1, 1});
    const int alien = add(t, kAlienTank, 0, {9, 9});
    t.automated[1] = {0};
    t.deployBudget[1] = 200;
    const BattleState tInitial = t;
    BattleServer s2(t, {}, 1);
    std::vector<Event> tall = s2.handle(AutomatedRequest{1, 0, {{9, 9}, {5, 5}}}); // not his unit
    CHECK(count<RequestRejected>(tall) == 1);
    ev = s2.handle(AutomatedRequest{1, 0, {{1, 1}, {9, 9}}}); // occupied destination
    CHECK(count<RequestRejected>(ev) == 1);
    ev = s2.handle(AutomatedRequest{1, 0, {{1, 1}, {5, 5}}});
    tall.insert(tall.end(), ev.begin(), ev.end());
    CHECK(count<AutomatedArrived>(ev) == 2 && count<UnitTransported>(ev) == 2);
    CHECK(same(s2.state().unit(tank).pos, {5, 5}) && s2.state().unit(tank).availableAp() == 0 &&
          s2.state().unit(tank).armor == 50 && s2.state().unitAt({1, 1}) < 0);
    CHECK(s2.state().deployBudget[1] == 170);
    checkReplica(tInitial, tall, s2.state());
    (void)alien;

    // AA at the destination only: damaged, armor x (40 - 25) / 40.
    BattleState a = world(open(16));
    const int carried = add(a, kTank, 1, {1, 1});
    add(a, kLightFlak, 0, {12, 12});
    a.automated[1] = {0};
    a.deployBudget[1] = 200;
    BattleServer s3(a, {}, 1);
    ev = s3.handle(AutomatedRequest{1, 0, {{1, 1}, {10, 10}}});
    std::vector<int> outcomes;
    for (const Event& e : ev)
        if (const AutomatedArrived* x = std::get_if<AutomatedArrived>(&e)) outcomes.push_back(x->outcome);
    CHECK(outcomes.size() == 2 && outcomes[0] == 1 && outcomes[1] == 2);
    CHECK(same(s3.state().unit(carried).pos, {10, 10}) && s3.state().unit(carried).armor == 50 * 15 / 40);
}

void testAutomatedAi() {
    // Scheduled rows are called in the AI turn whose T equals TurnArrival:
    // an alien drop and a human bomber (AI-owned); the human transport is
    // dropped by the race rule; a later row waits.
    BattleState s = world(open(16));
    add(s, kTank, 1, {0, 15});
    s.scheduledAutomated = {{14, {5, 5}, {5, 5}, 1}, {4, {14, 14}, {14, 14}, 1}, {0, {14, 14}, {3, 3}, 1}, {14, {9, 9}, {9, 9}, 3}};
    startBattle(s);
    const BattleState initial = s;
    BattleServer server(s, {}, 1);
    std::vector<Event> all = server.start();
    std::vector<Event> ev = server.handle(EndTurnRequest{1});
    all.insert(all.end(), ev.begin(), ev.end());
    std::vector<int> types;
    for (const Event& e : ev)
        if (const AutomatedArrived* a = std::get_if<AutomatedArrived>(&e)) types.push_back(a->typeId);
    CHECK(types.size() == 2 && types[0] == 14 && types[1] == 4);
    int alienDrops = 0;
    for (const Event& e : ev)
        if (const UnitAdded* a = std::get_if<UnitAdded>(&e)) alienDrops += a->owner == 0 && a->typeId == kAlienTank;
    CHECK(alienDrops == 4);
    for (const Event& e : ev)
        if (const AutomatedArrived* a = std::get_if<AutomatedArrived>(&e)) CHECK(a->owner == 0);
    checkReplica(initial, all, server.state());

    // A random call (6.1) with strategic AI info: the medic goes to the most
    // damaged alien unit, if the AI owns the prerequisite building.
    BattleState r = world(open(16));
    add(r, kTank, 1, {0, 15});
    const int hurt = add(r, kAlienTank, 0, {8, 8});
    r.unit(hurt).armor = 10;
    r.unit(hurt).order = {4, 99, 100, 65535, {}, {}}; // keep it still
    BuildingType lab;
    lab.id = 250;
    lab.armor = 100;
    r.addBuilding({1000002, &lab, 0, 100, {12, 1}});
    r.aiSupport = {true, 100, 1000, {10}};
    startBattle(r);
    BattleServer rs(r, {}, 1);
    ev = aiTurn(rs);
    const AutomatedArrived* medic = first<AutomatedArrived>(ev);
    CHECK(medic && medic->typeId == 10 && same(medic->tile, {8, 8}));
    CHECK(rs.state().unit(hurt).armor == 50);
    CHECK(server.state().scheduledAutomated.size() == 1 && server.state().scheduledAutomated[0].turn == 3);
}

} // namespace

// A state on a rule set of its own (the tests that change unit types).
BattleState worldWith(const GlobalRules& r, const std::vector<std::string>& rows) {
    TerrainMap t;
    t.size = static_cast<int>(rows.size());
    for (const std::string& row : rows)
        for (const char c : row) t.tiles.push_back(std::string(1, c));
    BattleState s(r, t);
    s.players = {0, 1, 2};
    s.currentPlayer = 1;
    return s;
}

int addWith(BattleState& s, const GlobalRules& r, int type, int owner, Point p) {
    const UnitType& t = r.units.at({type, 0});
    return s.addUnit({100 + static_cast<int>(s.units().size()), &t, owner, t.actionPoints, 0, t.armor, p, 0});
}

int counter(const std::map<int, int>& m, int type) {
    const auto it = m.find(type);
    return it == m.end() ? 0 : it->second;
}

void testStatistics() {
    // A shot: the shooter's owner gets the kill; the computer player has no
    // record (battle-flow.md 5.3).
    BattleState s = world(open(12));
    const int tank = add(s, kTank, 1, {0, 0});
    const int enemy = add(s, kTank, 0, {3, 0});
    s.unit(enemy).armor = 5;
    BuildingType depot;
    depot.id = 77;
    depot.armor = 5;
    const int building = s.addBuilding({500, &depot, 0, 5, {3, 3}});
    s.addWall({0, 2, 5, {0, 3}});
    const BattleState initial = s;
    BattleServer server(s, hotSeat(), 1);
    Script rng;
    server.random = [&](int n) { return rng(n); };
    std::vector<Event> all = server.handle(ShootRequest{1, {{tank, {}}}, {3, 0}, false});
    const UnitDestroyed* dead = first<UnitDestroyed>(all);
    CHECK(dead && dead->cause.attacker == 1 && dead->cause.automatedType == -1);
    const BattleStats& st = server.state().stats[1];
    CHECK(counter(st.unitsDestroyed, kTank) == 1 && st.unitsLost.empty());
    CHECK(server.state().stats[0].unitsLost.empty() && server.state().stats[0].unitsDestroyed.empty());
    // A building counts; a wall does not.
    std::vector<Event> ev = server.handle(ShootRequest{1, {{tank, {}}}, {3, 3}, false});
    all.insert(all.end(), ev.begin(), ev.end());
    CHECK(!server.state().building(building).alive && counter(st.buildingsDestroyed, 77) == 1);
    ev = server.handle(ShootRequest{1, {{tank, {}}}, {0, 3}, false});
    all.insert(all.end(), ev.begin(), ev.end());
    CHECK(count<WallDestroyed>(ev) == 1 && server.state().stats[2].buildingsLost.empty() &&
          counter(st.buildingsDestroyed, 77) == 1 && BattleStats::total(st.unitsDestroyed) == 1);
    // Every copy of the state counts the same.
    BattleState replica = initial;
    for (const Event& e : all) applyEvent(replica, e);
    CHECK(replica.stats[1].unitsDestroyed == st.unitsDestroyed && replica.stats[1].buildingsDestroyed == st.buildingsDestroyed);

    // The computer player's kill is only the victim's loss.
    BattleState a = world(open(10));
    const int ai = add(a, kTank, 0, {0, 0});
    const int victim = add(a, kTank, 1, {2, 0});
    a.unit(victim).armor = 5;
    a.currentPlayer = 0;
    BattleServer s2(a, hotSeat(), 1);
    s2.random = [&](int n) { return rng(n); };
    s2.handle(ShootRequest{0, {{ai, {}}}, {2, 0}, false});
    CHECK(!s2.state().unit(victim).alive && counter(s2.state().stats[1].unitsLost, kTank) == 1 &&
          s2.state().stats[1].unitsDestroyed.empty());

    // Area damage: own units in the blast count as a loss and as a kill
    // (no same-side check outside death match).
    for (const bool deathMatch : {false, true}) {
        BattleState b = world(open(10));
        b.computerPlayer = !deathMatch;
        const int foe = deathMatch ? 2 : 0;
        const int art = add(b, kArtillery, 1, {0, 0});
        add(b, kTrooper, foe, {5, 0});
        add(b, kTrooper, foe, {6, 1});
        const int own = add(b, kTrooper, 1, {4, 0});
        CombatOptions o = hotSeat();
        o.coop = !deathMatch; // death match: the two players are enemies
        BattleServer s3(b, o, 1);
        s3.random = [&](int n) { return rng(n); };
        rng.values = {0, 4};
        s3.handle(ShootRequest{1, {{art, {}}}, {5, 0}, false});
        CHECK(!s3.state().unit(own).alive);
        const BattleStats& b1 = s3.state().stats[1];
        CHECK(counter(b1.unitsLost, kTrooper) == 1);
        // Death match credits no friendly kills.
        CHECK(counter(b1.unitsDestroyed, kTrooper) == (deathMatch ? 2 : 3));
        CHECK(counter(s3.state().stats[2].unitsLost, kTrooper) == (deathMatch ? 2 : 0));
    }

    // A mine: its owner gets the kill.
    BattleState m = world(open(8));
    const int walker = add(m, kTank, 1, {0, 0});
    m.unit(walker).armor = 10;
    BattleMine mine{0, 2, {2, 0}};
    mine.hidden[1] = true;
    m.addMine(mine);
    BattleServer s4(m, hotSeat(), 1);
    s4.random = [&](int n) { return rng(n); };
    rng.values = {9};
    s4.handle(MoveRequest{1, {{walker, planMove(s4.state(), walker, {4, 0}).path}}});
    CHECK(!s4.state().unit(walker).alive && counter(s4.state().stats[2].unitsDestroyed, kTank) == 1 &&
          counter(s4.state().stats[1].unitsLost, kTank) == 1);
}

void testScore() {
    GlobalRules r = makeRules();
    BuildingType depot;
    depot.id = 77;
    depot.cost = 100;
    r.buildings[77] = depot;
    r.units[{kTank, 1}] = r.units[{kTank, 0}]; // a second tech level of the same type
    r.units[{kTank, 1}].techLevel = 1;
    BattleState s = worldWith(r, open(6));
    s.stats[1].unitsDestroyed[kTank] = 2;     // 2 x 2 x 50
    s.stats[1].unitsLost[kTrooper] = 1;       // - 20
    s.stats[1].buildingsDestroyed[77] = 1;    // + 100
    s.stats[2].unitsLost[kTank] = 3;          // negative: shown as 0
    s.stats[2].buildingsDestroyed[77] = 1;
    CHECK(battleScore(s, 1) == 280);
    CHECK(battleScore(s, 2) == 0);
    CHECK(battleScore(s, 0) == 0);
    // The original's number: units destroyed only, once per Unit Types row.
    CHECK(originalBattleScore(s, 1) == 200);
    s.stats[1].unitsDestroyed[kTank] = 1000; // clamped for display
    CHECK(battleScore(s, 1) == 65535);
    s.stats[1].unitsDestroyed[kTank] = 2;
    const std::vector<PlayerResult> rows = battleResults(s);
    CHECK(rows.size() == 2 && rows[0].player == 1 && rows[1].player == 2);
    CHECK(rows[0].unitsDestroyed == 2 && rows[0].unitsLost == 1 && rows[0].buildingsDestroyed == 1 &&
          rows[0].buildingsLost == 0 && rows[0].score == 280);
    CHECK(rows[1].unitsLost == 3 && rows[1].buildingsDestroyed == 1 && rows[1].score == 0);

    // The result message carries the rows; applyEvent stores them.
    BattleState b = world(open(8));
    const int tank = add(b, kTank, 1, {0, 0});
    const int enemy = add(b, kTank, 0, {3, 0});
    b.unit(enemy).armor = 5;
    b.objective = {6, 0, {}, {}}; // Destroy All Except: won when no AI unit is left
    b.players = {0, 1};
    BattleServer server(b, hotSeat(), 1);
    Script rng;
    server.random = [&](int n) { return rng(n); };
    const std::vector<Event> ev = server.handle(ShootRequest{1, {{tank, {}}}, {3, 0}, false});
    const BattleOver* over = first<BattleOver>(ev);
    CHECK(over && over->success && over->results.size() == 1 && over->results[0].unitsDestroyed == 1 &&
          over->results[0].score == 100);
    CHECK(server.state().results.size() == 1 && server.state().results[0].score == 100);
}

void testSurrender() {
    // Skirmish / campaign province battle: lost, loser 5 (battle-flow.md 5.6).
    BattleState s = world(open(6));
    add(s, kTank, 1, {0, 0});
    add(s, kTank, 0, {5, 5});
    s.objective = {0, 0, {}, {{3, 3}}};
    s.addMine({0, 0, {3, 3}}); // not cleared yet
    s.turnLimit = 10;
    s.players = {0, 1};
    BattleServer server(s, hotSeat(), 1);
    std::vector<Event> ev = server.handle(CheatRequest{1, false});
    const BattleOver* over = first<BattleOver>(ev);
    CHECK(over && !over->success && over->loser == 5 && over->results.size() == 1 && server.state().over);
    CHECK(count<RequestRejected>(server.handle(EndTurnRequest{1})) == 1);

    // City battles (kind 7) ignore the flag.
    BattleState c = world(open(6));
    add(c, kTank, 1, {0, 0});
    add(c, kTank, 0, {5, 5});
    BuildingType hq;
    c.addBuilding({1, &hq, 1, 100, {2, 2}});
    c.objective = {7, 99, {}, {}};
    BattleServer s2(c, hotSeat(), 1);
    ev = s2.handle(CheatRequest{1, false});
    CHECK(count<RequestRejected>(ev) == 1 && count<BattleOver>(ev) == 0 && !s2.state().over);

    // Death match: success is forced to 1, loser 5: everyone sees the ranking.
    BattleState d = world(open(6));
    add(d, kTank, 1, {0, 0});
    add(d, kTank, 2, {5, 5});
    d.computerPlayer = false;
    d.objective.kind = 10;
    d.turnLimit = 10;
    BattleServer s3(d, hotSeat(), 1);
    ev = s3.handle(CheatRequest{2, false});
    over = first<BattleOver>(ev);
    CHECK(over && over->success && over->loser == 5 && over->results.size() == 2);

    // The kind's own rule decides before the flags: a won Destroy stays won.
    BattleState w = world(open(6));
    add(w, kTank, 1, {0, 0});
    w.objective = {0, 0, {}, {}};
    BattleServer s4(w, hotSeat(), 1);
    ev = s4.handle(CheatRequest{1, false});
    over = first<BattleOver>(ev);
    CHECK(over && over->success);
}

void testJammer() {
    // Jammer 0: the mover never draws reaction fire (tactical-movement.md 5).
    GlobalRules r = makeRules();
    r.units[{kTank, 0}].jammer = 0;
    BattleState s = worldWith(r, open(12));
    const int mover = addWith(s, r, kTank, 1, {0, 5});
    addWith(s, r, kTank, 0, {8, 0});
    BattleServer server(s, hotSeat(), 1);
    const std::vector<Event> ev = server.handle(MoveRequest{1, {{mover, planMove(server.state(), mover, {5, 5}).path}}});
    CHECK(count<UnitShot>(ev) == 0 && same(server.state().unit(mover).pos, {5, 5}));

    // The hit chance at a unit of the player whose turn it is, scaled by
    // its Jammer (tactical-combat.md 3.2).
    r.units[{kTank, 0}].jammer = 10;
    BattleState h = worldWith(r, open(10));
    const int watcher = addWith(h, r, kTank, 0, {0, 0});
    const int target = addWith(h, r, kTank, 1, {2, 0});
    h.currentPlayer = 1;
    CHECK(hitChance(h, watcher, h.unit(target).pos) == 8); // 80 x 10 / 100
    h.currentPlayer = 0;
    CHECK(hitChance(h, watcher, h.unit(target).pos) == 80);
    h.currentPlayer = 1;
    h.unit(target).pos = {1, 0};
    CHECK(hitChance(h, watcher, {1, 0}) == 101); // adjacent: no roll
}

// A staircase corridor through mountains: up segments of 4 tiles, right
// segments of 3, starting at the bottom left. Returns the last tile.
Point staircase(std::vector<std::string>& rows, int stairs) {
    const int n = static_cast<int>(rows.size());
    for (std::string& row : rows) row.assign(static_cast<std::size_t>(n), 'm');
    Point p{1, n - 2};
    auto carve = [&](Point q) { rows[static_cast<std::size_t>(q.y)][static_cast<std::size_t>(q.x)] = '.'; };
    carve(p);
    for (int k = 0; k < stairs; ++k) {
        for (int i = 0; i < 3; ++i) carve(p = {p.x + 1, p.y});
        for (int i = 0; i < 4; ++i) carve(p = {p.x, p.y - 1});
    }
    return p;
}

void testAiSearch() {
    // The window: only on maps of 57 tiles or more, outside the central
    // 12 x 12 square (tactical-movement.md 3.1).
    CHECK(!aiSearchWindowed(56, {0, 0}));
    CHECK(aiSearchWindowed(60, {0, 0}));
    CHECK(!aiSearchWindowed(60, {24, 24}) && !aiSearchWindowed(60, {35, 35}));
    CHECK(aiSearchWindowed(60, {23, 24}) && aiSearchWindowed(60, {36, 30}));

    // 42 x 42 window pushed back into the map: columns 1 ... 42 for a unit
    // at the left edge.
    BattleState s = world(open(60));
    const int unit = add(s, kTank, 0, {0, 5});
    const PathMap normal(s, unit);
    const PathMap ai(s, unit, nullptr, true);
    CHECK(normal.reachable({50, 5}) && !ai.reachable({50, 5}));
    CHECK(ai.reachable({42, 5}) && !ai.reachable({43, 5}));
    // A unit in the centre searches the whole map.
    const int central = add(s, kTank, 0, {30, 30});
    CHECK(PathMap(s, central, nullptr, true).reachable({58, 58}));

    // The round cap (7 rounds) also applies on small maps; boats are exempt.
    std::vector<std::string> rows(40);
    const Point end = staircase(rows, 9);
    BattleState m = world(rows);
    const int climber = add(m, kTank, 0, {1, 38});
    CHECK(PathMap(m, climber).reachable(end));
    CHECK(!PathMap(m, climber, nullptr, true).reachable(end));
    CHECK(PathMap(m, climber, nullptr, true).reachable({4, 34}));
}

void testOverwatchReserve() {
    BattleState s = world(open(16));
    const int tank = add(s, kTank, 1, {0, 0}); // AP 100, shot 30
    const BattleUnit& u = s.unit(tank);
    CHECK(movementBudget(u, false) == 100 && movementBudget(u, true) == 70);
    s.unit(tank).ap = 20; // below one shot: nothing is kept
    CHECK(movementBudget(s.unit(tank), true) == 20);
    s.unit(tank).ap = 100;
    // The planner cuts the path to the budget.
    CHECK(planMove(s, tank, {12, 0}, false).affordable == 10);
    CHECK(planMove(s, tank, {12, 0}, true).affordable == 7);
    const std::vector<PlannedMove> g = planGroupMove(s, {tank}, {12, 0}, true);
    CHECK(g.size() == 1 && g[0].affordable == 7);
}

void testGroupRules() {
    // A human order needs a clicked tile the unit can stop on.
    BattleState s = world({"......", "......", "...m..", "......", "......", "......"});
    const int a = add(s, kTank, 1, {0, 0});
    CHECK(planGroupMove(s, {a}, {3, 2}).empty());
    Point dest;
    CHECK(groupTile(s, s.unit(a), {3, 2}, PlanContext{}, dest) && same(dest, {2, 2})); // the computer player's Goto
    // A claimed tile: the replacement order is (X-1, Y), (X, Y-1), (X, Y+1), (X+1, Y), then the diagonals.
    const int b = add(s, kTank, 1, {0, 1});
    const int c = add(s, kTank, 1, {0, 2});
    std::vector<PlannedMove> g = planGroupMove(s, {a, b, c}, {4, 4});
    CHECK(g.size() == 3 && same(g[0].target, {4, 4}) && same(g[1].target, {3, 4}) && same(g[2].target, {4, 3}));

    // The tile where a unit really stops is claimed, not the clicked one.
    BattleState far = world(open(25));
    const int f1 = add(far, kTank, 1, {0, 0});
    const int f2 = add(far, kTank, 1, {0, 1});
    g = planGroupMove(far, {f1, f2}, {20, 0});
    CHECK(g.size() == 2 && same(g[0].path[static_cast<std::size_t>(g[0].affordable - 1)].pos, {10, 0}) &&
          same(g[1].target, {20, 0}));

    // Repeat passes: a unit blocked by a later member moves on the second pass.
    BattleState corridor = world({"........", "mmmmmmmm", "mmmmmmmm", "mmmmmmmm", "mmmmmmmm", "mmmmmmmm", "mmmmmmmm", "mmmmmmmm"});
    const int back = add(corridor, kTank, 1, {0, 0});
    const int front = add(corridor, kTank, 1, {1, 0});
    g = planGroupMove(corridor, {back, front}, {5, 0});
    CHECK(g.size() == 2 && g[0].unit == front && same(g[0].target, {5, 0}) && g[1].unit == back &&
          same(g[1].target, {4, 0}));
    // MOVE ALL makes one pass.
    g = planGroupOrders(corridor, {{back, {5, 0}}, {front, {5, 0}}}, false, false);
    CHECK(g.size() == 1 && g[0].unit == front);
}

void testMoveToFire() {
    BattleState s = world(open(16));
    const int tank = add(s, kTank, 1, {0, 0}); // range 6, AP 100, shot 30
    const int enemy = add(s, kTank, 0, {12, 0});
    s.unit(enemy).ap = 0; // no reaction fire
    std::vector<PathStep> path;
    CHECK(planMoveToFire(s, tank, {12, 0}, nullptr, path) && same(path.back().pos, {6, 0}) && path.size() == 6);
    // The path must cost less than the AP and leave a shot.
    s.unit(tank).ap = 50;
    CHECK(!planMoveToFire(s, tank, {12, 0}, nullptr, path) && path.empty());
    s.unit(tank).ap = 100;
    // The shot request carries the path; the server walks and fires.
    BattleServer server(s, hotSeat(), 1);
    Script rng;
    server.random = [&](int n) { return rng(n); };
    ShootRequest r{1, {{tank, {}}}, {12, 0}, false};
    CHECK(planMoveToFire(server.state(), tank, {12, 0}, nullptr, r.orders[0].path) && r.orders[0].path.size() == 6);
    const std::vector<Event> ev = server.handle(r);
    CHECK(count<UnitMoved>(ev) == 1 && count<UnitShot>(ev) == 1 && server.state().unit(enemy).armor < 50);
}

void testProgress() {
    // The red number of the turn counter: the Hold Position counter while
    // T <= last held turn + 1 (tactical-interface.md 2.5).
    BattleState h = world(open(6));
    h.objective.kind = 1;
    h.holdCount = 2;
    h.lastHeldTurn = 3;
    h.turn = 4;
    CHECK(objectiveProgress(h) == 2);
    h.turn = 5;
    CHECK(objectiveProgress(h) == 0);
    h.lastHeldTurn = -1;
    h.holdCount = 0;
    CHECK(objectiveProgress(h) == 0);
    h.objective.kind = 0;
    h.holdCount = 3;
    CHECK(objectiveProgress(h) == 0);
}

int main() {
    testTiles();
    testPaths();
    testApCut();
    testLineAndShots();
    testDamage();
    testInterruptions();
    testTurns();
    testObjectives();
    testReinforcements();
    testAi();
    testDeployment();
    testSides();
    testAutomatedCalls();
    testAntiAir();
    testDrops();
    testSupport();
    testAutomatedAi();
    testStatistics();
    testScore();
    testSurrender();
    testJammer();
    testAiSearch();
    testOverwatchReserve();
    testGroupRules();
    testMoveToFire();
    testProgress();
    if (failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    std::puts("all battle tests passed");
    return EXIT_SUCCESS;
}
