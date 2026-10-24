#include "campaign/savegame_import.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <memory>
#include <stdexcept>

#include "battle/objectives.hpp"
#include "data/mdb_import.hpp"
#include "map/terrain_map.hpp"

namespace openday::game {
namespace fs = std::filesystem;
namespace {

[[noreturn]] void refuse(const std::string& why) { throw std::runtime_error("cannot import the saved game: " + why); }

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// A child of `dir` whose name matches case-insensitively; empty if none.
fs::path findChild(const fs::path& dir, const std::string& name) {
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return {};
    const std::string want = lower(name);
    for (const auto& e : fs::directory_iterator(dir, ec))
        if (lower(e.path().filename().string()) == want) return e.path();
    return {};
}

int toInt(const data::Row& r, int col) { return r.isNull(col) ? 0 : static_cast<int>(r.integer(col)); }

void requireTable(const data::SqliteDb& db, const std::string& table, const char* file) {
    if (!db.hasTable(table)) refuse(std::string(file) + " has no table '" + table + "'");
}

// The first row of a table; refused when the table is empty.
std::vector<int> firstRow(const data::SqliteDb& db, const std::string& table, const std::vector<std::string>& columns,
                          const char* file) {
    requireTable(db, table, file);
    std::string sql = "SELECT ";
    for (std::size_t i = 0; i < columns.size(); ++i) sql += (i ? ", " : "") + data::quoteIdent(columns[i]);
    sql += " FROM " + data::quoteIdent(table);
    std::vector<int> out;
    bool found = false;
    try {
        db.forEach(sql, [&](const data::Row& r) {
            if (found) return;
            found = true;
            for (std::size_t i = 0; i < columns.size(); ++i) out.push_back(toInt(r, static_cast<int>(i)));
        });
    } catch (const std::exception& ex) {
        refuse(std::string(file) + ", table '" + table + "': " + ex.what());
    }
    if (!found) refuse(std::string(file) + ", table '" + table + "' is empty");
    return out;
}

// Rows of (player, a, b) from a per-player table.
std::vector<std::array<int, 3>> playerRows(const data::SqliteDb& db, const std::string& table, const std::string& a,
                                           const std::string& b, const char* file) {
    std::vector<std::array<int, 3>> out;
    if (!db.hasTable(table)) return out;
    try {
        db.forEach("SELECT \"Player\", " + data::quoteIdent(a) + ", " + data::quoteIdent(b) + " FROM " + data::quoteIdent(table),
                   [&](const data::Row& r) { out.push_back({toInt(r, 0), toInt(r, 1), toInt(r, 2)}); });
    } catch (const std::exception& ex) {
        refuse(std::string(file) + ", table '" + table + "': " + ex.what());
    }
    return out;
}

std::string trim(std::string s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
    return s;
}

} // namespace

std::vector<OriginalSlot> listOriginalSlots(const fs::path& gameDir) {
    // The [Installation] savegame path of LIBDAY.INI is <game>\SAVEGAME\ in
    // the shipped installation; OpenDay looks there.
    std::vector<OriginalSlot> out;
    const fs::path saves = findChild(gameDir, "SAVEGAME");
    for (int n = 1; n <= 10; ++n) {
        OriginalSlot s;
        s.number = n;
        char name[16];
        std::snprintf(name, sizeof name, "savegame.%02d", n);
        s.folder = saves.empty() ? fs::path() : findChild(saves, name);
        if (!s.folder.empty()) {
            s.used = !findChild(s.folder, "GAMEINFO.MDB").empty() && !findChild(s.folder, "CITY.MDB").empty() &&
                     !findChild(s.folder, "COLONY.MDB").empty();
            std::error_code ec;
            for (const auto& e : fs::directory_iterator(s.folder, ec))
                if (lower(e.path().extension().string()) == ".dsc") {
                    s.description = e.path().stem().string();
                    break;
                }
        }
        out.push_back(std::move(s));
    }
    return out;
}

Campaign importOriginalSave(const OriginalSaveDatabases& dbs, const CampaignResources& res, const std::string& description) {
    if (!res.rules) refuse("no game rules");
    const GlobalRules& rules = *res.rules;
    for (const fs::path& p : {dbs.gameInfo, dbs.city, dbs.colony})
        if (p.empty() || !fs::exists(p)) refuse("a database of the slot is missing");
    try {
        const data::SqliteDb gi(dbs.gameInfo), colonyDb(dbs.colony), cityDb(dbs.city);
        Campaign c;
        c.rules = &rules;

        // GAMEINFO.MDB (savegame.md 2.4, 3.1).
        const auto mode = firstRow(gi, "Server Mode Campaign", {"Colony Number", "Campaign State", "Tactical Province"},
                                   "GAMEINFO.MDB");
        const auto info = firstRow(gi, "Server Mode Info", {"Difficulty"}, "GAMEINFO.MDB");
        c.colony = loadColony(colonyDb);
        c.colonyNumber = c.colony.number;
        if (c.colonyNumber < 1 || c.colonyNumber > 4) refuse("COLONY.MDB names no colony 1-4");
        // `Colony Number` is the colony index; taken as 0-based *(?)*, the
        // colony database decides.
        if (mode[0] != c.colonyNumber - 1 && mode[0] != c.colonyNumber)
            refuse("GAMEINFO.MDB and COLONY.MDB name different colonies");
        const int savedState = mode[1];
        if (savedState != 1 && savedState != 3 && savedState != 4 && savedState != 6)
            refuse("campaign state " + std::to_string(savedState) + " cannot be resumed");
        c.state = static_cast<CampaignState>(savedState);
        c.battleProvince = mode[2];
        c.difficulty = std::clamp(info[0], 0, 10);

        // The player: single player, the human slot is 1 *(?: the lowest
        // non-zero Player ID is taken when there is no row for 1)*.
        requireTable(gi, "Player Campaign", "GAMEINFO.MDB");
        struct PlayerRow {
            int id = 0, credits = 0, tech = 0, ud = 0, ul = 0, bd = 0, bl = 0;
        };
        std::vector<PlayerRow> players;
        gi.forEach("SELECT \"Player ID\", Credits, Tech, \"Units Destroyed\", \"Units Lost\", \"Buildings Destroyed\", "
                   "\"Buildings Lost\" FROM \"Player Campaign\"",
                   [&](const data::Row& r) {
                       players.push_back({toInt(r, 0), toInt(r, 1), toInt(r, 2), toInt(r, 3), toInt(r, 4), toInt(r, 5),
                                          toInt(r, 6)});
                   });
        std::sort(players.begin(), players.end(), [](const PlayerRow& a, const PlayerRow& b) { return a.id < b.id; });
        const auto human = std::find_if(players.begin(), players.end(), [](const PlayerRow& p) { return p.id >= 1; });
        if (human == players.end()) refuse("GAMEINFO.MDB has no human player");
        const int pid = human->id;
        c.player.credits = human->credits;
        c.player.tech = human->tech;
        c.player.unitsDestroyed = human->ud;
        c.player.unitsLost = human->ul;
        c.player.buildingsDestroyed = human->bd;
        c.player.buildingsLost = human->bl;
        for (const auto& [p, type, level] : playerRows(gi, "Player Campaign Tech", "Unit Type", "Tech Level", "GAMEINFO.MDB"))
            if (p == pid) c.player.techLevels[type] = level;
        if (gi.hasTable("User List"))
            gi.forEach("SELECT \"User Name\" FROM \"User List\"", [&](const data::Row& r) {
                if (c.playerName.empty()) c.playerName = trim(r.text(0));
            });
        if (c.playerName.empty()) c.playerName = description.empty() ? "Commander" : description;

        // COLONY.MDB: captured flags and variants (2.6).
        if (!colonyDb.hasTable("Province List Captured")) refuse("COLONY.MDB has no 'Province List Captured'");
        if (c.colony.provinces.empty()) refuse("COLONY.MDB has no provinces");
        for (const Province& p : c.colony.provinces) c.provinces.push_back({p.captured, std::max(1, p.variant)});
        if (c.battleProvince < 0 || c.battleProvince > static_cast<int>(c.provinces.size()))
            refuse("the battle province does not exist");

        // CITY.MDB: the city (during a city battle: the battle map).
        const std::string cityName = "s" + std::to_string(c.colonyNumber) + "-" + std::to_string(c.colony.playerCity) + "-1";
        c.city = loadCity(cityDb, cityName, rules);
        c.city.computeBridgeSites(rules);

        // The strategic phase's "built in this phase" lists (3.5).
        if (c.state == CampaignState::Strategic) {
            for (const auto& [p, id, unused] : playerRows(gi, "Strategic IO Building", "Id", "Id", "GAMEINFO.MDB"))
                if (p == pid) c.built.buildings.insert(id);
            for (const auto& [p, x, y] : playerRows(gi, "Strategic IO Road", "PosX", "PosY", "GAMEINFO.MDB"))
                if (p == pid) c.built.roads.insert({x, y});
            for (const auto& [p, x, y] : playerRows(gi, "Strategic IO Wall", "PosX", "PosY", "GAMEINFO.MDB"))
                if (p == pid) c.built.walls.insert({x, y});
            for (const auto& [p, x, y] : playerRows(gi, "Strategic IO Mine", "PosX", "PosY", "GAMEINFO.MDB"))
                if (p == pid) c.built.mines.insert({x, y});
            return c;
        }

        // A battle in progress (3.2-3.4).
        const auto tactic = firstRow(gi, "Tactic IO", {"Current Player", "Current Turn", "Objective Turn", "Last Turn"},
                                     "GAMEINFO.MDB");
        if (tactic[0] == 5) refuse("the battle was saved during deployment");
        if (tactic[0] < 0 || tactic[0] > 4 || tactic[1] < 1) refuse("the battle's turn state is invalid");
        CampaignBattle b;
        std::unique_ptr<data::SqliteDb> scenarioDb;
        const data::SqliteDb* battleDb = &cityDb;
        std::string scenarioName = cityName;
        if (c.state == CampaignState::Tactic) {
            if (dbs.scenario.empty() || !fs::exists(dbs.scenario)) refuse("the province battle has no SCENARIO.MDB");
            scenarioDb = std::make_unique<data::SqliteDb>(dbs.scenario);
            battleDb = scenarioDb.get();
            scenarioName = c.provinceScenario(c.battleProvince);
            if (c.battleProvince < 1) refuse("the province battle has no province");
            // Texts, lists and counter-attack rows: the slot's copy keeps
            // the scenario's unwritten tables (2.5).
            b = c.provinceBattle(dbs.scenario, scenarioName, rules);
        } else {
            b = c.cityBattleSetup(rules);
            if (cityDb.hasTable("Scenario Info")) readScenarioTexts(cityDb, b);
        }
        const TerrainMap terrain = loadTerrainMap(*battleDb);
        auto state = std::make_shared<BattleState>(loadBattle(*battleDb, rules, terrain));
        BattleState& s = *state;
        if (b.prepare) b.prepare(s); // owners final except slots 2-4, which OpenDay's single player merges into 1
        for (std::size_t i = 0; i < s.units().size(); ++i)
            if (s.unit(static_cast<int>(i)).owner >= 2) s.unit(static_cast<int>(i)).owner = kCampaignPlayer;
        for (std::size_t i = 0; i < s.buildings().size(); ++i)
            if (s.building(static_cast<int>(i)).owner >= 2) s.building(static_cast<int>(i)).owner = kCampaignPlayer;
        if (c.state == CampaignState::Tactic) {
            s.holdCount = tactic[2];
            s.lastHeldTurn = tactic[3];
        } else {
            // A fresh counter-attack objective; the zones from the city (3.3).
            s.objective = {7, 99, {}, {}};
            const auto zones = cityZones(c.city, rules);
            s.deployZone = {};
            s.deployZone[kCampaignPlayer] = zones[kCampaignPlayer];
        }
        s.players = {0, kCampaignPlayer};
        s.computerPlayer = true;
        s.currentPlayer = tactic[0] >= 1 ? kCampaignPlayer : 0;
        s.turn = tactic[1];
        s.deploying = false;
        s.over = false;
        s.deployBudget = {};
        if (gi.hasTable("Player Tactical"))
            gi.forEach("SELECT \"Player ID\", \"Deploy Res\" FROM \"Player Tactical\"", [&](const data::Row& r) {
                if (toInt(r, 0) == pid) s.deployBudget[kCampaignPlayer] = toInt(r, 1);
            });
        BattleStats& st = s.stats[kCampaignPlayer];
        for (const auto& [p, type, n] : playerRows(gi, "Player Tactical Units Destroyed", "Unit Type", "Number", "GAMEINFO.MDB"))
            if (p == pid) st.unitsDestroyed[type] += n;
        for (const auto& [p, type, n] : playerRows(gi, "Player Tactical Units Lost", "Unit Type", "Number", "GAMEINFO.MDB"))
            if (p == pid) st.unitsLost[type] += n;
        for (const auto& [p, type, n] :
             playerRows(gi, "Player Tactical Buildings Destroyed", "Building Type", "Number", "GAMEINFO.MDB"))
            if (p == pid) st.buildingsDestroyed[type] += n;
        for (const auto& [p, type, n] : playerRows(gi, "Player Tactical Buildings Lost", "Building Type", "Number", "GAMEINFO.MDB"))
            if (p == pid) st.buildingsLost[type] += n;
        s.objectiveProgress = objectiveProgress(s);

        SavedBattle saved;
        saved.header.scenario = scenarioName;
        saved.header.title = b.title;
        saved.header.province = b.provinceName;
        saved.header.description = b.description;
        saved.header.aftermath = b.aftermath;
        saved.header.city = b.city;
        saved.header.provinceNumber = b.city ? b.province : c.battleProvince;
        saved.header.techLine = !b.city;
        saved.header.options = b.options;
        // Not stored by the original: the AI's budget starts full (4), the
        // random numbers anew.
        saved.aiBudget = s.aiSupport.present ? s.aiSupport.budget * (s.counterAttack ? 3 : 2) / 2 : 0;
        saved.state = std::move(state);
        c.savedBattle = std::make_shared<const SavedBattle>(std::move(saved));
        return c;
    } catch (const data::DbError& ex) {
        refuse(ex.what());
    }
}

Campaign importOriginalSlot(const OriginalSlot& slot, const CampaignResources& res, const fs::path& workDir) {
    if (!slot.used) refuse("the slot is empty");
    fs::create_directories(workDir);
    auto convert = [&](const char* file, const char* out) -> fs::path {
        const fs::path src = findChild(slot.folder, file);
        if (src.empty()) return {};
        std::ifstream f(src, std::ios::binary);
        const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        if (bytes.empty()) refuse(std::string(file) + " cannot be read");
        const fs::path dst = workDir / out;
        try {
            data::convertMdb(bytes, dst);
        } catch (const std::exception& ex) {
            refuse(std::string(file) + ": " + ex.what());
        }
        return dst;
    };
    OriginalSaveDatabases dbs;
    dbs.gameInfo = convert("GAMEINFO.MDB", "gameinfo.sqlite");
    dbs.city = convert("CITY.MDB", "city.sqlite");
    dbs.colony = convert("COLONY.MDB", "colony.sqlite");
    // A stale SCENARIO.MDB is ignored unless the state is 4 (2.1).
    dbs.scenario = convert("SCENARIO.MDB", "scenario.sqlite");
    return importOriginalSave(dbs, res, slot.description);
}

} // namespace openday::game
