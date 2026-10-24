#include "battle/battle_save.hpp"

#include <algorithm>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>

namespace openday::game {
namespace {

[[noreturn]] void fail(const std::string& what) { throw std::runtime_error("battle save: " + what); }

// The rules key of a unit type: found by address (types are shared with the
// rules), else by the row's own fields.
std::pair<int, int> unitKey(const GlobalRules& rules, const UnitType* t) {
    for (const auto& [key, type] : rules.units)
        if (&type == t) return key;
    return {t->typeId, t->techLevel};
}

int buildingKey(const GlobalRules& rules, const BuildingType* t) {
    for (const auto& [key, type] : rules.buildings)
        if (&type == t) return key;
    return t->id;
}

void writeList(std::ostream& o, const std::vector<int>& v) {
    for (const int x : v) o << ' ' << x;
}

void writePoints(std::ostream& o, const std::vector<Point>& v) {
    for (const Point p : v) o << ' ' << p.x << ' ' << p.y;
}

void writeCounter(std::ostream& o, int player, const char* kind, const std::map<int, int>& m) {
    if (m.empty()) return;
    o << "stats " << player << ' ' << kind;
    for (const auto& [type, n] : m) o << ' ' << type << ' ' << n;
    o << '\n';
}

// Reads every remaining integer of a line.
std::vector<int> readInts(std::istream& in) {
    std::vector<int> v;
    for (int x = 0; in >> x;) v.push_back(x);
    return v;
}

std::vector<Point> toPoints(const std::vector<int>& v) {
    if (v.size() % 2 != 0) fail("odd number of coordinates");
    std::vector<Point> out;
    for (std::size_t i = 0; i + 1 < v.size(); i += 2) out.push_back({v[i], v[i + 1]});
    return out;
}

int slotOf(int p) {
    if (p < 0 || p >= kPlayerSlots) fail("player slot " + std::to_string(p));
    return p;
}

// Reads exactly `n` integers; fails otherwise.
std::vector<int> need(std::istream& in, std::size_t n, const std::string& key) {
    std::vector<int> v(n);
    for (int& x : v)
        if (!(in >> x)) fail("bad line: " + key);
    return v;
}

} // namespace

std::string escapeSaveText(const std::string& s) {
    std::string out;
    for (const char c : s) {
        if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else out += c;
    }
    return out;
}

std::string unescapeSaveText(const std::string& s) {
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '\\' || i + 1 == s.size()) {
            out += s[i];
            continue;
        }
        const char c = s[++i];
        out += c == 'n' ? '\n' : c == 'r' ? '\r' : c;
    }
    return out;
}

std::string saveBattle(const BattleServer& server, const BattleHeader& header) {
    return saveBattle(server.state(), header, server.rngState(), server.aiBudget());
}

std::string saveBattle(const BattleState& s, const BattleHeader& header, const std::string& rng, int aiBudget) {
    // savegame.md: no save during deployment (the format has no
    // "deployment in progress"), and nothing to resume after the end.
    if (s.deploying) throw std::runtime_error("cannot save during deployment");
    if (s.over) throw std::runtime_error("the battle is over");
    const GlobalRules& rules = s.rules();
    std::ostringstream o;
    o << "battle " << kBattleSaveVersion << '\n';
    o << "scenario " << escapeSaveText(header.scenario) << '\n';
    o << "title " << escapeSaveText(header.title) << '\n';
    o << "province-name " << escapeSaveText(header.province) << '\n';
    o << "description " << escapeSaveText(header.description) << '\n';
    o << "aftermath " << escapeSaveText(header.aftermath) << '\n';
    o << "city " << (header.city ? 1 : 0) << '\n';
    o << "province " << header.provinceNumber << '\n';
    o << "tech-line " << (header.techLine ? 1 : 0) << '\n';
    const CombatOptions& op = header.options;
    o << "options " << op.coop << ' ' << op.aiDamage << ' ' << op.difficulty << ' ' << op.computerAi << ' '
      << op.faithfulAutomated << '\n';

    // The map, run-length coded in row order (y outer, x inner).
    const TerrainMap& t = s.terrain();
    o << "map " << t.size << '\n';
    for (std::size_t i = 0; i < t.tiles.size();) {
        std::size_t j = i;
        while (j < t.tiles.size() && t.tiles[j] == t.tiles[i]) ++j;
        if (t.tiles[i].empty() || t.tiles[i].find_first_of(" \t\r\n") != std::string::npos) fail("terrain name");
        o << "tiles " << t.tiles[i] << ' ' << (j - i) << '\n';
        i = j;
    }

    o << "players";
    writeList(o, s.players);
    o << '\n';
    o << "computer " << (s.computerPlayer ? 1 : 0) << '\n';
    o << "turn " << s.turn << ' ' << s.currentPlayer << ' ' << s.turnLimit << '\n';
    o << "objective " << s.objective.kind << ' ' << s.objective.turns << '\n';
    o << "objective-ids";
    writeList(o, s.objective.ids);
    o << '\n';
    o << "objective-zone";
    writePoints(o, s.objective.zone);
    o << '\n';
    o << "hold " << s.holdCount << ' ' << s.lastHeldTurn << ' ' << s.objectiveProgress << '\n';
    o << "cheat " << s.cheatWin << ' ' << s.cheatLose << '\n';
    for (int p = 0; p < kPlayerSlots; ++p) {
        const auto i = static_cast<std::size_t>(p);
        if (!s.deployZone[i].empty()) {
            o << "zone " << p;
            writePoints(o, s.deployZone[i]);
            o << '\n';
        }
        o << "resource " << p << ' ' << s.deployBudget[i] << ' ' << s.income[i] << '\n';
        if (!s.techLevels[i].empty()) {
            o << "tech-levels " << p;
            for (const auto& [type, level] : s.techLevels[i]) o << ' ' << type << ' ' << level;
            o << '\n';
        }
        o << "automated " << p << ' ' << s.automatedNeedsBuildings[i];
        writeList(o, s.automated[i]);
        o << '\n';
        if (!s.automatedUsed[i].empty()) {
            o << "automated-used " << p;
            writeList(o, s.automatedUsed[i]);
            o << '\n';
        }
        const BattleStats& st = s.stats[i];
        writeCounter(o, p, "ud", st.unitsDestroyed);
        writeCounter(o, p, "ul", st.unitsLost);
        writeCounter(o, p, "bd", st.buildingsDestroyed);
        writeCounter(o, p, "bl", st.buildingsLost);
    }
    const BattleState::CityIncome& ci = s.cityIncome;
    o << "city-income " << ci.deployType << ' ' << ci.deployProduction << ' ' << ci.hqProduction << ' ' << ci.multiplier
      << ' ' << ci.share << '\n';
    o << "deploy-needs-buildings " << s.deployNeedsBuildings << '\n';
    o << "purchasable";
    writeList(o, s.purchasable);
    o << '\n';
    o << "bridge-sites";
    writePoints(o, s.bridgeSites);
    o << '\n';
    for (const BattleState::ScheduledAutomated& a : s.scheduledAutomated)
        o << "scheduled " << a.type << ' ' << a.start.x << ' ' << a.start.y << ' ' << a.dest.x << ' ' << a.dest.y << ' '
          << a.turn << '\n';
    o << "ai-support " << s.aiSupport.present << ' ' << s.aiSupport.chance << ' ' << s.aiSupport.budget;
    writeList(o, s.aiSupport.types);
    o << '\n';
    o << "counter-attack " << s.counterAttack << '\n';
    o << "one-off " << s.oneOffDeployment << '\n';
    // The order flag is kept (the original writes 0, savegame.md 4).
    for (const Reinforcement& r : s.reinforcements)
        o << "reinforcement " << r.unitId << ' ' << r.type << ' ' << r.pos.x << ' ' << r.pos.y << ' ' << r.turn << ' '
          << r.order << '\n';

    for (const BattleUnit& u : s.units()) {
        if (!u.alive) continue;
        const auto [type, level] = unitKey(rules, u.type);
        o << "unit " << u.id << ' ' << type << ' ' << level << ' ' << u.owner << ' ' << u.ap << ' ' << u.negAp << ' ' << u.armor
          << ' ' << u.pos.x << ' ' << u.pos.y << ' ' << u.facing << ' ' << u.order.order << ' ' << u.order.turn << ' '
          << u.order.probability << ' ' << u.order.enemyType << '\n';
        if (!u.order.positions.empty()) {
            o << "unit-positions";
            writePoints(o, u.order.positions);
            o << '\n';
        }
        if (!u.order.ids.empty()) {
            o << "unit-ids";
            writeList(o, u.order.ids);
            o << '\n';
        }
    }
    // Building indices close their gaps; the held tech buildings follow.
    std::vector<int> newIndex(s.buildings().size(), -1);
    int next = 0;
    for (std::size_t i = 0; i < s.buildings().size(); ++i) {
        const BattleBuilding& b = s.buildings()[i];
        if (!b.alive) continue;
        newIndex[i] = next++;
        o << "building " << b.id << ' ' << buildingKey(rules, b.type) << ' ' << b.owner << ' ' << b.armor << ' ' << b.pos.x
          << ' ' << b.pos.y << '\n';
    }
    for (const int b : s.techHeld)
        if (b >= 0 && b < static_cast<int>(newIndex.size()) && newIndex[static_cast<std::size_t>(b)] >= 0)
            o << "tech-held " << newIndex[static_cast<std::size_t>(b)] << '\n';
    for (const BattleWall& w : s.walls())
        if (w.alive)
            o << "wall " << w.type << ' ' << w.owner << ' ' << w.armor << ' ' << w.pos.x << ' ' << w.pos.y << ' ' << w.brokenEnd
              << '\n';
    for (const BattleRoad& r : s.roads())
        if (r.alive) o << "road " << r.type << ' ' << r.owner << ' ' << r.pos.x << ' ' << r.pos.y << '\n';
    for (const BattleMine& m : s.mines()) {
        if (!m.alive) continue;
        o << "mine " << m.type << ' ' << m.owner << ' ' << m.pos.x << ' ' << m.pos.y << ' ' << m.armed;
        for (const bool h : m.hidden) o << ' ' << h;
        o << '\n';
    }
    // Wrecks and rubble of this battle included (savegame.md, deliberate deviation).
    for (const BattleRuin& r : s.ruins()) {
        if (r.name.empty() || r.name.find_first_of(" \t\r\n") != std::string::npos) fail("ruin name");
        o << "ruin " << r.name << ' ' << r.pos.x << ' ' << r.pos.y << '\n';
    }
    o << "ai-budget " << aiBudget << '\n';
    if (!rng.empty()) o << "rng " << rng << '\n';
    o << "battle-end\n";
    return o.str();
}

SavedBattle loadBattleSave(const std::string& section, const GlobalRules& rules) {
    SavedBattle out;
    std::istringstream in(section);
    std::string line;
    if (!std::getline(in, line)) fail("empty");
    {
        std::istringstream v(line);
        std::string key;
        int version = 0;
        v >> key >> version;
        if (key != "battle") fail("no battle section");
        if (version < 1 || version > kBattleSaveVersion) fail("unsupported version " + std::to_string(version));
    }
    // Parsed first, built at the end (the map must exist before the objects).
    TerrainMap terrain;
    terrain.size = -1;
    struct UnitRow {
        BattleUnit unit;
        int type = 0, level = 0;
    };
    std::vector<UnitRow> units;
    struct BuildingRow {
        BattleBuilding b;
        int type = 0;
    };
    std::vector<BuildingRow> buildings;
    std::vector<BattleWall> walls;
    std::vector<BattleRoad> roads;
    std::vector<BattleMine> mines;
    std::vector<BattleRuin> ruins;
    std::vector<int> techHeld;
    // Fields copied into the state once it exists.
    struct Fields {
        std::vector<int> players;
        bool computer = true;
        int turn = 0, current = 0, limit = 0;
        Objective objective;
        int holdCount = 0, lastHeld = -1, progress = 0;
        bool cheatWin = false, cheatLose = false;
        std::array<std::vector<Point>, kPlayerSlots> zones{};
        std::array<int, kPlayerSlots> budget{}, income{};
        std::array<std::map<int, int>, kPlayerSlots> levels{};
        std::array<std::vector<int>, kPlayerSlots> automated{}, used{};
        std::array<bool, kPlayerSlots> automatedNeeds{};
        std::array<BattleStats, kPlayerSlots> stats{};
        BattleState::CityIncome cityIncome;
        bool deployNeeds = false;
        std::vector<int> purchasable;
        std::vector<Point> bridges;
        std::vector<BattleState::ScheduledAutomated> scheduled;
        BattleState::AiSupport ai;
        bool counterAttack = false;
        int oneOff = 0;
        std::vector<Reinforcement> reinforcements;
    } f;
    bool ended = false, sawTurn = false;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        const auto space = line.find(' ');
        const std::string key = line.substr(0, space);
        const std::string rest = space == std::string::npos ? std::string() : line.substr(space + 1);
        std::istringstream v(rest);
        BattleHeader& h = out.header;
        if (key == "battle-end") {
            ended = true;
            break;
        } else if (key == "scenario") {
            h.scenario = unescapeSaveText(rest);
        } else if (key == "title") {
            h.title = unescapeSaveText(rest);
        } else if (key == "province-name") {
            h.province = unescapeSaveText(rest);
        } else if (key == "description") {
            h.description = unescapeSaveText(rest);
        } else if (key == "aftermath") {
            h.aftermath = unescapeSaveText(rest);
        } else if (key == "city") {
            h.city = need(v, 1, key)[0] != 0;
        } else if (key == "province") {
            h.provinceNumber = need(v, 1, key)[0];
        } else if (key == "tech-line") {
            h.techLine = need(v, 1, key)[0] != 0;
        } else if (key == "options") {
            const auto x = need(v, 5, key);
            h.options.coop = x[0] != 0;
            h.options.aiDamage = x[1] != 0;
            h.options.difficulty = x[2];
            h.options.computerAi = x[3] != 0;
            h.options.faithfulAutomated = x[4] != 0;
        } else if (key == "map") {
            terrain.size = need(v, 1, key)[0];
            if (terrain.size <= 0 || terrain.size > 1024) fail("map size");
        } else if (key == "tiles") {
            std::string name;
            long long count = 0;
            if (!(v >> name >> count) || count <= 0 || terrain.size <= 0 ||
                terrain.tiles.size() + static_cast<std::size_t>(count) >
                    static_cast<std::size_t>(terrain.size) * static_cast<std::size_t>(terrain.size))
                fail("bad map tiles");
            terrain.tiles.insert(terrain.tiles.end(), static_cast<std::size_t>(count), name);
        } else if (key == "players") {
            f.players = readInts(v);
            for (const int p : f.players) slotOf(p);
        } else if (key == "computer") {
            f.computer = need(v, 1, key)[0] != 0;
        } else if (key == "turn") {
            const auto x = need(v, 3, key);
            f.turn = x[0];
            f.current = slotOf(x[1]);
            f.limit = x[2];
            sawTurn = true;
        } else if (key == "objective") {
            const auto x = need(v, 2, key);
            f.objective.kind = x[0];
            f.objective.turns = x[1];
        } else if (key == "objective-ids") {
            f.objective.ids = readInts(v);
        } else if (key == "objective-zone") {
            f.objective.zone = toPoints(readInts(v));
        } else if (key == "hold") {
            const auto x = need(v, 3, key);
            f.holdCount = x[0];
            f.lastHeld = x[1];
            f.progress = x[2];
        } else if (key == "cheat") {
            const auto x = need(v, 2, key);
            f.cheatWin = x[0] != 0;
            f.cheatLose = x[1] != 0;
        } else if (key == "zone") {
            const int p = slotOf(need(v, 1, key)[0]);
            f.zones[static_cast<std::size_t>(p)] = toPoints(readInts(v));
        } else if (key == "resource") {
            const auto x = need(v, 3, key);
            const auto p = static_cast<std::size_t>(slotOf(x[0]));
            f.budget[p] = x[1];
            f.income[p] = x[2];
        } else if (key == "tech-levels") {
            const auto p = static_cast<std::size_t>(slotOf(need(v, 1, key)[0]));
            const auto x = readInts(v);
            if (x.size() % 2 != 0) fail("bad line: " + key);
            for (std::size_t i = 0; i + 1 < x.size(); i += 2) f.levels[p][x[i]] = x[i + 1];
        } else if (key == "automated") {
            const auto x = need(v, 2, key);
            const auto p = static_cast<std::size_t>(slotOf(x[0]));
            f.automatedNeeds[p] = x[1] != 0;
            f.automated[p] = readInts(v);
        } else if (key == "automated-used") {
            const auto p = static_cast<std::size_t>(slotOf(need(v, 1, key)[0]));
            f.used[p] = readInts(v);
        } else if (key == "stats") {
            std::string kind;
            int p = 0;
            if (!(v >> p >> kind)) fail("bad line: stats");
            BattleStats& st = f.stats[static_cast<std::size_t>(slotOf(p))];
            std::map<int, int>* m = kind == "ud"   ? &st.unitsDestroyed
                                    : kind == "ul" ? &st.unitsLost
                                    : kind == "bd" ? &st.buildingsDestroyed
                                    : kind == "bl" ? &st.buildingsLost
                                                   : nullptr;
            if (!m) fail("statistics kind " + kind);
            const auto x = readInts(v);
            if (x.size() % 2 != 0) fail("bad line: stats");
            for (std::size_t i = 0; i + 1 < x.size(); i += 2) (*m)[x[i]] = x[i + 1];
        } else if (key == "city-income") {
            const auto x = need(v, 5, key);
            f.cityIncome = {x[0], x[1], x[2], x[3], x[4]};
        } else if (key == "deploy-needs-buildings") {
            f.deployNeeds = need(v, 1, key)[0] != 0;
        } else if (key == "purchasable") {
            f.purchasable = readInts(v);
        } else if (key == "bridge-sites") {
            f.bridges = toPoints(readInts(v));
        } else if (key == "scheduled") {
            const auto x = need(v, 6, key);
            f.scheduled.push_back({x[0], {x[1], x[2]}, {x[3], x[4]}, x[5]});
        } else if (key == "ai-support") {
            const auto x = need(v, 3, key);
            f.ai.present = x[0] != 0;
            f.ai.chance = x[1];
            f.ai.budget = x[2];
            f.ai.types = readInts(v);
        } else if (key == "counter-attack") {
            f.counterAttack = need(v, 1, key)[0] != 0;
        } else if (key == "one-off") {
            f.oneOff = need(v, 1, key)[0];
        } else if (key == "reinforcement") {
            const auto x = need(v, 6, key);
            f.reinforcements.push_back({x[0], x[1], {x[2], x[3]}, x[4], x[5] != 0});
        } else if (key == "unit") {
            const auto x = need(v, 14, key);
            UnitRow r;
            r.unit.id = x[0];
            r.type = x[1];
            r.level = x[2];
            r.unit.owner = slotOf(x[3]);
            r.unit.ap = x[4];
            r.unit.negAp = x[5];
            r.unit.armor = x[6];
            r.unit.pos = {x[7], x[8]};
            r.unit.facing = x[9] & 7;
            r.unit.order.order = x[10];
            r.unit.order.turn = x[11];
            r.unit.order.probability = x[12];
            r.unit.order.enemyType = x[13];
            units.push_back(std::move(r));
        } else if (key == "unit-positions") {
            if (units.empty()) fail("order positions without a unit");
            units.back().unit.order.positions = toPoints(readInts(v));
        } else if (key == "unit-ids") {
            if (units.empty()) fail("order ids without a unit");
            units.back().unit.order.ids = readInts(v);
        } else if (key == "building") {
            const auto x = need(v, 6, key);
            BuildingRow r;
            r.b.id = x[0];
            r.type = x[1];
            r.b.owner = slotOf(x[2]);
            r.b.armor = x[3];
            r.b.pos = {x[4], x[5]};
            buildings.push_back(r);
        } else if (key == "tech-held") {
            techHeld.push_back(need(v, 1, key)[0]);
        } else if (key == "wall") {
            const auto x = need(v, 6, key);
            walls.push_back({x[0], slotOf(x[1]), x[2], {x[3], x[4]}, x[5] != 0, true});
        } else if (key == "road") {
            const auto x = need(v, 4, key);
            roads.push_back({x[0], slotOf(x[1]), {x[2], x[3]}, true});
        } else if (key == "mine") {
            const auto x = need(v, 5 + kPlayerSlots, key);
            BattleMine m;
            m.type = x[0];
            m.owner = slotOf(x[1]);
            m.pos = {x[2], x[3]};
            m.armed = x[4] != 0;
            for (std::size_t i = 0; i < m.hidden.size(); ++i) m.hidden[i] = x[5 + i] != 0;
            mines.push_back(m);
        } else if (key == "ruin") {
            BattleRuin r;
            if (!(v >> r.name >> r.pos.x >> r.pos.y)) fail("bad line: ruin");
            ruins.push_back(std::move(r));
        } else if (key == "ai-budget") {
            out.aiBudget = need(v, 1, key)[0];
        } else if (key == "rng") {
            out.rng = rest;
        } else {
            fail("unknown line '" + key + "'");
        }
    }
    if (!ended) fail("truncated");
    if (terrain.size <= 0 ||
        terrain.tiles.size() != static_cast<std::size_t>(terrain.size) * static_cast<std::size_t>(terrain.size))
        fail("incomplete map");
    if (!sawTurn || f.players.empty()) fail("no turn state");

    auto state = std::make_shared<BattleState>(rules, std::move(terrain));
    BattleState& s = *state;
    auto checkInside = [&](Point p, const char* what) {
        if (!s.inside(p)) fail(std::string(what) + " off the map");
    };
    for (const BattleRoad& r : roads) {
        if (!rules.roads.count(r.type)) fail("unknown road type " + std::to_string(r.type));
        checkInside(r.pos, "road");
        s.addRoad(r);
    }
    for (const BattleWall& w : walls) {
        if (!rules.walls.count(w.type)) fail("unknown wall type " + std::to_string(w.type));
        checkInside(w.pos, "wall");
        s.addWall(w);
    }
    for (const BattleMine& m : mines) {
        if (!rules.mines.count(m.type)) fail("unknown mine type " + std::to_string(m.type));
        checkInside(m.pos, "mine");
        s.addMine(m);
    }
    for (BattleRuin& r : ruins) {
        checkInside(r.pos, "ruin");
        s.addRuin(std::move(r));
    }
    for (BuildingRow& r : buildings) {
        const auto t = rules.buildings.find(r.type);
        if (t == rules.buildings.end()) fail("unknown building type " + std::to_string(r.type));
        checkInside(r.b.pos, "building");
        r.b.type = &t->second;
        s.addBuilding(r.b);
    }
    for (UnitRow& r : units) {
        const auto t = rules.units.find({r.type, r.level});
        if (t == rules.units.end())
            fail("unknown unit type " + std::to_string(r.type) + " level " + std::to_string(r.level));
        checkInside(r.unit.pos, "unit");
        r.unit.type = &t->second;
        s.addUnit(r.unit);
    }
    for (const int b : techHeld)
        if (b < 0 || b >= static_cast<int>(s.buildings().size())) fail("tech building index");
    s.techHeld = std::move(techHeld);
    s.players = std::move(f.players);
    s.computerPlayer = f.computer;
    s.turn = f.turn;
    s.currentPlayer = f.current;
    s.turnLimit = f.limit;
    s.objective = std::move(f.objective);
    s.holdCount = f.holdCount;
    s.lastHeldTurn = f.lastHeld;
    s.objectiveProgress = f.progress;
    s.cheatWin = f.cheatWin;
    s.cheatLose = f.cheatLose;
    s.deployZone = std::move(f.zones);
    s.deployBudget = f.budget;
    s.income = f.income;
    s.techLevels = std::move(f.levels);
    s.automated = std::move(f.automated);
    s.automatedUsed = std::move(f.used);
    s.automatedNeedsBuildings = f.automatedNeeds;
    s.stats = std::move(f.stats);
    s.cityIncome = f.cityIncome;
    s.deployNeedsBuildings = f.deployNeeds;
    s.purchasable = std::move(f.purchasable);
    s.bridgeSites = std::move(f.bridges);
    s.scheduledAutomated = std::move(f.scheduled);
    s.aiSupport = std::move(f.ai);
    s.counterAttack = f.counterAttack;
    s.oneOffDeployment = f.oneOff;
    s.reinforcements = std::move(f.reinforcements);
    s.deploying = false;
    s.over = false;
    out.state = std::move(state);
    return out;
}

std::unique_ptr<BattleServer> restoreServer(const SavedBattle& saved) {
    if (!saved.state) throw std::runtime_error("battle save: no state");
    auto server = std::make_unique<BattleServer>(*saved.state, saved.header.options, 0u);
    server->restore(saved.rng, saved.aiBudget);
    return server;
}

} // namespace openday::game
