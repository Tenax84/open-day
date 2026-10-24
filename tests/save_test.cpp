// Saving and loading (docs/mechanics/savegame.md) on synthetic data: the
// battle section round trip, an identical continuation with the restored
// random numbers, the resume rules, campaign files with and without a
// battle (old format included), the atomic write and the import of an
// original save slot.

#include "anim/battle_session.hpp"
#include "battle/battle_save.hpp"
#include "battle/server.hpp"
#include "campaign/campaign.hpp"
#include "campaign/savegame_import.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <variant>
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

enum TypeId { kTank = 1, kTrooper = 2, kAlien = 7 };

UnitType armed(int typeId, int movement, int ap, int armor, int range, int shotAp, int base, int random) {
    UnitType t;
    t.typeId = typeId;
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
    t.cost = 30;
    return t;
}

BuildingType buildingType(int id, int size, int armor, int techReward) {
    BuildingType b;
    b.id = id;
    b.image = "B" + std::to_string(id);
    b.sizeX = b.sizeY = size;
    b.armor = armor;
    b.techReward = techReward;
    b.cost = 100;
    return b;
}

GlobalRules makeRules() {
    GlobalRules r;
    r.terrain = {{".", {TerrainClass::Plain, true, ","}},
                 {",", {TerrainClass::Plain, true, ""}},
                 {"f", {TerrainClass::Forest, false, ""}}};
    r.roads = {{0, {"ROAL", 999}}};
    r.walls = {{0, {"WALLC", 60}}};
    r.mines = {{0, {"MINE", false, 25, 10}}};
    r.ruins = {{"WRECK", {true, true}}};
    r.ranges = {3, 6};
    r.damageEasy = 70;
    r.damageHard = 130;
    for (auto& row : r.aiPriority) row.fill(50);
    r.units[{kTank, 0}] = armed(kTank, 1, 100, 60, 6, 30, 12, 10);
    r.units[{kTank, 1}] = armed(kTank, 1, 120, 70, 6, 30, 14, 10);
    r.units[{kTank, 1}].techLevel = 1;
    r.units[{kTrooper, 0}] = armed(kTrooper, 2, 80, 20, 4, 40, 3, 1);
    UnitType alien = armed(kAlien, 1, 100, 50, 6, 30, 10, 10);
    alien.race = 2;
    r.units[{kAlien, 0}] = alien;
    r.buildings[130] = buildingType(130, 2, 300, 0);
    r.buildings[200] = buildingType(200, 1, 100, 5);
    return r;
}

const GlobalRules& rules() {
    static const GlobalRules r = makeRules();
    return r;
}

int add(BattleState& s, int type, int level, int owner, Point p) {
    const UnitType& t = rules().units.at({type, level});
    return s.addUnit({100 + static_cast<int>(s.units().size()), &t, owner, t.actionPoints, 0, t.armor, p, 0});
}

// A 16 x 16 battle with every kind of object, a dead unit and a destroyed
// building (gaps in the indices), past its first computer turn.
BattleState makeBattle() {
    TerrainMap t;
    t.size = 16;
    t.tiles.assign(256, ".");
    for (int x = 6; x < 10; ++x) t.tiles[static_cast<std::size_t>(10 * 16 + x)] = "f";
    BattleState s(rules(), t);
    add(s, kTank, 1, 1, {2, 2});
    add(s, kTrooper, 0, 1, {3, 2});
    const int dead = add(s, kTank, 0, 1, {4, 4});
    s.removeUnit(dead);
    const int a = add(s, kAlien, 0, 0, {12, 12});
    s.unit(a).order = {1, 99, 100, 65535, {{10, 12}, {9, 12}}, {}};
    const int b = add(s, kAlien, 0, 0, {13, 12});
    s.unit(b).order = {4, 99, 80, 65535, {}, {1000002}};
    const int gone = s.addBuilding({1000009, &rules().buildings.at(200), 0, 10, {14, 1}});
    s.removeBuilding(gone);
    s.addBuilding({1000001, &rules().buildings.at(130), 1, 250, {1, 6}});
    s.addBuilding({1000002, &rules().buildings.at(200), 0, 90, {11, 4}});
    s.addWall({0, 1, 30, {6, 6}, true, true});
    s.addRoad({0, 1, {7, 7}, true});
    s.addRoad({0, 1, {7, 8}, true});
    BattleMine m1;
    m1.owner = 0;
    m1.pos = {8, 3};
    m1.hidden = {false, true, true, true, true};
    s.addMine(m1);
    BattleMine m2;
    m2.owner = 1;
    m2.pos = {9, 9};
    m2.armed = false;
    m2.hidden = {true, false, false, false, false};
    s.addMine(m2);
    s.addRuin({"WRECK", {5, 5}});
    s.setTerrain({0, 0}, ",");
    s.objective.kind = 1; // Hold Position
    s.objective.turns = 5;
    s.objective.zone = {{2, 2}, {3, 2}, {2, 3}};
    s.turnLimit = 30;
    s.reinforcements.push_back({500, kAlien, {14, 14}, 3, true});
    s.reinforcements.push_back({501, kAlien, {14, 13}, 4, false});
    s.scheduledAutomated.push_back({4, {0, 0}, {3, 3}, 9});
    startBattle(s);
    s.players = {0, 1};
    s.deployZone[1] = {{1, 1}, {1, 2}};
    s.deployBudget[1] = 120;
    s.income[1] = 15;
    s.purchasable = {kTank, kTrooper};
    s.techLevels[1] = {{kTank, 1}};
    s.automated[1] = {3, 4};
    s.automatedUsed[1] = {3};
    s.aiSupport = {true, 0, 75, {}};
    s.stats[1].unitsLost[kTank] = 1;
    s.stats[1].buildingsDestroyed[200] = 1;
    return s;
}

BattleHeader makeHeader() {
    BattleHeader h;
    h.scenario = "s1-2-1";
    h.title = "A title";
    h.province = "Province";
    h.description = "Line 1##Line 2\r\nwith a \\ backslash";
    h.aftermath = "Won.";
    h.provinceNumber = 2;
    h.techLine = true;
    h.options.difficulty = 7;
    return h;
}

template <class T> int count(const std::vector<Event>& events) {
    int n = 0;
    for (const Event& e : events) n += std::holds_alternative<T>(e) ? 1 : 0;
    return n;
}

int unitIndex(const BattleState& s, int id) {
    for (std::size_t i = 0; i < s.units().size(); ++i)
        if (s.units()[i].alive && s.units()[i].id == id) return static_cast<int>(i);
    return -1;
}

// The same requests on two servers; unit indices are found by unit id.
void play(BattleServer& server, int unitId, Point target) {
    const BattleState& s = server.state();
    const int u = unitIndex(s, unitId);
    if (u >= 0 && s.currentPlayer == 1) server.handle(moveOrder(s, {u}, target));
    server.handle(EndTurnRequest{1});
}

void battleRoundTrip() {
    BattleServer s1(makeBattle(), {}, 1234);
    std::vector<Event> ev = s1.start(); // the computer player's turn 1, then player 1
    CHECK(s1.state().currentPlayer == 1 && s1.state().turn == 1);
    const int tank = unitIndex(s1.state(), 100);
    ev = s1.handle(moveOrder(s1.state(), {tank}, {2, 4}));
    CHECK(count<UnitMoved>(ev) == 1);
    const int apAfterMove = s1.state().unit(tank).ap;
    CHECK(apAfterMove < s1.state().unit(tank).type->actionPoints);

    const BattleHeader header = makeHeader();
    const std::string text = saveBattle(s1, header);
    CHECK(text.rfind("battle 1\n", 0) == 0 && text.find("battle-end") != std::string::npos);
    CHECK(text.find("reinforcement 500 7 14 14 3 1") != std::string::npos); // the order flag is kept
    CHECK(text.find("ruin WRECK 5 5") != std::string::npos);
    CHECK(text.find("mine 0 0 8 3 1 0 1 1 1 1") != std::string::npos);

    const SavedBattle loaded = loadBattleSave(text, rules());
    CHECK(loaded.header.description == header.description && loaded.header.title == header.title &&
          loaded.header.provinceNumber == 2 && loaded.header.techLine && loaded.header.options.difficulty == 7);
    const BattleState& r = *loaded.state;
    CHECK(r.units().size() == 4 && r.buildings().size() == 2); // dead unit and destroyed building dropped
    CHECK(r.terrain().at(0, 0) == "," && r.hasRuin({5, 5}));
    CHECK(r.mines().size() == 2 && r.mine(0).hidden[1] && !r.mine(0).hidden[0] && !r.mine(1).armed);
    CHECK(r.wall(0).armor == 30 && r.wall(0).brokenEnd && r.roads().size() == 2);
    CHECK(r.unit(0).type->techLevel == 1 && r.unit(0).ap == apAfterMove);
    CHECK(r.reinforcements.size() == 2 && r.reinforcements[0].order && !r.reinforcements[1].order);
    CHECK(r.objective.kind == 1 && r.objective.zone.size() == 3 && r.turnLimit == 30);
    CHECK(r.holdCount == s1.state().holdCount && r.lastHeldTurn == s1.state().lastHeldTurn);
    CHECK(r.automatedUsed[1] == std::vector<int>{3} && r.aiSupport.present && r.aiSupport.budget == 75);
    CHECK(r.stats[1].unitsLost.at(kTank) == 1 && r.deployBudget[1] == s1.state().deployBudget[1] && r.income[1] == 15);
    CHECK(r.scheduledAutomated.size() == 1 && r.scheduledAutomated[0].turn == 9);
    CHECK(r.techLevels[1].at(kTank) == 1 && r.deployZone[1].size() == 2);
    CHECK(r.unit(2).order.positions.size() == 2 && r.unit(3).order.ids == std::vector<int>{1000002});
    CHECK(loaded.aiBudget == s1.aiBudget() && !loaded.rng.empty());
    // Writing the restored state again gives the same text.
    CHECK(saveBattle(r, loaded.header, loaded.rng, loaded.aiBudget) == text);

    // Resume (savegame.md 3.4): the player's turn goes on with the saved
    // AP; no income, no refill, no events.
    auto s2 = restoreServer(loaded);
    ev = s2->resume();
    CHECK(ev.empty() && s2->state().currentPlayer == 1 && s2->state().unit(0).ap == apAfterMove);
    CHECK(s2->state().deployBudget[1] == s1.state().deployBudget[1]);

    // The same requests continue identically (same random numbers).
    for (int round = 0; round < 3; ++round) {
        play(s1, 101, {3, 5 + round});
        play(*s2, 101, {3, 5 + round});
        CHECK(saveBattle(s1, header) == saveBattle(*s2, header));
    }
    CHECK(s1.state().turn == 4);

    // Without the stored random state the continuation differs (the
    // original's behaviour): a different seed gives other draws.
    SavedBattle noRng = loaded;
    noRng.rng.clear();
    auto s3 = restoreServer(noRng);
    CHECK(s3->rngState() != s2->rngState());
}

void computerTurnResume() {
    // A save whose current player is the computer player: it plays its turn
    // from the beginning on resume, then player 1's turn begins.
    BattleServer s1(makeBattle(), {}, 77);
    s1.start();
    BattleState copy = s1.state();
    copy.currentPlayer = 0;
    const std::string text = saveBattle(copy, makeHeader(), s1.rngState(), s1.aiBudget());
    auto s2 = restoreServer(loadBattleSave(text, rules()));
    CHECK(s2->state().currentPlayer == 0);
    const std::vector<Event> ev = s2->resume();
    int aiActions = 0;
    for (const Event& e : ev) {
        if (const auto* m = std::get_if<UnitMoved>(&e)) aiActions += s2->state().unit(m->unit).owner == 0;
        if (const auto* sh = std::get_if<UnitShot>(&e)) aiActions += s2->state().unit(sh->unit).owner == 0;
    }
    CHECK(aiActions > 0);
    CHECK(count<TurnStarted>(ev) == 1 && s2->state().currentPlayer == 1);

    // No save during deployment or after the end.
    BattleState deploying = makeBattle();
    deploying.deploying = true;
    bool threw = false;
    try {
        saveBattle(deploying, makeHeader(), "", 0);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    CHECK(threw);
}

bool loadFails(const std::string& text) {
    try {
        loadBattleSave(text, rules());
    } catch (const std::runtime_error&) {
        return true;
    }
    return false;
}

void damagedSections() {
    BattleServer s1(makeBattle(), {}, 5);
    s1.start();
    const std::string text = saveBattle(s1, makeHeader());
    CHECK(!loadFails(text));
    CHECK(loadFails("battle 2\nbattle-end\n"));                    // a newer version
    CHECK(loadFails(text.substr(0, text.size() / 2)));             // truncated
    std::string unknown = text;
    unknown.replace(unknown.find("\nunit 100 1 1"), 13, "\nunit 100 9 1"); // unknown unit type
    CHECK(loadFails(unknown));
    std::string junk = text;
    junk.insert(junk.find("battle-end"), "frobnicate 1\n");
    CHECK(loadFails(junk));
    std::string offMap = text;
    offMap.replace(offMap.find("\nroad 0 1 7 7"), 13, "\nroad 0 1 7 70");
    CHECK(loadFails(offMap));
    CHECK(escapeSaveText("a\\b\nc\r") == "a\\\\b\\nc\\r" && unescapeSaveText(escapeSaveText("a\\b\nc\r")) == "a\\b\nc\r");
}

// --- Campaign files ----------------------------------------------------------------

fs::path testDir() {
    const fs::path d = fs::temp_directory_path() / "openday-save-test";
    fs::create_directories(d);
    return d;
}

// Minimal colony and city databases.
CampaignResources makeResources() {
    const fs::path dir = testDir();
    const fs::path colony = dir / "c1.sqlite", city = dir / "s1-1-1.sqlite";
    fs::remove(colony);
    fs::remove(city);
    {
        data::SqliteDb db(colony, data::SqliteDb::Mode::ReadWriteCreate);
        db.exec("CREATE TABLE \"Continent Info\" (\"No Continent\", NameEnglish, StartCash, PlayerCity, AlienCity, "
                "AiAutoUnitChanceCall, AiAutoUnitRes, Intro, \"End\", SmackProvinces, SmackProvFrameBegin, "
                "SmackProvFrameQty, DeployEasy, DeployAvg, DeployHard)");
        db.exec("INSERT INTO \"Continent Info\" VALUES (0, 'Testland', 500, 1, 2, 10, 100, 'INTRO1', 'END1', 'C1', 0, 1, "
                "100, 100, 100)");
        db.exec("CREATE TABLE \"Province List\" (NoProvince, ProvinceNameEnglish, TitleEnglish, DescriptionEnglish, "
                "ObjectiveEnglish, MapSize, Credits, Tech, mapScreen1, mapScreen2, mapScreen3, mapScreen4)");
        db.exec("INSERT INTO \"Province List\" VALUES (1, 'Home', 'T', 'D', 'O', 16, 0, 0, NULL, NULL, NULL, NULL)");
        db.exec("INSERT INTO \"Province List\" VALUES (2, 'Away', 'T', 'D', 'O', 16, 100, 5, NULL, NULL, NULL, NULL)");
    }
    {
        data::SqliteDb db(city, data::SqliteDb::Mode::ReadWriteCreate);
        db.exec("CREATE TABLE \"Map\" (PosX, PosY, TerrainImg)");
        db.exec("BEGIN");
        for (int y = 0; y < 16; ++y)
            for (int x = 0; x < 16; ++x)
                db.exec("INSERT INTO \"Map\" VALUES (" + std::to_string(x) + "," + std::to_string(y) + ",'.')");
        db.exec("COMMIT");
        db.exec("CREATE TABLE \"Buildings On Map\" (Counter, id, buildingType, playerOwner, currentAR, PosX, PosY)");
        db.exec("CREATE TABLE \"Roads On Map\" (Counter, roadtype, playerOwner, PosX, PosY)");
        db.exec("CREATE TABLE \"Walls On Map\" (Counter, roadtype, playerOwner, currentAR, posX, posY, isDamaged)");
        db.exec("CREATE TABLE \"Mines On Map\" (Counter, typeID, playerOwner, PosX, PosY, armed)");
    }
    CampaignResources res;
    res.rules = &rules();
    res.scenario = [city](const std::string& n) { return n == "s1-1-1" ? city : fs::path(); };
    res.colony = [colony](int n) { return n == 1 ? colony : fs::path(); };
    res.tempDir = dir / "tmp";
    return res;
}

const char* kVersion1 = "OPENDAY-CAMPAIGN 1\n"
                        "description Old save\n"
                        "name Ann\n"
                        "difficulty 3\n"
                        "colony 1\n"
                        "state 3\n"
                        "province 0\n"
                        "credits 777\n"
                        "tech 12\n"
                        "stats 1 2 3 4\n"
                        "level 1 1\n"
                        "prov 1 1\n"
                        "prov 0 2\n"
                        "city s1-1-1\n"
                        "nextids 1000005 120\n"
                        "building 1000001 130 1 300 4 4\n"
                        "road 0 1 6 4\n"
                        "hq 1 4 4\n"
                        "built-road 6 4\n"
                        "end\n";

void campaignFiles() {
    const CampaignResources res = makeResources();
    std::string description;
    // A version 1 file (OpenDay before battle saves) still loads.
    Campaign old = loadCampaign(kVersion1, res, &description);
    CHECK(description == "Old save" && old.state == CampaignState::Strategic && old.player.credits == 777);
    CHECK(old.city.buildings.size() == 1 && old.city.roads.size() == 1 && !old.savedBattle && !old.counterAttack);

    // A province battle in progress.
    Campaign c = old;
    c.state = CampaignState::Tactic;
    c.battleProvince = 2;
    c.counterAttack = true;
    c.pendingAttack.push_back({300, kAlien, {3, 4}, 2, true});
    BattleServer server(makeBattle(), {}, 42);
    server.start();
    const std::string battle = saveBattle(server, makeHeader());
    const std::string text = saveCampaign(c, "Mid battle", battle);
    CHECK(text.rfind("OPENDAY-CAMPAIGN 2\n", 0) == 0 && saveDescription(text) == "Mid battle");
    Campaign back = loadCampaign(text, res);
    CHECK(back.state == CampaignState::Tactic && back.battleProvince == 2 && back.counterAttack);
    CHECK(back.pendingAttack.size() == 1 && back.pendingAttack[0].order && back.pendingAttack[0].unitId == 300);
    CHECK(back.savedBattle && saveBattle(*back.savedBattle->state, back.savedBattle->header, back.savedBattle->rng,
                                         back.savedBattle->aiBudget) == battle);
    CHECK(back.city.buildings.size() == 1 && back.player.credits == 777);

    // A battle section is refused outside a battle state, both ways.
    bool threw = false;
    try {
        saveCampaign(old, "x", battle);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    CHECK(threw);
    std::string misplaced = text;
    misplaced.replace(misplaced.find("state 4"), 7, "state 3");
    threw = false;
    try {
        loadCampaign(misplaced, res);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    CHECK(threw);
    // A newer format and a cut-off battle section are refused.
    threw = false;
    try {
        loadCampaign("OPENDAY-CAMPAIGN 99\nend\n", res);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    CHECK(threw);
    threw = false;
    try {
        loadCampaign(text.substr(0, text.find("battle-end")), res);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    CHECK(threw);
}

std::string readAll(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

void atomicWrite() {
    const fs::path dir = testDir() / "atomic";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const fs::path file = dir / "campaign.01";
    writeSaveFile(file, "first\n");
    CHECK(readAll(file) == "first\n");
    writeSaveFile(file, "second\n");
    CHECK(readAll(file) == "second\n" && !fs::exists(dir / "campaign.01.tmp"));
    // The temporary file cannot be written: the old content stays.
    fs::create_directories(dir / "campaign.01.tmp");
    bool threw = false;
    try {
        writeSaveFile(file, "third\n");
    } catch (const std::runtime_error&) {
        threw = true;
    }
    CHECK(threw && readAll(file) == "second\n");
    fs::remove_all(dir);
}

// --- Import of an original save slot -------------------------------------------------

void makeGameInfo(const fs::path& path, int state, int currentPlayer, int colonyIndex) {
    fs::remove(path);
    data::SqliteDb db(path, data::SqliteDb::Mode::ReadWriteCreate);
    db.exec("CREATE TABLE \"Server Mode Campaign\" (\"Colony Number\", \"Campaign State\", \"Tactical Province\", "
            "\"Load Terrain Change List\", \"Load Tactical Scenario\")");
    db.exec("INSERT INTO \"Server Mode Campaign\" VALUES (" + std::to_string(colonyIndex) + "," + std::to_string(state) +
            ",2,0,1)");
    db.exec("CREATE TABLE \"Server Mode Info\" (Difficulty, \"Turn Time\")");
    db.exec("INSERT INTO \"Server Mode Info\" VALUES (6, 0)");
    db.exec("CREATE TABLE \"User List\" (\"User Name\", \"User Password\", \"User ID\", \"User Color\")");
    db.exec("INSERT INTO \"User List\" VALUES ('Bob  ', ' ', 1, 0)");
    db.exec("CREATE TABLE \"Player Campaign\" (\"Player ID\", Credits, Tech, \"Units Destroyed\", \"Units Lost\", "
            "\"Buildings Destroyed\", \"Buildings Lost\")");
    db.exec("INSERT INTO \"Player Campaign\" VALUES (0, 9, 9, 9, 9, 9, 9)");
    db.exec("INSERT INTO \"Player Campaign\" VALUES (1, 4321, 33, 5, 6, 7, 8)");
    db.exec("CREATE TABLE \"Player Campaign Tech\" (Player, \"Unit Type\", \"Tech Level\")");
    db.exec("INSERT INTO \"Player Campaign Tech\" VALUES (1, 1, 1)");
    db.exec("INSERT INTO \"Player Campaign Tech\" VALUES (0, 2, 3)");
    db.exec("CREATE TABLE \"Player Tactical\" (\"Player ID\", \"Deploy Res\")");
    db.exec("INSERT INTO \"Player Tactical\" VALUES (1, 85)");
    db.exec("CREATE TABLE \"Player Tactical Units Destroyed\" (Player, \"Unit Type\", Number)");
    db.exec("INSERT INTO \"Player Tactical Units Destroyed\" VALUES (1, 7, 2)");
    db.exec("CREATE TABLE \"Player Tactical Buildings Lost\" (Player, \"Building Type\", Number)");
    db.exec("INSERT INTO \"Player Tactical Buildings Lost\" VALUES (1, 200, 1)");
    db.exec("CREATE TABLE \"Tactic IO\" (\"Current Player\", \"Current Turn\", \"Objective Turn\", \"Last Turn\")");
    db.exec("INSERT INTO \"Tactic IO\" VALUES (" + std::to_string(currentPlayer) + ", 3, 2, 2)");
    db.exec("CREATE TABLE \"Strategic IO Road\" (Player, PosX, PosY)");
    db.exec("INSERT INTO \"Strategic IO Road\" VALUES (1, 6, 4)");
    db.exec("INSERT INTO \"Strategic IO Road\" VALUES (2, 7, 4)");
}

void makeSlotColony(const fs::path& from, const fs::path& path) {
    fs::copy_file(from, path, fs::copy_options::overwrite_existing);
    data::SqliteDb db(path, data::SqliteDb::Mode::ReadWriteCreate);
    db.exec("CREATE TABLE \"Province List Captured\" (NoProvince, Captured, RandomChosen)");
    db.exec("INSERT INTO \"Province List Captured\" VALUES (1, 1, 1)");
    db.exec("INSERT INTO \"Province List Captured\" VALUES (2, 0, 2)");
}

void makeSlotScenario(const fs::path& path) {
    fs::remove(path);
    data::SqliteDb db(path, data::SqliteDb::Mode::ReadWriteCreate);
    db.exec("CREATE TABLE \"Map\" (PosX, PosY, TerrainImg)");
    db.exec("BEGIN");
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x)
            db.exec("INSERT INTO \"Map\" VALUES (" + std::to_string(x) + "," + std::to_string(y) + ",'" +
                    (x == 0 && y == 0 ? "," : ".") + "')");
    db.exec("COMMIT");
    db.exec("CREATE TABLE \"Units On Map\" (Counter, ID, unitType, unitTL, playerOwner, facing, currentAR, currentAP, negAP, "
            "PosX, PosY, OrderID, Turn, Probability, EnemyUnitType)");
    db.exec("INSERT INTO \"Units On Map\" VALUES (1, 100, 1, 1, 1, 2, 55, 40, 0, 2, 2, 0, 0, 100, 65535)");
    db.exec("INSERT INTO \"Units On Map\" VALUES (2, 101, 2, 0, 2, 0, 20, 80, 10, 3, 2, 0, 0, 100, 65535)");
    db.exec("INSERT INTO \"Units On Map\" VALUES (3, 102, 7, 0, 0, 4, 50, 25, 0, 12, 12, 1, 99, 100, 65535)");
    db.exec("CREATE TABLE \"Unit Order Pos List\" (Counter, \"Unit ID\", PosX, PosY)");
    db.exec("INSERT INTO \"Unit Order Pos List\" VALUES (1, 102, 9, 12)");
    db.exec("CREATE TABLE \"Buildings On Map\" (Counter, id, buildingType, playerOwner, currentAR, PosX, PosY, IsAiTarget)");
    db.exec("INSERT INTO \"Buildings On Map\" VALUES (1, 1000002, 200, 0, 90, 11, 4, NULL)");
    db.exec("CREATE TABLE \"Roads On Map\" (Counter, roadtype, playerOwner, PosX, PosY)");
    db.exec("CREATE TABLE \"Walls On Map\" (Counter, roadtype, playerOwner, currentAR, posX, posY, isDamaged)");
    db.exec("CREATE TABLE \"Mines On Map\" (Counter, typeID, playerOwner, PosX, PosY, armed, hidden_AI, hidden_1, hidden_2, "
            "hidden_3, hidden_4)");
    db.exec("INSERT INTO \"Mines On Map\" VALUES (1, 0, 0, 8, 3, 1, 0, 1, 1, 1, 1)");
    db.exec("CREATE TABLE \"Ruins On Map\" (Counter, id, PosX, PosY)");
    db.exec("CREATE TABLE \"Objective\" (ObjectifId, TurnNumber)");
    db.exec("INSERT INTO \"Objective\" VALUES (1, 5)");
    db.exec("CREATE TABLE \"Objectives Pos List\" (Counter, PosX, PosY)");
    db.exec("INSERT INTO \"Objectives Pos List\" VALUES (1, 2, 2)");
    db.exec("CREATE TABLE \"Reinforcement\" (Counter, UnitID, type, posX, posY, turn, \"order\")");
    db.exec("INSERT INTO \"Reinforcement\" VALUES (1, 500, 7, 14, 14, 5, 0)");
    db.exec("CREATE TABLE \"Scenario Info\" (TitleEnglish, provincenameEnglish, descriptionEnglish, AftermathEnglish, "
            "\"One Off Deployment\", ReinfRes, CounterAttack, turn_qty, NextID_Units, NextID_Buildings)");
    db.exec("INSERT INTO \"Scenario Info\" VALUES ('Away title', 'Away', 'Hold it', 'Held', 100, 10, 1, 20, 103, 1000003)");
    db.exec("CREATE TABLE \"Counter Attack\" (Counter, UnitID, type, posX, posY, turn, \"order\")");
    db.exec("INSERT INTO \"Counter Attack\" VALUES (1, 600, 7, 5, 5, 1, 1)");
}

bool importFails(const OriginalSaveDatabases& dbs, const CampaignResources& res) {
    try {
        importOriginalSave(dbs, res, "x");
    } catch (const std::runtime_error&) {
        return true;
    }
    return false;
}

void importTests() {
    const CampaignResources res = makeResources();
    const fs::path dir = testDir() / "import";
    fs::create_directories(dir);
    OriginalSaveDatabases dbs;
    dbs.gameInfo = dir / "gameinfo.sqlite";
    dbs.city = res.scenario("s1-1-1");
    dbs.colony = dir / "colony.sqlite";
    makeSlotColony(res.colony(1), dbs.colony);

    // The strategic phase.
    makeGameInfo(dbs.gameInfo, 3, 5, 0);
    Campaign c = importOriginalSave(dbs, res, "Slot name");
    CHECK(c.state == CampaignState::Strategic && c.colonyNumber == 1 && c.difficulty == 6 && c.playerName == "Bob");
    CHECK(c.player.credits == 4321 && c.player.tech == 33 && c.player.unitsLost == 6 && c.player.buildingsLost == 8);
    CHECK(c.player.techLevels.size() == 1 && c.player.techLevels.at(1) == 1);
    CHECK(c.provinces.size() == 2 && c.provinces[0].captured && !c.provinces[1].captured && c.provinces[1].variant == 2);
    CHECK(c.built.roads.size() == 1 && c.built.roads.count({6, 4}) && !c.savedBattle);
    // It saves in OpenDay's format.
    CHECK(loadCampaign(saveCampaign(c, "imported"), res).player.credits == 4321);

    // A province battle on the player's turn.
    makeGameInfo(dbs.gameInfo, 4, 1, 0);
    dbs.scenario = dir / "scenario.sqlite";
    makeSlotScenario(dbs.scenario);
    c = importOriginalSave(dbs, res, "Slot name");
    CHECK(c.state == CampaignState::Tactic && c.battleProvince == 2 && c.savedBattle);
    CHECK(c.counterAttack && c.pendingAttack.size() == 1 && c.pendingAttack[0].unitId == 600);
    if (c.savedBattle) {
        const SavedBattle& sb = *c.savedBattle;
        const BattleState& s = *sb.state;
        CHECK(sb.header.title == "Away title" && sb.header.provinceNumber == 2 && sb.header.techLine && !sb.header.city);
        CHECK(s.turn == 3 && s.currentPlayer == 1 && !s.deploying && s.players == std::vector<int>({0, 1}));
        CHECK(s.units().size() == 3 && s.unit(0).ap == 40 && s.unit(1).negAp == 10); // saved AP, no refill
        CHECK(s.unit(1).owner == 1);                                                 // slot 2 joins slot 1
        CHECK(s.unit(2).order.order == 1 && s.unit(2).order.positions.size() == 1);
        CHECK(s.objective.kind == 1 && s.holdCount == 2 && s.lastHeldTurn == 2);
        CHECK(s.deployBudget[1] == 85 && s.stats[1].unitsDestroyed.at(7) == 2 && s.stats[1].buildingsLost.at(200) == 1);
        CHECK(s.mines().size() == 1 && s.mine(0).hidden[1] && s.terrain().at(0, 0) == ",");
        CHECK(s.reinforcements.size() == 1 && !s.reinforcements[0].order);
        CHECK(sb.aiBudget == 100); // AiAutoUnitRes, full (not stored by the original)
        auto server = restoreServer(sb);
        CHECK(server->resume().empty() && server->state().unit(0).ap == 40);
        // The imported battle saves in OpenDay's format and loads back.
        const std::string text = saveCampaign(c, "imported", saveBattle(*server, sb.header));
        const Campaign back = loadCampaign(text, res);
        CHECK(back.savedBattle && back.savedBattle->state->units().size() == 3 && back.counterAttack);
    }
    // The computer player's turn: it will replay its turn.
    makeGameInfo(dbs.gameInfo, 4, 0, 0);
    c = importOriginalSave(dbs, res, "x");
    CHECK(c.savedBattle && c.savedBattle->state->currentPlayer == 0);

    // Refused: deployment, states that cannot be resumed, another colony,
    // a missing province scenario, a missing table.
    makeGameInfo(dbs.gameInfo, 4, 5, 0);
    CHECK(importFails(dbs, res));
    makeGameInfo(dbs.gameInfo, 5, 1, 0);
    CHECK(importFails(dbs, res));
    makeGameInfo(dbs.gameInfo, 3, 1, 3);
    CHECK(importFails(dbs, res));
    makeGameInfo(dbs.gameInfo, 4, 1, 0);
    OriginalSaveDatabases noScenario = dbs;
    noScenario.scenario.clear();
    CHECK(importFails(noScenario, res));
    {
        data::SqliteDb db(dbs.gameInfo, data::SqliteDb::Mode::ReadWriteCreate);
        db.exec("DROP TABLE \"Tactic IO\"");
    }
    CHECK(importFails(dbs, res));

    // Slot listing: folders with the three databases count as used.
    const fs::path game = dir / "game";
    fs::remove_all(game);
    fs::create_directories(game / "SAVEGAME" / "savegame.02");
    fs::create_directories(game / "SAVEGAME" / "SAVEGAME.03");
    for (const char* f : {"GAMEINFO.MDB", "CITY.MDB", "COLONY.MDB", "My game.dsc"})
        std::ofstream(game / "SAVEGAME" / "savegame.02" / f) << "";
    const std::vector<OriginalSlot> slots = listOriginalSlots(game);
    CHECK(slots.size() == 10 && slots[1].used && slots[1].description == "My game" && !slots[2].used && !slots[0].used);
    // An empty file is not a database.
    bool threw = false;
    try {
        importOriginalSlot(slots[1], res, dir / "work");
    } catch (const std::runtime_error&) {
        threw = true;
    }
    CHECK(threw);
    fs::remove_all(game);
}

} // namespace

int main() {
    battleRoundTrip();
    computerTurnResume();
    damagedSections();
    campaignFiles();
    atomicWrite();
    importTests();
    if (failures) std::fprintf(stderr, "%d check(s) failed\n", failures);
    else std::printf("save_test: all checks passed\n");
    return failures ? 1 : 0;
}
