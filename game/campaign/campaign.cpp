#include "campaign/campaign.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>

#include "battle/movement.hpp"
#include "map/terrain_map.hpp"

namespace openday::game {
namespace {

int toInt(const data::Row& row, int col) { return row.isNull(col) ? 0 : static_cast<int>(row.integer(col)); }

// The deployment centre a colony offers (city-building.md 2.1).
int colonyDeployCentre(int colony) {
    static constexpr int kTypes[4] = {131, 173, 174, 175};
    return kTypes[std::clamp(colony, 1, 4) - 1];
}

int facingToward(Point from, Point to) {
    // Same convention as the battle: 0 = +y, then clockwise in screen terms.
    static constexpr Point kStep[8] = {{0, 1}, {-1, 1}, {-1, 0}, {-1, -1}, {0, -1}, {1, -1}, {1, 0}, {1, 1}};
    const int dx = (to.x > from.x) - (to.x < from.x), dy = (to.y > from.y) - (to.y < from.y);
    for (int i = 0; i < 8; ++i)
        if (kStep[i].x == dx && kStep[i].y == dy) return i;
    return 0;
}


struct ScenarioText {
    std::string title, province, description, aftermath;
    int oneOff = 0, reinf = 0;
    bool counterAttack = false;
};

ScenarioText readScenarioInfo(const data::SqliteDb& db) {
    ScenarioText t;
    db.forEach("SELECT TitleEnglish, provincenameEnglish, descriptionEnglish, AftermathEnglish, \"One Off Deployment\", "
               "ReinfRes, CounterAttack FROM \"Scenario Info\"",
               [&](const data::Row& r) {
                   t.title = r.text(0);
                   t.province = r.text(1);
                   t.description = r.text(2);
                   t.aftermath = r.text(3);
                   t.oneOff = toInt(r, 4);
                   t.reinf = toInt(r, 5);
                   t.counterAttack = r.integer(6) != 0;
               });
    return t;
}

const UnitType* lowestLevel(const GlobalRules& rules, int typeId) {
    const auto it = rules.units.lower_bound({typeId, -1000});
    return it != rules.units.end() && it->first.first == typeId ? &it->second : nullptr;
}

} // namespace

int interpolate(int easy, int hard, int difficulty) { return easy + (hard - easy) * difficulty / 10; }

int scalePercent(int value, int multiplier) { return value < 0 ? 0 : value * multiplier / 100; }

// --- Colony ------------------------------------------------------------------

Campaign Campaign::create(const CampaignResources& res, const std::string& name, int difficulty, std::mt19937& rng) {
    Campaign c;
    c.rules = res.rules;
    c.playerName = name;
    c.difficulty = std::clamp(difficulty, 0, 10);
    c.startColony(res, 1, rng);
    return c;
}

void Campaign::startColony(const CampaignResources& res, int number, std::mt19937& rng) {
    rules = res.rules;
    colonyNumber = number;
    colony = loadColony(data::SqliteDb(res.colony(number)));
    player.credits = colony.startCash; // credits do not carry over
    provinces.assign(colony.provinces.size(), {});
    for (std::size_t i = 0; i < colony.provinces.size(); ++i) {
        const int p = colony.provinces[i].number;
        if (p == colony.playerCity) provinces[i] = {true, 1};
        else provinces[i] = {false, static_cast<int>(rng() % 2) + 1};
    }
    const std::string cityName = "s" + std::to_string(number) + "-" + std::to_string(colony.playerCity) + "-1";
    city = loadCity(data::SqliteDb(res.scenario(cityName)), cityName, *res.rules);
    city.removeUnusedSlots(1);
    built.clear();
    battleProvince = 0;
    counterAttack = false;
    pendingAttack.clear();
    state = number == 1 ? CampaignState::FirstColony : CampaignState::NewColony;
}

void Campaign::reloadColony(const CampaignResources& res) {
    rules = res.rules;
    colony = loadColony(data::SqliteDb(res.colony(colonyNumber)));
}

int Campaign::deployMultiplier() const {
    return interpolate(colony.deployPercent[0], colony.deployPercent[2], difficulty);
}

int Campaign::creditMultiplier() const {
    return rules ? interpolate(rules->creditPercent[0], rules->creditPercent[2], difficulty) : 100;
}

bool Campaign::captured(int province) const {
    return province >= 1 && province <= static_cast<int>(provinces.size()) &&
           provinces[static_cast<std::size_t>(province - 1)].captured;
}

bool Campaign::attackable(int province) const {
    if (province < 1 || province > static_cast<int>(provinces.size()) || captured(province)) return false;
    return std::any_of(colony.adjacent.begin(), colony.adjacent.end(),
                       [&](const std::pair<int, int>& e) { return e.first == province && captured(e.second); });
}

int Campaign::provinceCredits(int province) const {
    const Province* p = colony.province(province);
    return p ? scalePercent(p->credits, creditMultiplier()) : 0;
}

std::string Campaign::provinceScenario(int province) const {
    const int v = province >= 1 && province <= static_cast<int>(provinces.size())
                      ? provinces[static_cast<std::size_t>(province - 1)].variant
                      : 1;
    return "s" + std::to_string(colonyNumber) + "-" + std::to_string(province) + "-" + std::to_string(v);
}

std::vector<int> Campaign::deployableTypes(const GlobalRules& r) const { return game::deployableTypes(city, r, kCampaignPlayer); }

std::vector<int> Campaign::automatedTypes(const GlobalRules& r) const { return game::automatedTypes(city, r, kCampaignPlayer); }

int Campaign::techLevel(int typeId) const {
    const auto it = player.techLevels.find(typeId);
    return it == player.techLevels.end() ? 0 : it->second;
}

int Campaign::cityBudget(const GlobalRules& r) const {
    const int deploy = colonyDeployCentre(colonyNumber);
    const auto dt = r.buildings.find(deploy);
    const auto hq = r.buildings.find(kHeadquarters);
    const int raw = (dt == r.buildings.end() ? 0 : dt->second.resProduction * city.count(kCampaignPlayer, deploy)) +
                    (city.owns(kCampaignPlayer, kHeadquarters) && hq != r.buildings.end() ? hq->second.resProduction : 0);
    return scalePercent(raw, deployMultiplier());
}

bool Campaign::upgrade(const GlobalRules& r, int typeId) {
    const int level = techLevel(typeId);
    const auto row = r.units.find({typeId, level});
    if (row == r.units.end() || row->second.costToNextTL <= 0) return false;
    if (player.tech < row->second.costToNextTL || !r.units.count({typeId, level + 1})) return false;
    player.tech -= row->second.costToNextTL;
    player.techLevels[typeId] = level + 1;
    return true;
}

// --- Transitions ----------------------------------------------------------------

void Campaign::introDone() {
    if (state == CampaignState::FirstColony) state = CampaignState::FirstCityAttack;
    else if (state == CampaignState::NewColony) {
        state = CampaignState::Strategic;
        built.clear();
    }
}

bool Campaign::attack(int province) {
    if (state != CampaignState::Strategic || !attackable(province)) return false;
    battleProvince = province;
    state = CampaignState::Tactic;
    return true;
}

void Campaign::counterAttackVideoDone() {
    if (state == CampaignState::PreCounterAttack) state = CampaignState::CounterAttack;
}

bool Campaign::endsCampaign(bool success) const {
    if (state == CampaignState::Tactic) return colonyNumber == 4 && battleProvince == colony.alienCity;
    if (state == CampaignState::FirstCityAttack || state == CampaignState::CounterAttack) return !success;
    return false;
}

void Campaign::battleFinished(const GlobalRules& r, const BattleOutcome& o) {
    const bool cityBattle = state == CampaignState::FirstCityAttack || state == CampaignState::CounterAttack;
    if (state != CampaignState::Tactic && !cityBattle) return;
    if (endsCampaign(o.success)) {
        if (o.success) {
            player.unitsDestroyed += o.unitsDestroyed;
            player.unitsLost += o.unitsLost;
            player.buildingsDestroyed += o.buildingsDestroyed;
            player.buildingsLost += o.buildingsLost;
        }
        state = o.success ? CampaignState::Won : CampaignState::Lost;
        return;
    }
    if (!o.success) {
        // A lost province battle: the province switches to its other map.
        ProvinceState& p = provinces[static_cast<std::size_t>(battleProvince - 1)];
        p.variant = p.variant >= 2 ? 1 : p.variant + 1;
        counterAttack = false;
        pendingAttack.clear();
        state = CampaignState::Strategic;
        built.clear();
        return;
    }
    player.unitsDestroyed += o.unitsDestroyed;
    player.unitsLost += o.unitsLost;
    player.buildingsDestroyed += o.buildingsDestroyed;
    player.buildingsLost += o.buildingsLost;
    if (cityBattle) {
        if (o.finalState) {
            // campaign.md 6.6: towers return, everything standing is repaired,
            // units, ruins and burned terrain go.
            const BattleState& s = *o.finalState;
            std::vector<CityBuilding> buildings;
            for (const BattleBuilding& b : s.buildings()) {
                if (!b.alive) continue;
                const int i = city.buildingById(b.id);
                if (i < 0) continue;
                CityBuilding cb = city.buildings[static_cast<std::size_t>(i)];
                cb.armor = b.type->armor;
                buildings.push_back(cb);
            }
            for (const BattleUnit& u : s.units()) {
                if (!u.alive || u.owner == 0) continue;
                for (const auto& [id, bt] : r.buildings)
                    if (isTower(bt) && bt.transformIntoUnit == u.type->typeId && bt.race == 0) {
                        buildings.push_back({city.nextBuildingId++, id, u.owner, bt.armor, u.pos, bt.sizeX, bt.sizeY});
                        break;
                    }
            }
            city.buildings = std::move(buildings);
            city.walls.clear();
            for (const BattleWall& w : s.walls())
                if (w.alive) {
                    const auto t = r.walls.find(w.type);
                    city.walls.push_back({w.type, w.owner, t == r.walls.end() ? w.armor : t->second.armor, w.pos, w.brokenEnd});
                }
            city.roads.clear();
            for (const BattleRoad& rd : s.roads())
                if (rd.alive) city.roads.push_back({rd.type, rd.owner, rd.pos});
            city.mines.clear();
            for (const BattleMine& m : s.mines())
                if (m.alive) city.mines.push_back({m.type, m.owner, m.pos, m.armed, m.hidden});
        }
        if (state == CampaignState::FirstCityAttack) battleProvince = colony.playerCity;
        pendingAttack.clear();
        counterAttack = false;
    } else {
        player.tech += o.techFound;
        if (counterAttack) {
            state = CampaignState::PreCounterAttack;
            return;
        }
    }
    // Strategic results: the province is taken and its credits paid.
    provinces[static_cast<std::size_t>(battleProvince - 1)].captured = true;
    player.credits += provinceCredits(battleProvince);
    state = CampaignState::StrategicResults;
}

bool Campaign::resultsDone(const CampaignResources& res, std::mt19937& rng) {
    if (state != CampaignState::StrategicResults) return false;
    if (battleProvince == colony.alienCity) {
        if (colonyNumber < 4) {
            startColony(res, colonyNumber + 1, rng);
            return true;
        }
        state = CampaignState::Won;
        return false;
    }
    battleProvince = 0;
    built.clear();
    state = CampaignState::Strategic;
    return false;
}

// --- City zones -------------------------------------------------------------------

std::array<std::vector<Point>, kPlayerSlots> cityZones(const City& city, const GlobalRules& rules) {
    enum : int { kFree = 0, kBlocked = 0x100, kAi = 0x80 };
    auto zone = [](int k) { return 0x40 + 0x10 * (k - 1); };
    auto building = [](int k) { return 0x08 + 2 * (k - 1); };
    const int n = city.size();
    std::vector<int> f(static_cast<std::size_t>(n * n), kFree);
    auto at = [&](int x, int y) -> int& { return f[static_cast<std::size_t>(y * n + x)]; };
    auto set = [&](Point p, int v) {
        if (city.inside(p) && at(p.x, p.y) == kFree) at(p.x, p.y) = v;
    };
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) {
            const TerrainClass c = rules.classOf(city.terrain.at(x, y));
            if (c == TerrainClass::Unknown || c == TerrainClass::Mountains || c == TerrainClass::Crack) at(x, y) = kBlocked;
        }
    for (const CityBuilding& b : city.buildings) {
        if (b.owner < 1 || b.owner > 4) continue;
        const auto t = rules.buildings.find(b.type);
        const bool isT = t != rules.buildings.end() && isTower(t->second);
        for (int dy = 0; dy < b.h; ++dy)
            for (int dx = 0; dx < b.w; ++dx) set({b.pos.x + dx, b.pos.y + dy}, isT ? zone(b.owner) : building(b.owner));
    }
    for (const CityWall& w : city.walls) set(w.pos, kBlocked);
    for (const CityMine& m : city.mines) set(m.pos, kBlocked);
    for (const CityRoad& r : city.roads)
        if (r.owner >= 1 && r.owner <= 4) set(r.pos, zone(r.owner));
    // The window of radius r: the last column and row are never inspected.
    auto window = [&](int x, int y, int r, const std::function<bool(int)>& test) {
        for (int yy = std::max(0, y - r); yy <= std::min(y + r, n - 2); ++yy)
            for (int xx = std::max(0, x - r); xx <= std::min(x + r, n - 2); ++xx)
                if (test(at(xx, yy))) return true;
        return false;
    };
    for (int k = 1; k <= 4; ++k)
        for (int y = 0; y < n; ++y)
            for (int x = 0; x < n; ++x)
                if (at(x, y) == kFree && window(x, y, 3, [&](int v) { return v == building(k); })) at(x, y) = zone(k);
    auto isZone = [&](int v) { return v == zone(1) || v == zone(2) || v == zone(3) || v == zone(4); };
    std::vector<Point> ai;
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) {
            if (at(x, y) != kFree) continue;
            const bool corner = (x <= 1 || x >= n - 2) && (y <= 1 || y >= n - 2);
            if ((!window(x, y, 11, isZone) || corner) && window(x, y, 14, isZone)) at(x, y) = kAi;
        }
    std::array<std::vector<Point>, kPlayerSlots> out{};
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) {
            const int v = at(x, y);
            if (v == kAi) out[0].push_back({x, y});
            for (int k = 1; k <= 4; ++k)
                if (v == zone(k)) out[static_cast<std::size_t>(k)].push_back({x, y});
        }
    return out;
}

// --- Battles ----------------------------------------------------------------------

CampaignBattle Campaign::battle(const CampaignResources& res, std::mt19937& rng) {
    const GlobalRules& r = *res.rules;
    if (state == CampaignState::Tactic) {
        std::string name = provinceScenario(battleProvince);
        std::filesystem::path path = res.scenario(name);
        if (path.empty()) { // a missing variant falls back to variant 1
            name = "s" + std::to_string(colonyNumber) + "-" + std::to_string(battleProvince) + "-1";
            path = res.scenario(name);
        }
        if (path.empty()) throw std::runtime_error("scenario " + name + " not found");
        return provinceBattle(path, name, r);
    }
    return cityBattle(res, rng);
}

CampaignBattle Campaign::provinceBattle(const std::filesystem::path& path, const std::string& name, const GlobalRules& r) {
    CampaignBattle b;
    b.options.aiDamage = true; // campaign battles only (campaign.md 4.1)
    b.options.difficulty = difficulty;
    const std::vector<int> deployable = deployableTypes(r);
    const std::map<int, int> levels = player.techLevels;
    const int multiplier = deployMultiplier();
    // Automated units: the player's from his city, the computer player's
    // from the colony (campaign.md 4.1 steps 4-5).
    const std::vector<int> automated = automatedTypes(r);
    BattleState::AiSupport aiSupport{true, colony.aiAutoChance, colony.aiAutoBudget, colony.aiAutomated};
    {
        const data::SqliteDb db(path);
        const ScenarioText text = readScenarioInfo(db);
        counterAttack = text.counterAttack;
        pendingAttack.clear();
        if (counterAttack && db.hasTable("Counter Attack"))
            db.forEach("SELECT UnitID, type, posX, posY, turn, \"order\" FROM \"Counter Attack\" ORDER BY Counter",
                       [&](const data::Row& row) {
                           pendingAttack.push_back({toInt(row, 0), toInt(row, 1), {toInt(row, 2), toInt(row, 3)},
                                                    toInt(row, 4), row.integer(5) != 0});
                       });
        int deployCentres = 0;
        for (const CityBuilding& cb : city.buildings)
            if (cb.owner == kCampaignPlayer && isDeployCentre(cb.type)) ++deployCentres;
        const int budget = scalePercent(text.oneOff, multiplier);
        const int income = scalePercent(text.reinf + text.reinf * 2 * deployCentres / 100, multiplier);
        b.database = path;
        b.scenario = name;
        b.province = battleProvince;
        b.title = text.title;
        b.provinceName = text.province;
        b.description = text.description;
        b.aftermath = text.aftermath;
        b.prepare = [deployable, levels, budget, income, automated, aiSupport](BattleState& s) {
            // One human player: the other slots' units and buildings join him,
            // and so do their deployment zones (campaign.md 4.1).
            for (std::size_t i = 0; i < s.units().size(); ++i) {
                BattleUnit& u = s.unit(static_cast<int>(i));
                if (u.owner >= 2) u.owner = kCampaignPlayer;
            }
            for (std::size_t i = 0; i < s.buildings().size(); ++i) {
                BattleBuilding& bd = s.building(static_cast<int>(i));
                if (bd.owner >= 2) bd.owner = kCampaignPlayer;
            }
            for (int p = 2; p < kPlayerSlots; ++p) {
                for (const Point z : s.deployZone[static_cast<std::size_t>(p)])
                    if (!s.inDeployZone(kCampaignPlayer, z)) s.deployZone[kCampaignPlayer].push_back(z);
                s.deployZone[static_cast<std::size_t>(p)].clear();
            }
            s.players = {0, kCampaignPlayer};
            s.purchasable = deployable;
            s.techLevels[kCampaignPlayer] = levels;
            s.deployBudget[kCampaignPlayer] = budget;
            s.income[kCampaignPlayer] = income;
            s.automated = {};
            s.automated[kCampaignPlayer] = automated;
            s.automatedNeedsBuildings = {};
            s.aiSupport = aiSupport;
            s.deploying = !s.deployZone[kCampaignPlayer].empty();
        };
        return b;
    }
}

CampaignBattle Campaign::cityBattleSetup(const GlobalRules& r) const {
    if (state != CampaignState::FirstCityAttack && state != CampaignState::CounterAttack)
        throw std::runtime_error("no battle in this campaign state");
    CampaignBattle b;
    b.options.aiDamage = true; // campaign battles only (campaign.md 4.1)
    b.options.difficulty = difficulty;
    const std::vector<int> deployable = deployableTypes(r);
    const std::map<int, int> levels = player.techLevels;
    const int multiplier = deployMultiplier();
    const std::vector<int> automated = automatedTypes(r);
    BattleState::AiSupport aiSupport{true, colony.aiAutoChance, colony.aiAutoBudget, colony.aiAutomated};
    b.scenario = city.scenario;
    b.city = true;
    b.province = state == CampaignState::FirstCityAttack ? colony.playerCity : battleProvince;
    const int budget = cityBudget(r);
    const int deployType = colonyDeployCentre(colonyNumber);
    BattleState::CityIncome ci;
    ci.deployType = deployType;
    if (const auto t = r.buildings.find(deployType); t != r.buildings.end()) ci.deployProduction = t->second.resProduction;
    if (const auto t = r.buildings.find(kHeadquarters); t != r.buildings.end()) ci.hqProduction = t->second.resProduction;
    ci.multiplier = multiplier;
    b.prepare = [deployable, levels, budget, ci, automated, aiSupport](BattleState& s) {
        s.players = {0, kCampaignPlayer};
        s.purchasable = deployable;
        s.deployNeedsBuildings = true;
        s.techLevels[kCampaignPlayer] = levels;
        s.deployBudget[kCampaignPlayer] = budget;
        s.cityIncome = ci;
        // The usable list follows the buildings still standing (campaign.md 6.4).
        s.automated = {};
        s.automated[kCampaignPlayer] = automated;
        s.automatedNeedsBuildings = {};
        s.automatedNeedsBuildings[kCampaignPlayer] = true;
        s.aiSupport = aiSupport;
        s.counterAttack = true; // the aliens attack the player's city
        s.deploying = !s.deployZone[kCampaignPlayer].empty();
    };
    return b;
}

CampaignBattle Campaign::cityBattle(const CampaignResources& res, std::mt19937& rng) {
    const GlobalRules& r = *res.rules;
    CampaignBattle b = cityBattleSetup(r);

    // A battle on the city (campaign.md 6): the city is written into a copy
    // of its scenario database.
    const std::filesystem::path source = res.scenario(city.scenario);
    if (source.empty()) throw std::runtime_error("scenario " + city.scenario + " not found");
    std::filesystem::create_directories(res.tempDir);
    const std::filesystem::path path = res.tempDir / "city-battle.sqlite";
    std::filesystem::copy_file(source, path, std::filesystem::copy_options::overwrite_existing);
    const std::vector<AttackRow> attackers = state == CampaignState::FirstCityAttack ? city.counterAttack : pendingAttack;
    {
        data::SqliteDb db(path, data::SqliteDb::Mode::ReadWriteCreate);
        db.exec("BEGIN");
        for (const char* t : {"Buildings On Map", "Roads On Map", "Walls On Map", "Mines On Map", "Units On Map", "Ruins On Map",
                              "Deployment", "Reinforcement", "Objective", "Objectives ID List", "Objectives Pos List"})
            if (db.hasTable(t)) db.exec("DELETE FROM " + data::quoteIdent(t));
        const int n = city.size();
        const Point centre{n / 2, n / 2};
        int unitId = city.nextUnitId;
        for (const AttackRow& a : attackers) unitId = std::max(unitId, a.unitId + 1);
        for (const CityBuilding& cb : city.buildings) {
            const auto t = r.buildings.find(cb.type);
            if (t == r.buildings.end()) continue;
            if (isTower(t->second)) { // towers fight as units
                const UnitType* u = lowestLevel(r, t->second.transformIntoUnit);
                if (!u) continue;
                db.exec("INSERT INTO \"Units On Map\" (ID, unitType, unitTL, playerOwner, facing, currentAR, currentAP, negAP, "
                        "PosX, PosY, OrderID, Turn, Probability, EnemyUnitType) VALUES (" +
                        std::to_string(unitId++) + "," + std::to_string(u->typeId) + "," + std::to_string(u->techLevel) + "," +
                        std::to_string(cb.owner) + "," + std::to_string(facingToward(cb.pos, centre)) + "," +
                        std::to_string(u->armor) + "," + std::to_string(u->actionPoints) + ",0," + std::to_string(cb.pos.x) + "," +
                        std::to_string(cb.pos.y) + ",0,0,100,65535)");
                continue;
            }
            db.exec("INSERT INTO \"Buildings On Map\" (id, buildingType, playerOwner, currentAR, PosX, PosY, IsAiTarget) VALUES (" +
                    std::to_string(cb.id) + "," + std::to_string(cb.type) + "," + std::to_string(cb.owner) + "," +
                    std::to_string(cb.armor) + "," + std::to_string(cb.pos.x) + "," + std::to_string(cb.pos.y) + ",1)");
        }
        for (const CityRoad& rd : city.roads)
            db.exec("INSERT INTO \"Roads On Map\" (roadtype, playerOwner, PosX, PosY) VALUES (" + std::to_string(rd.type) + "," +
                    std::to_string(rd.owner) + "," + std::to_string(rd.pos.x) + "," + std::to_string(rd.pos.y) + ")");
        for (const CityWall& w : city.walls)
            db.exec("INSERT INTO \"Walls On Map\" (roadtype, playerOwner, currentAR, posX, posY, isDamaged) VALUES (" +
                    std::to_string(w.type) + "," + std::to_string(w.owner) + "," + std::to_string(w.armor) + "," +
                    std::to_string(w.pos.x) + "," + std::to_string(w.pos.y) + "," + (w.brokenEnd ? "1" : "0") + ")");
        for (const CityMine& m : city.mines) {
            std::string v = std::to_string(m.type) + "," + std::to_string(m.owner) + "," + std::to_string(m.pos.x) + "," +
                            std::to_string(m.pos.y) + "," + (m.armed ? "1" : "0");
            for (int p = 0; p < kPlayerSlots; ++p) v += m.hidden[static_cast<std::size_t>(p)] ? ",1" : ",0";
            db.exec("INSERT INTO \"Mines On Map\" (typeID, playerOwner, PosX, PosY, armed, hidden_AI, hidden_1, hidden_2, "
                    "hidden_3, hidden_4) VALUES (" + v + ")");
        }
        db.exec("INSERT INTO \"Objective\" (ObjectifId, TurnNumber) VALUES (7, 99)");
        // Zones and the attackers' positions (campaign.md 6.2, 6.3).
        const auto zones = cityZones(city, r);
        for (int p = 1; p < kPlayerSlots; ++p)
            for (const Point z : zones[static_cast<std::size_t>(p)])
                db.exec("INSERT INTO \"Deployment\" (player, PosX, PosY, Turn) VALUES (" + std::to_string(p) + "," +
                        std::to_string(z.x) + "," + std::to_string(z.y) + ",0)");
        db.exec("COMMIT");

        // The tile check needs the map with its objects.
        const TerrainMap terrain = loadTerrainMap(db);
        BattleState probeState = loadBattle(db, r, terrain);
        std::vector<Point> pool = zones[0];
        for (std::size_t i = 0; i < pool.size(); ++i) std::swap(pool[i], pool[rng() % pool.size()]);
        struct Placed {
            AttackRow row;
            bool found = false;
        };
        std::vector<Placed> placed;
        for (int water = 0; water < 2; ++water) {
            int firstFound = -1;
            for (const AttackRow& a : attackers) {
                const UnitType* u = lowestLevel(r, a.type);
                if (!u) continue;
                const bool isWater = u->movement == 4 || u->movement == 6;
                if (isWater != (water == 1)) continue;
                Placed pl{a, false};
                BattleUnit probe;
                probe.type = u;
                for (std::size_t i = 0; i < pool.size(); ++i)
                    if (evaluateTile(probeState, probe, pool[i]).enterable) {
                        pl.row.pos = pool[i];
                        pl.found = true;
                        const Point t = pool[i];
                        std::erase_if(pool, [&](Point p) { return std::abs(p.x - t.x) <= 1 && std::abs(p.y - t.y) <= 1; });
                        break;
                    }
                if (pl.found && firstFound < 0) firstFound = static_cast<int>(placed.size());
                if (!pl.found && firstFound >= 0) pl.row.pos = placed[static_cast<std::size_t>(firstFound)].row.pos;
                placed.push_back(pl);
            }
        }
        db.exec("BEGIN");
        std::set<std::pair<int, int>> used;
        for (const Placed& pl : placed) {
            const AttackRow& a = pl.row;
            const UnitType* u = lowestLevel(r, a.type);
            const bool now = a.turn == 0 && pl.found && used.insert({a.pos.x, a.pos.y}).second;
            if (now)
                db.exec("INSERT INTO \"Units On Map\" (ID, unitType, unitTL, playerOwner, facing, currentAR, currentAP, negAP, "
                        "PosX, PosY, OrderID, Turn, Probability, EnemyUnitType) VALUES (" +
                        std::to_string(a.unitId) + "," + std::to_string(a.type) + "," + std::to_string(u->techLevel) + ",0," +
                        std::to_string(facingToward(a.pos, centre)) + "," + std::to_string(u->armor) + "," +
                        std::to_string(u->actionPoints) + ",0," + std::to_string(a.pos.x) + "," + std::to_string(a.pos.y) +
                        ",0,0,100,65535)");
            else
                db.exec("INSERT INTO \"Reinforcement\" (UnitID, type, posX, posY, turn, \"order\") VALUES (" +
                        std::to_string(a.unitId) + "," + std::to_string(a.type) + "," + std::to_string(a.pos.x) + "," +
                        std::to_string(a.pos.y) + "," + std::to_string(a.turn) + "," + (a.order ? "1" : "0") + ")");
        }
        db.exec("COMMIT");
        const ScenarioText text = readScenarioInfo(db);
        b.title = text.title;
        b.provinceName = text.province;
        b.description = text.description;
        b.aftermath = text.aftermath;
    }
    b.database = path;
    return b;
}

void readScenarioTexts(const data::SqliteDb& db, CampaignBattle& b) {
    const ScenarioText text = readScenarioInfo(db);
    b.title = text.title;
    b.provinceName = text.province;
    b.description = text.description;
    b.aftermath = text.aftermath;
}

// --- Saving -------------------------------------------------------------------------

std::string saveCampaign(const Campaign& c, const std::string& description, const std::string& battleSection) {
    std::ostringstream o;
    o << "OPENDAY-CAMPAIGN " << kCampaignSaveVersion << "\n";
    o << "description " << description << "\n";
    o << "name " << c.playerName << "\n";
    o << "difficulty " << c.difficulty << "\n";
    o << "colony " << c.colonyNumber << "\n";
    o << "state " << static_cast<int>(c.state) << "\n";
    o << "province " << c.battleProvince << "\n";
    o << "credits " << c.player.credits << "\n";
    o << "tech " << c.player.tech << "\n";
    o << "stats " << c.player.unitsDestroyed << " " << c.player.unitsLost << " " << c.player.buildingsDestroyed << " "
      << c.player.buildingsLost << "\n";
    for (const auto& [type, level] : c.player.techLevels) o << "level " << type << " " << level << "\n";
    for (const ProvinceState& p : c.provinces) o << "prov " << (p.captured ? 1 : 0) << " " << p.variant << "\n";
    o << "city " << c.city.scenario << "\n";
    o << "nextids " << c.city.nextBuildingId << " " << c.city.nextUnitId << "\n";
    for (const CityBuilding& b : c.city.buildings)
        o << "building " << b.id << " " << b.type << " " << b.owner << " " << b.armor << " " << b.pos.x << " " << b.pos.y << "\n";
    for (const CityRoad& r : c.city.roads) o << "road " << r.type << " " << r.owner << " " << r.pos.x << " " << r.pos.y << "\n";
    for (const CityWall& w : c.city.walls)
        o << "wall " << w.type << " " << w.owner << " " << w.armor << " " << w.pos.x << " " << w.pos.y << " "
          << (w.brokenEnd ? 1 : 0) << "\n";
    for (const CityMine& m : c.city.mines) {
        o << "mine " << m.type << " " << m.owner << " " << m.pos.x << " " << m.pos.y << " " << (m.armed ? 1 : 0);
        for (const bool h : m.hidden) o << " " << (h ? 1 : 0);
        o << "\n";
    }
    for (const HqSite& h : c.city.hqSites) o << "hq " << h.slot << " " << h.pos.x << " " << h.pos.y << "\n";
    for (const int id : c.built.buildings) o << "built-building " << id << "\n";
    for (const auto& [x, y] : c.built.roads) o << "built-road " << x << " " << y << "\n";
    for (const auto& [x, y] : c.built.walls) o << "built-wall " << x << " " << y << "\n";
    for (const auto& [x, y] : c.built.mines) o << "built-mine " << x << " " << y << "\n";
    // The province battle's counter-attack (read when the battle starts).
    o << "counter-attack " << (c.counterAttack ? 1 : 0) << "\n";
    for (const AttackRow& a : c.pendingAttack)
        o << "attack " << a.unitId << " " << a.type << " " << a.pos.x << " " << a.pos.y << " " << a.turn << " "
          << (a.order ? 1 : 0) << "\n";
    if (!battleSection.empty()) {
        if (c.state != CampaignState::FirstCityAttack && c.state != CampaignState::Tactic &&
            c.state != CampaignState::CounterAttack)
            throw std::runtime_error("a battle can only be saved in a battle state");
        o << battleSection;
        if (battleSection.back() != '\n') o << "\n";
    }
    o << "end\n";
    return o.str();
}

void writeSaveFile(const std::filesystem::path& file, const std::string& text) {
    std::filesystem::path tmp = file;
    tmp += ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) throw std::runtime_error("cannot write " + tmp.string());
        f << text;
        f.flush();
        if (!f) {
            f.close();
            std::error_code ec;
            std::filesystem::remove(tmp, ec);
            throw std::runtime_error("cannot write " + tmp.string());
        }
    }
    std::error_code ec;
    std::filesystem::rename(tmp, file, ec); // replaces an existing file
    if (ec) {
        std::filesystem::remove(tmp, ec);
        throw std::runtime_error("cannot replace " + file.string());
    }
}

std::string saveDescription(const std::string& text) {
    std::istringstream in(text);
    std::string line;
    if (!std::getline(in, line) || line.rfind("OPENDAY-CAMPAIGN", 0) != 0) return {};
    while (std::getline(in, line))
        if (line.rfind("description ", 0) == 0) return line.substr(12);
    return {};
}

Campaign loadCampaign(const std::string& text, const CampaignResources& res, std::string* description) {
    std::istringstream in(text);
    std::string line;
    if (!std::getline(in, line) || line.rfind("OPENDAY-CAMPAIGN", 0) != 0) throw std::runtime_error("not an OpenDay save");
    {
        int version = 0;
        std::istringstream v(line.substr(16));
        v >> version;
        if (version < 1) throw std::runtime_error("not an OpenDay save");
        if (version > kCampaignSaveVersion) throw std::runtime_error("the save was made by a newer OpenDay");
    }
    Campaign c;
    std::string battleSection;
    c.rules = res.rules;
    std::string cityName;
    std::vector<CityBuilding> buildings;
    std::vector<CityRoad> roads;
    std::vector<CityWall> walls;
    std::vector<CityMine> mines;
    std::vector<HqSite> hq;
    int nextBuilding = 0, nextUnit = 0;
    bool ended = false;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto space = line.find(' ');
        const std::string key = line.substr(0, space);
        const std::string rest = space == std::string::npos ? std::string() : line.substr(space + 1);
        std::istringstream v(rest);
        if (key == "description") {
            if (description) *description = rest;
        } else if (key == "name") {
            c.playerName = rest;
        } else if (key == "difficulty") {
            v >> c.difficulty;
        } else if (key == "colony") {
            v >> c.colonyNumber;
        } else if (key == "state") {
            int s = 3;
            v >> s;
            c.state = static_cast<CampaignState>(s);
        } else if (key == "province") {
            v >> c.battleProvince;
        } else if (key == "credits") {
            v >> c.player.credits;
        } else if (key == "tech") {
            v >> c.player.tech;
        } else if (key == "stats") {
            v >> c.player.unitsDestroyed >> c.player.unitsLost >> c.player.buildingsDestroyed >> c.player.buildingsLost;
        } else if (key == "level") {
            int t = 0, l = 0;
            v >> t >> l;
            c.player.techLevels[t] = l;
        } else if (key == "prov") {
            int cap = 0, var = 1;
            v >> cap >> var;
            c.provinces.push_back({cap != 0, var});
        } else if (key == "city") {
            cityName = rest;
        } else if (key == "nextids") {
            v >> nextBuilding >> nextUnit;
        } else if (key == "building") {
            CityBuilding b;
            v >> b.id >> b.type >> b.owner >> b.armor >> b.pos.x >> b.pos.y;
            buildings.push_back(b);
        } else if (key == "road") {
            CityRoad r;
            v >> r.type >> r.owner >> r.pos.x >> r.pos.y;
            roads.push_back(r);
        } else if (key == "wall") {
            CityWall w;
            int broken = 0;
            v >> w.type >> w.owner >> w.armor >> w.pos.x >> w.pos.y >> broken;
            w.brokenEnd = broken != 0;
            walls.push_back(w);
        } else if (key == "mine") {
            CityMine m;
            int armed = 1;
            v >> m.type >> m.owner >> m.pos.x >> m.pos.y >> armed;
            m.armed = armed != 0;
            for (bool& h : m.hidden) {
                int x = 0;
                v >> x;
                h = x != 0;
            }
            mines.push_back(m);
        } else if (key == "hq") {
            HqSite h;
            v >> h.slot >> h.pos.x >> h.pos.y;
            hq.push_back(h);
        } else if (key == "built-building") {
            int id = 0;
            v >> id;
            c.built.buildings.insert(id);
        } else if (key == "built-road" || key == "built-wall" || key == "built-mine") {
            int x = 0, y = 0;
            v >> x >> y;
            (key == "built-road" ? c.built.roads : key == "built-wall" ? c.built.walls : c.built.mines).insert({x, y});
        } else if (key == "counter-attack") {
            int x = 0;
            v >> x;
            c.counterAttack = x != 0;
        } else if (key == "attack") {
            AttackRow a;
            int order = 0;
            v >> a.unitId >> a.type >> a.pos.x >> a.pos.y >> a.turn >> order;
            a.order = order != 0;
            c.pendingAttack.push_back(a);
        } else if (key == "battle") {
            // The battle section, kept whole for loadBattleSave.
            battleSection = line + "\n";
            bool closed = false;
            while (std::getline(in, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                battleSection += line + "\n";
                if (line == "battle-end") {
                    closed = true;
                    break;
                }
            }
            if (!closed) throw std::runtime_error("the save file is incomplete");
        } else if (key == "end") {
            ended = true;
        }
    }
    if (!ended || cityName.empty()) throw std::runtime_error("the save file is incomplete");
    if (!battleSection.empty()) {
        if (c.state != CampaignState::FirstCityAttack && c.state != CampaignState::Tactic &&
            c.state != CampaignState::CounterAttack)
            throw std::runtime_error("the save has a battle outside a battle state");
        c.savedBattle = std::make_shared<const SavedBattle>(loadBattleSave(battleSection, *res.rules));
    }
    c.reloadColony(res);
    if (c.provinces.size() != c.colony.provinces.size()) throw std::runtime_error("the save does not match the colony");
    const std::filesystem::path cityPath = res.scenario(cityName);
    if (cityPath.empty()) throw std::runtime_error("scenario " + cityName + " not found");
    c.city = loadCity(data::SqliteDb(cityPath), cityName, *res.rules);
    for (CityBuilding& b : buildings) {
        const auto t = res.rules->buildings.find(b.type);
        if (t == res.rules->buildings.end()) continue;
        b.w = t->second.sizeX;
        b.h = t->second.sizeY;
    }
    std::erase_if(buildings, [&](const CityBuilding& b) { return !res.rules->buildings.count(b.type); });
    c.city.buildings = std::move(buildings);
    c.city.roads = std::move(roads);
    c.city.walls = std::move(walls);
    c.city.mines = std::move(mines);
    c.city.hqSites = std::move(hq);
    c.city.nextBuildingId = std::max(c.city.nextBuildingId, nextBuilding);
    c.city.nextUnitId = std::max(c.city.nextUnitId, nextUnit);
    return c;
}

} // namespace openday::game
