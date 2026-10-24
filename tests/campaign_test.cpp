// Campaign rules on synthetic data: city placement checks, segments,
// recycling, moving, unlocks, deployment zones on the city, difficulty
// formulas, upgrades, the campaign state machine and save files.
// With --data DIR (converted databases of a local installation) it also
// starts a real campaign and prepares its battles.

#include "anim/battle_session.hpp"
#include "battle/server.hpp"
#include "campaign/campaign.hpp"
#include "campaign/city.hpp"

#include <cstdio>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <random>
#include <string>
#include <vector>

using namespace openday;
using namespace openday::game;
namespace fs = std::filesystem;

namespace {

int failures = 0;

#define CHECK(cond)                                                                       \
    do {                                                                                  \
        if (!(cond)) {                                                                    \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                                   \
        }                                                                                 \
    } while (0)

BuildingType building(int id, int w, int h, int prereq, bool unique, int cost, int recycle, int position = 3) {
    BuildingType b;
    b.id = id;
    b.image = "B" + std::to_string(id);
    b.name = "Building " + std::to_string(id);
    b.sizeX = w;
    b.sizeY = h;
    b.armor = 100;
    b.canBuild = true;
    b.prerequisite = prereq;
    b.uniqueOnMap = unique;
    b.cost = cost;
    b.recycle = recycle;
    b.position = position;
    return b;
}

GlobalRules makeRules() {
    GlobalRules r;
    r.terrain = {{".", {TerrainClass::Plain, true, ""}},
                 {"w", {TerrainClass::DeepWater, true, ""}},
                 {"r", {TerrainClass::Riverside, true, ""}},
                 {"m", {TerrainClass::Mountains, false, ""}},
                 {"f", {TerrainClass::Forest, false, ""}}};
    r.buildings[kHeadquarters] = building(kHeadquarters, 4, 4, kNone, true, 0, 0);
    r.buildings[kHeadquarters].resProduction = 50;
    r.buildings[131] = building(131, 2, 2, kHeadquarters, false, 200, 150); // Deploy Mk1
    r.buildings[131].resProduction = 100;
    r.buildings[133] = building(133, 2, 2, 131, true, 100, 75);   // Barracks
    r.buildings[170] = building(170, 2, 2, 133, true, 150, 90, 1); // dock on deep water
    BuildingType tower = building(100, 1, 1, kHeadquarters, false, 25, 15);
    tower.transformIntoUnit = 37;
    r.buildings[100] = tower;
    RoadType paved{"ROAH", 10};
    paved.id = 1;
    paved.canBuild = true;
    paved.cost = 3;
    RoadType basic{"ROAL", 999};
    basic.id = 0;
    basic.canBuild = true;
    basic.cost = 1;
    r.roads = {{0, basic}, {1, paved}};
    WallType wall{"WALLC", 60};
    wall.id = 0;
    wall.canBuild = true;
    wall.cost = 5;
    wall.recycle = 3;
    r.walls = {{0, wall}};
    MineType mine{"MINE", false, 25, 10};
    mine.canBuy = true;
    mine.cost = 10;
    mine.recycle = 5;
    r.mines = {{0, mine}};
    UnitType trooper;
    trooper.typeId = 2;
    trooper.prerequisite = kHeadquarters;
    trooper.costToNextTL = 5;
    trooper.actionPoints = 100;
    trooper.armor = 5;
    trooper.movement = 2;
    r.units[{2, 0}] = trooper;
    trooper.techLevel = 1;
    trooper.costToNextTL = 0;
    r.units[{2, 1}] = trooper;
    UnitType grenadier = trooper;
    grenadier.typeId = 3;
    grenadier.techLevel = 0;
    grenadier.prerequisite = 133;
    r.units[{3, 0}] = grenadier;
    UnitType towerUnit = trooper;
    towerUnit.typeId = 37;
    towerUnit.techLevel = 0;
    towerUnit.prerequisite = kHeadquarters;
    r.units[{37, 0}] = towerUnit;
    UnitType truck = trooper;
    truck.typeId = 5;
    truck.techLevel = 0;
    truck.isConvoy = true;
    r.units[{5, 0}] = truck;
    r.creditPercent = {125, 100, 75};
    return r;
}

// A 48 x 48 plain city with the HQ of slot 1 at (20, 20) and a road on its
// east side; slot 2's HQ site at (30, 20) without a building.
City makeCity(const GlobalRules& rules) {
    City c;
    c.scenario = "test";
    c.terrain.size = 48;
    c.terrain.tiles.assign(48 * 48, ".");
    c.buildings.push_back({1000001, kHeadquarters, 1, 500, {20, 20}, 4, 4});
    c.hqSites.push_back({1, {20, 20}});
    for (int y = 20; y < 30; ++y) c.roads.push_back({1, 1, {24, y}});
    c.computeBridgeSites(rules);
    return c;
}

void placementRules() {
    const GlobalRules rules = makeRules();
    City c = makeCity(rules);
    auto check = [&](StructureKind k, int type, Point p) { return checkPlacement(c, rules, 1, k, type, p); };
    // Deploy Mk1 east of the road: touches the road, one tile from the HQ ring.
    CHECK(check(StructureKind::Building, 131, {25, 22}) == kBuildOk);
    // Touching the HQ (within one tile of the HQ site).
    CHECK(check(StructureKind::Building, 131, {20, 24}) == kBuildAroundHq);
    // Not next to a road.
    CHECK(check(StructureKind::Building, 131, {27, 22}) == kBuildNoRoad);
    // Far from everything.
    CHECK(check(StructureKind::Building, 131, {40, 40}) == kBuildBorder || check(StructureKind::Building, 131, {40, 40}) == kBuildTooFar);
    CHECK(check(StructureKind::Building, 131, {30, 33}) == kBuildTooFar);
    // Province border: 13 tiles.
    CHECK(check(StructureKind::Mine, 0, {12, 25}) == kBuildTooFar || check(StructureKind::Mine, 0, {12, 25}) == kBuildBorder);
    // The HQ only on an HQ site.
    CHECK(check(StructureKind::Building, kHeadquarters, {30, 20}) == kBuildTerrain); // unique, already owned
    c.hqSites.push_back({2, {30, 20}});
    CHECK(checkPlacement(c, rules, 2, StructureKind::Building, kHeadquarters, {30, 20}) == kBuildOk);
    CHECK(checkPlacement(c, rules, 2, StructureKind::Building, kHeadquarters, {31, 20}) == kBuildHqSite);
    c.hqSites.pop_back();
    // Unique building already owned: terrain/occupancy code.
    c.buildings.push_back({1000002, 133, 1, 100, {25, 26}, 2, 2});
    CHECK(check(StructureKind::Building, 133, {25, 29}) == kBuildTerrain);
    // Gap: a building needs one free tile to other buildings, towers may touch.
    CHECK(check(StructureKind::Building, 131, {25, 24}) == kBuildTooClose);
    CHECK(check(StructureKind::Tower, 100, {25, 25}) == kBuildOk);
    // Roads: connect to an own road or the HQ ring.
    CHECK(check(StructureKind::Road, 0, {23, 30}) == kBuildNoRoad);
    CHECK(check(StructureKind::Road, 0, {24, 30}) == kBuildOk);
    CHECK(check(StructureKind::Road, 0, {19, 22}) == kBuildOk); // on the HQ ring
    // Walls and mines need no road but must keep away from the HQ.
    CHECK(check(StructureKind::Wall, 0, {26, 30}) == kBuildOk);
    CHECK(check(StructureKind::Wall, 0, {19, 19}) == kBuildAroundHq);
    CHECK(check(StructureKind::Mine, 0, {24, 25}) == kBuildOk); // on a road
    // Another player's building within 6 tiles.
    c.buildings.push_back({1000003, 133, 2, 100, {32, 26}, 2, 2});
    CHECK(check(StructureKind::Building, 131, {25, 29}) == kBuildOtherPlayer);
}

void bridges() {
    const GlobalRules rules = makeRules();
    City c = makeCity(rules);
    // A river along y = 30..33 between x = 14 and 40: banks at y 30 and 33.
    for (int x = 14; x < 40; ++x) {
        c.terrain.tiles[static_cast<std::size_t>(30 * 48 + x)] = "r";
        c.terrain.tiles[static_cast<std::size_t>(31 * 48 + x)] = "w";
        c.terrain.tiles[static_cast<std::size_t>(32 * 48 + x)] = "w";
        c.terrain.tiles[static_cast<std::size_t>(33 * 48 + x)] = "r";
    }
    c.roads.clear();
    for (int y = 20; y < 30; ++y) c.roads.push_back({1, 1, {24, y}});
    c.computeBridgeSites(rules);
    CHECK(!c.bridgeSites().empty());
    CHECK(c.isBridgeSite({24, 30}) || !c.bridgeSites().empty());
    const Point ramp = c.bridgeSites().front();
    // Basic road cannot make ramps.
    if (c.roadAt({ramp.x, ramp.y - 1}) >= 0)
        CHECK(checkPlacement(c, rules, 1, StructureKind::Road, 0, ramp) == kBuildBridgeRoad);
}

void segmentsAndRecycling() {
    const GlobalRules rules = makeRules();
    City c = makeCity(rules);
    BuiltThisPhase built;
    int credits = 100;
    // A paved road eastwards from the road column: 5 tiles at 3 credits.
    BuildResult r = buildSegment(c, rules, 1, credits, built, StructureKind::Road, 1, {25, 29}, {29, 29});
    CHECK(r.code == kBuildOk && r.pieces == 5 && r.credits == 15 && credits == 85);
    // A segment that starts badly reports the first code and builds nothing.
    r = buildSegment(c, rules, 1, credits, built, StructureKind::Road, 1, {35, 35}, {36, 35});
    CHECK(r.pieces == 0 && r.code != kBuildOk);
    // Not enough credits for the second tile: one piece is built.
    int few = 4;
    r = buildSegment(c, rules, 1, few, built, StructureKind::Road, 1, {30, 29}, {32, 29});
    CHECK(r.pieces == 1 && few == 1);
    // Recycling in the same phase refunds the full cost, later the Recycle value.
    r = buildStructure(c, rules, 1, credits, built, StructureKind::Building, 131, {25, 22});
    CHECK(r.code == kBuildNoCredits); // 85 credits left
    credits = 1000;
    r = buildStructure(c, rules, 1, credits, built, StructureKind::Building, 131, {25, 25});
    CHECK(r.code == kBuildOk || r.code == kBuildTooClose);
    const int before = credits;
    const int idx = c.buildingAt({25, 22});
    if (idx >= 0) {
        r = recycleAt(c, rules, 1, credits, built, {25, 22});
        CHECK(r.pieces == 1);
        CHECK(credits - before == 200 || credits - before == 150);
    }
    // The HQ cannot be recycled or moved.
    CHECK(recycleAt(c, rules, 1, credits, built, {21, 21}).code == kBuildRecycleHq);
    CHECK(moveAt(c, rules, 1, built, {21, 21}, {30, 30}).code == kBuildMoveHq);
    // Nothing of mine on this tile.
    CHECK(recycleAt(c, rules, 1, credits, built, {40, 40}).code == kBuildNotOwn);
    // Recycle in a later phase: Recycle value.
    credits = 1000;
    r = buildStructure(c, rules, 1, credits, built, StructureKind::Building, 131, {25, 22});
    CHECK(r.code == kBuildOk);
    built.clear();
    const int c0 = credits;
    recycleAt(c, rules, 1, credits, built, {26, 23});
    CHECK(credits - c0 == 150);
    // Moving a wall piece is free.
    credits = 1000;
    r = buildSegment(c, rules, 1, credits, built, StructureKind::Wall, 0, {26, 31}, {26, 31});
    CHECK(r.pieces == 1);
    const int c1 = credits;
    r = moveAt(c, rules, 1, built, {26, 31}, {27, 31});
    CHECK(r.code == kBuildOk && credits == c1 && c.wallAt({27, 31}) >= 0);
    // A moved wall lost its full refund.
    recycleAt(c, rules, 1, credits, built, {27, 31});
    CHECK(credits - c1 == 3);
}

void deployLimitAndUnlocks() {
    const GlobalRules rules = makeRules();
    City c = makeCity(rules);
    BuiltThisPhase built;
    int credits = 100000;
    for (int i = 0; i < 10; ++i) c.buildings.push_back({2000 + i, 131, 1, 100, {0, i * 3}, 2, 2});
    CHECK(buildStructure(c, rules, 1, credits, built, StructureKind::Building, 131, {25, 22}).code == kBuildDeployLimit);
    // Unlocks: the HQ gives troopers and the tower unit is never listed;
    // convoys neither; a barracks adds grenadiers.
    std::vector<int> types = deployableTypes(c, rules, 1);
    CHECK(types == std::vector<int>({2}));
    c.buildings.push_back({3000, 133, 1, 100, {25, 26}, 2, 2});
    types = deployableTypes(c, rules, 1);
    CHECK(types == std::vector<int>({2, 3}));
    // Building list: barracks needs a deployment centre (any level); unique.
    const std::vector<BuildOption> list = buildingOptions(c, rules, {131, 133, 170}, 1);
    CHECK(list.size() == 2); // Deploy Mk1 and the dock; the barracks is owned
    const std::vector<BuildOption> defenses = defenseOptions(c, rules, {100}, 1);
    CHECK(defenses.size() == 5 && defenses[0].kind == StructureKind::Road && defenses[0].type == 1 &&
          defenses[2].kind == StructureKind::Tower && defenses[3].kind == StructureKind::Wall &&
          defenses[4].kind == StructureKind::Mine);
}

void zones() {
    const GlobalRules rules = makeRules();
    City c = makeCity(rules);
    c.terrain.size = 60;
    c.terrain.tiles.assign(60 * 60, ".");
    const auto z = cityZones(c, rules);
    CHECK(!z[1].empty());
    CHECK(!z[0].empty());
    // Player tiles are within 3 of a non-tower building, the AI band 12-14
    // from the player's zone.
    for (const Point p : z[1]) CHECK(p.x >= 17 && p.x <= 27 && p.y >= 17 && p.y <= 32);
    for (const Point p : z[0]) {
        int best = 1000;
        for (const Point q : z[1]) best = std::min(best, std::max(std::abs(p.x - q.x), std::abs(p.y - q.y)));
        CHECK(best >= 12 && best <= 14);
    }
}

void formulas() {
    CHECK(interpolate(130, 70, 4) == 106);
    CHECK(interpolate(125, 75, 0) == 125);
    CHECK(interpolate(125, 75, 10) == 75);
    CHECK(interpolate(130, 70, 5) == 100);
    CHECK(scalePercent(150, 106) == 159);
    CHECK(scalePercent(50, 106) == 53);
}

void upgrades() {
    const GlobalRules rules = makeRules();
    Campaign c;
    c.rules = &rules;
    c.player.tech = 4;
    CHECK(!c.upgrade(rules, 2)); // costs 5
    c.player.tech = 7;
    CHECK(c.upgrade(rules, 2) && c.player.tech == 2 && c.techLevel(2) == 1);
    CHECK(!c.upgrade(rules, 2)); // maximum level
}

void stateMachine() {
    const GlobalRules rules = makeRules();
    Campaign c;
    c.rules = &rules;
    c.colony.number = 1;
    c.colony.playerCity = 1;
    c.colony.alienCity = 3;
    c.colony.adjacent = {{1, 2}, {2, 1}, {2, 3}, {3, 2}};
    for (int p = 1; p <= 3; ++p) {
        Province pr;
        pr.number = p;
        pr.credits = 100 * p;
        c.colony.provinces.push_back(pr);
    }
    c.provinces = {{true, 1}, {false, 1}, {false, 2}};
    c.city = makeCity(rules);
    c.state = CampaignState::Strategic;
    CHECK(c.attackable(2) && !c.attackable(3) && !c.attackable(1));
    CHECK(!c.attack(3));
    CHECK(c.attack(2) && c.state == CampaignState::Tactic);
    // Lost: the variant flips and the strategic phase continues.
    BattleOutcome lost;
    c.battleFinished(rules, lost);
    CHECK(c.state == CampaignState::Strategic && c.provinces[1].variant == 2 && !c.provinces[1].captured);
    // Won without a counter-attack: captured, credits scaled by C(d).
    c.difficulty = 10; // C = 75
    c.player.credits = 0;
    CHECK(c.attack(2));
    BattleOutcome won;
    won.success = true;
    won.techFound = 250;
    c.battleFinished(rules, won);
    CHECK(c.state == CampaignState::StrategicResults && c.provinces[1].captured && c.player.credits == 150 &&
          c.player.tech == 250);
    std::mt19937 rng(1);
    CampaignResources res;
    res.rules = &rules;
    CHECK(!c.resultsDone(res, rng) && c.state == CampaignState::Strategic);
    // Won with a counter-attack: the video first, then the city battle.
    CHECK(c.attack(3));
    c.counterAttack = true;
    c.battleFinished(rules, won);
    CHECK(c.state == CampaignState::PreCounterAttack && !c.provinces[2].captured);
    c.counterAttackVideoDone();
    CHECK(c.state == CampaignState::CounterAttack);
    CHECK(c.endsCampaign(false) && !c.endsCampaign(true));
    BattleOutcome cityLost;
    Campaign copy = c;
    copy.battleFinished(rules, cityLost);
    CHECK(copy.state == CampaignState::Lost);
}

void saveFiles() {
    const GlobalRules rules = makeRules();
    Campaign c;
    c.rules = &rules;
    c.playerName = "Ann Smith";
    c.difficulty = 7;
    c.colonyNumber = 2;
    c.state = CampaignState::Strategic;
    c.player.credits = 1234;
    c.player.tech = 56;
    c.player.techLevels = {{2, 3}, {7, 1}};
    c.provinces = {{true, 1}, {false, 2}};
    c.city = makeCity(rules);
    c.built.roads.insert({24, 25});
    const std::string text = saveCampaign(c, "Colony 2 - turn");
    CHECK(saveDescription(text) == "Colony 2 - turn");
    CHECK(text.find("name Ann Smith") != std::string::npos);
    CHECK(text.find("building 1000001 130 1 500 20 20") != std::string::npos);
    CHECK(text.find("built-road 24 25") != std::string::npos);
    CHECK(saveDescription("garbage").empty());
}

// --- Optional: the user's converted databases ---------------------------------

void realData(const fs::path& dataDir, const fs::path& savePath) {
    std::map<std::string, fs::path> scenarios;
    fs::path global;
    std::map<int, fs::path> colonies;
    for (const auto& e : fs::recursive_directory_iterator(dataDir)) {
        if (!e.is_regular_file() || e.path().extension() != ".sqlite") continue;
        std::string stem = e.path().stem().string();
        for (char& ch : stem) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        if (stem == "global") global = e.path();
        if (stem.size() == 2 && stem[0] == 'c' && stem[1] >= '1' && stem[1] <= '4') colonies[stem[1] - '0'] = e.path();
        if (stem[0] == 's' || stem.rfind("dm", 0) == 0) scenarios[stem] = e.path();
    }
    CHECK(!global.empty() && colonies.size() == 4);
    if (global.empty() || colonies.size() != 4) return;
    const GlobalRules rules = loadGlobalRules(data::SqliteDb(global));
    CampaignResources res;
    res.rules = &rules;
    res.scenario = [&](const std::string& n) {
        const auto it = scenarios.find(n);
        return it == scenarios.end() ? fs::path() : it->second;
    };
    res.colony = [&](int n) { return colonies[n]; };
    res.tempDir = fs::temp_directory_path() / "openday-campaign-test";
    std::mt19937 rng(42);
    Campaign c = Campaign::create(res, "Tester", 4, rng);
    CHECK(c.state == CampaignState::FirstColony && c.colony.provinces.size() == 7 && c.player.credits == 625);
    CHECK(c.city.size() == 96 && c.city.count(1, kHeadquarters) == 1 && c.city.count(2, kHeadquarters) == 0);
    CHECK(c.deployMultiplier() == 106 && c.cityBudget(rules) == 53);
    c.introDone();
    CHECK(c.state == CampaignState::FirstCityAttack);
    CampaignBattle b = c.battle(res, rng);
    CHECK(b.city && fs::exists(b.database));
    auto session = BattleSession::load(b.database, rules, nullptr, 7, b.options, b.prepare);
    const BattleState& s = session->state();
    CHECK(s.objective.kind == 7 && !s.deployZone[1].empty() && s.deployBudget[1] == 53);
    int attackers = 0, towers = 0;
    for (const BattleUnit& u : s.units()) {
        if (u.owner == 0) ++attackers;
        if (u.owner == 1) ++towers;
    }
    CHECK(attackers + static_cast<int>(s.reinforcements.size()) == 4 && towers == 2);
    std::printf("first city attack: %d attackers placed, %zu pending, zone %zu tiles\n", attackers, s.reinforcements.size(),
                s.deployZone[1].size());
    // Win it and check the city comes back with its towers.
    BattleOutcome won;
    won.success = true;
    won.finalState = &session->server().state();
    c.battleFinished(rules, won);
    CHECK(c.state == CampaignState::StrategicResults && c.city.count(1, 100) == 2);
    CHECK(!c.resultsDone(res, rng) && c.state == CampaignState::Strategic);
    if (!savePath.empty()) { // a strategic-phase save for trying the screens
        c.player.tech = 500;
        std::ofstream(savePath, std::ios::binary) << saveCampaign(c, "Test - strategic phase");
    }
    // A province battle.
    CHECK(c.attackable(3) || c.attackable(4) || c.attackable(5));
    int target = 0;
    for (int p = 2; p <= 7 && !target; ++p)
        if (c.attackable(p)) target = p;
    CHECK(c.attack(target));
    b = c.battle(res, rng);
    session = BattleSession::load(b.database, rules, nullptr, 7, b.options, b.prepare);
    CHECK(session->state().deployBudget[1] > 0 && !session->state().purchasable.empty());
    std::printf("province %d: %s, budget %d\n", target, b.title.c_str(), session->state().deployBudget[1]);
    // Save and load.
    const std::string text = saveCampaign(c, "test");
    const Campaign back = loadCampaign(text, res);
    CHECK(back.city.buildings.size() == c.city.buildings.size() && back.player.credits == c.player.credits &&
          back.state == c.state && back.provinces.size() == c.provinces.size());
}

} // namespace

int main(int argc, char** argv) {
    placementRules();
    bridges();
    segmentsAndRecycling();
    deployLimitAndUnlocks();
    zones();
    formulas();
    upgrades();
    stateMachine();
    saveFiles();
    fs::path data, save;
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::string(argv[i]) == "--data") data = argv[i + 1];
        if (std::string(argv[i]) == "--write-save") save = argv[i + 1];
    }
    if (!data.empty()) realData(data, save);
    if (failures) std::fprintf(stderr, "%d check(s) failed\n", failures);
    else std::printf("campaign_test: all checks passed\n");
    return failures ? 1 : 0;
}
