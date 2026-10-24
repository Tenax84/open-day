#include "battle/battle_state.hpp"

#include <algorithm>
#include <set>
#include <tuple>

#include "battle/automated.hpp"

namespace openday::game {
namespace {

int toInt(const data::Row& row, int col) { return static_cast<int>(row.integer(col)); }

bool hasColumn(const data::SqliteDb& db, const std::string& table, const std::string& column) {
    bool has = false;
    db.forEach("SELECT name FROM pragma_table_info('" + table + "') WHERE name = '" + column + "'",
               [&](const data::Row&) { has = true; });
    return has;
}

} // namespace

BattleState::BattleState(const GlobalRules& rules, TerrainMap terrain) : rules_(&rules), terrain_(std::move(terrain)) {
    const auto n = static_cast<std::size_t>(terrain_.size * terrain_.size);
    unitGrid_.assign(n, -1);
    buildingGrid_.assign(n, -1);
    wallGrid_.assign(n, -1);
    roadGrid_.assign(n, -1);
    mineGrid_.assign(n, -1);
    ruinFlags_.assign(n, 0);
}

TerrainClass BattleState::classAt(Point p) const {
    return inside(p) ? rules_->classOf(terrain_.at(p.x, p.y)) : TerrainClass::Unknown;
}

const TerrainType* BattleState::terrainAt(Point p) const {
    return inside(p) ? rules_->terrainType(terrain_.at(p.x, p.y)) : nullptr;
}

void BattleState::setTerrain(Point p, const std::string& name) {
    if (inside(p)) terrain_.tiles[index(p)] = name;
}

void BattleState::mark(std::vector<int>& grid, Point p, int value) {
    if (inside(p)) grid[index(p)] = value;
}

int BattleState::addUnit(BattleUnit u) {
    const int i = static_cast<int>(units_.size());
    units_.push_back(u);
    for (int dy = 0; dy < u.type->sizeY; ++dy)
        for (int dx = 0; dx < u.type->sizeX; ++dx) mark(unitGrid_, {u.pos.x + dx, u.pos.y + dy}, i);
    return i;
}

int BattleState::addBuilding(BattleBuilding b) {
    const int i = static_cast<int>(buildings_.size());
    buildings_.push_back(b);
    for (int dy = 0; dy < b.type->sizeY; ++dy)
        for (int dx = 0; dx < b.type->sizeX; ++dx) mark(buildingGrid_, {b.pos.x + dx, b.pos.y + dy}, i);
    return i;
}

int BattleState::addWall(BattleWall w) {
    const int i = static_cast<int>(walls_.size());
    walls_.push_back(w);
    mark(wallGrid_, w.pos, i);
    return i;
}

int BattleState::addRoad(BattleRoad r) {
    const int i = static_cast<int>(roads_.size());
    roads_.push_back(r);
    mark(roadGrid_, r.pos, i);
    return i;
}

int BattleState::addMine(BattleMine m) {
    const int i = static_cast<int>(mines_.size());
    mines_.push_back(m);
    mark(mineGrid_, m.pos, i);
    return i;
}

void BattleState::addRuin(BattleRuin r) {
    if (!inside(r.pos)) return;
    std::uint8_t flags = 1;
    const auto it = rules_->ruins.find(r.name);
    if (it != rules_->ruins.end()) {
        if (!it->second.canMoveThrough) flags |= 2;
        if (!it->second.canFireThrough) flags |= 4;
    }
    ruinFlags_[index(r.pos)] |= flags;
    ruins_.push_back(std::move(r));
}

void BattleState::moveUnit(int i, Point to) {
    BattleUnit& u = unit(i);
    if (unitAt(u.pos) == i) mark(unitGrid_, u.pos, -1);
    u.pos = to;
    mark(unitGrid_, to, i);
}

void BattleState::removeUnit(int i) {
    BattleUnit& u = unit(i);
    u.alive = false;
    for (int dy = 0; dy < u.type->sizeY; ++dy)
        for (int dx = 0; dx < u.type->sizeX; ++dx) {
            const Point p{u.pos.x + dx, u.pos.y + dy};
            if (unitAt(p) == i) mark(unitGrid_, p, -1);
        }
}

void BattleState::removeBuilding(int i) {
    BattleBuilding& b = building(i);
    b.alive = false;
    for (int dy = 0; dy < b.type->sizeY; ++dy)
        for (int dx = 0; dx < b.type->sizeX; ++dx) {
            const Point p{b.pos.x + dx, b.pos.y + dy};
            if (buildingAt(p) == i) mark(buildingGrid_, p, -1);
        }
}

void BattleState::removeWall(int i) {
    walls_[static_cast<std::size_t>(i)].alive = false;
    if (wallAt(walls_[static_cast<std::size_t>(i)].pos) == i) mark(wallGrid_, walls_[static_cast<std::size_t>(i)].pos, -1);
}

void BattleState::removeRoad(int i) {
    roads_[static_cast<std::size_t>(i)].alive = false;
    if (roadAt(roads_[static_cast<std::size_t>(i)].pos) == i) mark(roadGrid_, roads_[static_cast<std::size_t>(i)].pos, -1);
}

void BattleState::removeMine(int i) {
    mines_[static_cast<std::size_t>(i)].alive = false;
    if (mineAt(mines_[static_cast<std::size_t>(i)].pos) == i) mark(mineGrid_, mines_[static_cast<std::size_t>(i)].pos, -1);
}

const UnitType* BattleState::unitType(int typeId, int techLevel) const {
    if (techLevel >= 0) {
        const auto it = rules_->units.find({typeId, techLevel});
        return it == rules_->units.end() ? nullptr : &it->second;
    }
    const auto it = rules_->units.lower_bound({typeId, -1000});
    return it != rules_->units.end() && it->first.first == typeId ? &it->second : nullptr;
}

bool BattleState::inDeployZone(int player, Point p) const {
    if (player < 0 || player >= kPlayerSlots) return false;
    const auto& zone = deployZone[static_cast<std::size_t>(player)];
    return std::any_of(zone.begin(), zone.end(), [&](Point z) { return z.x == p.x && z.y == p.y; });
}

int BattleState::techLevel(int player, int typeId) const {
    if (player < 0 || player >= kPlayerSlots) return 0;
    const auto& m = techLevels[static_cast<std::size_t>(player)];
    const auto it = m.find(typeId);
    return it == m.end() ? 0 : it->second;
}

std::vector<int> BattleState::buyable(int player) const {
    std::vector<int> out;
    for (const int id : purchasable) {
        if (deployNeedsBuildings) {
            const UnitType* t = unitType(id, -1);
            if (!t) continue;
            const bool owned = std::any_of(buildings_.begin(), buildings_.end(), [&](const BattleBuilding& b) {
                return b.alive && b.owner == player && b.type->id == t->prerequisite;
            });
            if (!owned) continue;
        }
        out.push_back(id);
    }
    return out;
}

std::vector<int> BattleState::usableAutomated(int player) const {
    std::vector<int> out;
    if (player < 0 || player >= kPlayerSlots) return out;
    const auto p = static_cast<std::size_t>(player);
    for (const int id : automated[p]) {
        const auto it = rules_->automated.find(id);
        if (it == rules_->automated.end()) continue;
        if (std::find(automatedUsed[p].begin(), automatedUsed[p].end(), id) != automatedUsed[p].end()) continue;
        if (automatedNeedsBuildings[p]) {
            const int prereq = it->second.prerequisite;
            const bool owned = std::any_of(buildings_.begin(), buildings_.end(), [&](const BattleBuilding& b) {
                return b.alive && b.owner == player && b.type->id == prereq;
            });
            if (!owned) continue;
        }
        if (std::find(out.begin(), out.end(), id) == out.end()) out.push_back(id);
    }
    return out;
}

bool BattleState::isBridgeSite(Point p) const {
    return std::any_of(bridgeSites.begin(), bridgeSites.end(), [&](Point s) { return s.x == p.x && s.y == p.y; });
}

int BattleState::techFound() const {
    int sum = 0;
    for (const int b : techHeld)
        if (b >= 0 && b < static_cast<int>(buildings_.size()) && buildings_[static_cast<std::size_t>(b)].alive)
            sum += buildings_[static_cast<std::size_t>(b)].type->techReward;
    return sum;
}

int BattleStats::total(const std::map<int, int>& counter) {
    int sum = 0;
    for (const auto& [type, n] : counter) sum += n;
    return sum;
}

namespace {

bool humanSlot(int p) { return p >= 1 && p < kPlayerSlots; }

} // namespace

void countUnitDestroyed(BattleState& state, int typeId, int owner, int attacker) {
    // Death match credits no friendly kills; the other modes have no
    // same-side check (battle-flow.md 5.3).
    if (humanSlot(owner)) ++state.stats[static_cast<std::size_t>(owner)].unitsLost[typeId];
    if (humanSlot(attacker) && !(state.deathMatch() && attacker == owner))
        ++state.stats[static_cast<std::size_t>(attacker)].unitsDestroyed[typeId];
}

void countBuildingDestroyed(BattleState& state, int typeId, int owner, int attacker) {
    if (humanSlot(owner)) ++state.stats[static_cast<std::size_t>(owner)].buildingsLost[typeId];
    if (humanSlot(attacker)) ++state.stats[static_cast<std::size_t>(attacker)].buildingsDestroyed[typeId];
}

int battleScore(const BattleState& state, int player) {
    if (!humanSlot(player)) return 0;
    const BattleStats& st = state.stats[static_cast<std::size_t>(player)];
    const GlobalRules& rules = state.rules();
    auto count = [](const std::map<int, int>& m, int type) {
        const auto it = m.find(type);
        return it == m.end() ? 0 : it->second;
    };
    long long score = 0;
    for (const auto& [id, b] : rules.buildings)
        score += static_cast<long long>(count(st.buildingsDestroyed, id) - count(st.buildingsLost, id)) * b.cost;
    // One term per unit type, at the Cost of its lowest tech level (level 0).
    int last = -1;
    for (const auto& [key, u] : rules.units) {
        if (key.first == last) continue;
        last = key.first;
        score += (2LL * count(st.unitsDestroyed, key.first) - count(st.unitsLost, key.first)) * u.cost;
    }
    return static_cast<int>(std::clamp<long long>(score, 0, 65535));
}

int originalBattleScore(const BattleState& state, int player) {
    if (!humanSlot(player)) return 0;
    const BattleStats& st = state.stats[static_cast<std::size_t>(player)];
    long long score = 0;
    for (const auto& [key, u] : state.rules().units)
        if (const auto it = st.unitsDestroyed.find(key.first); it != st.unitsDestroyed.end())
            score += static_cast<long long>(it->second) * u.cost;
    return static_cast<int>(score % 65536);
}

std::vector<PlayerResult> battleResults(const BattleState& state) {
    std::vector<PlayerResult> out;
    for (const int p : state.players) {
        if (!humanSlot(p)) continue;
        const BattleStats& st = state.stats[static_cast<std::size_t>(p)];
        PlayerResult r;
        r.player = p;
        r.unitsDestroyed = BattleStats::total(st.unitsDestroyed);
        r.buildingsDestroyed = BattleStats::total(st.buildingsDestroyed);
        r.unitsLost = BattleStats::total(st.unitsLost);
        r.buildingsLost = BattleStats::total(st.buildingsLost);
        r.score = battleScore(state, p);
        out.push_back(r);
    }
    return out;
}

int BattleState::roadTypeAt(Point p) const {
    const int r = roadAt(p);
    return r < 0 ? -1 : roads_[static_cast<std::size_t>(r)].type;
}

BattleState loadBattle(const data::SqliteDb& scenario, const GlobalRules& rules, const TerrainMap& terrain) {
    BattleState s(rules, terrain);
    // The filters below are the ones loadMapObjects applies.
    scenario.forEach("SELECT roadtype, playerOwner, PosX, PosY FROM \"Roads On Map\"", [&](const data::Row& r) {
        const BattleRoad road{toInt(r, 0), toInt(r, 1), {toInt(r, 2), toInt(r, 3)}};
        if (rules.roads.count(road.type) && s.inside(road.pos)) s.addRoad(road);
    });
    scenario.forEach("SELECT roadtype, playerOwner, currentAR, posX, posY, isDamaged FROM \"Walls On Map\"",
                     [&](const data::Row& r) {
                         const BattleWall w{toInt(r, 0), toInt(r, 1), toInt(r, 2), {toInt(r, 3), toInt(r, 4)},
                                            r.integer(5) != 0};
                         if (rules.walls.count(w.type) && s.inside(w.pos)) s.addWall(w);
                     });
    const bool mineVisibility = hasColumn(scenario, "Mines On Map", "hidden_AI");
    scenario.forEach(mineVisibility ? "SELECT typeID, playerOwner, PosX, PosY, armed, hidden_AI, hidden_1, hidden_2, "
                                      "hidden_3, hidden_4 FROM \"Mines On Map\""
                                    : "SELECT typeID, playerOwner, PosX, PosY, armed FROM \"Mines On Map\"",
                     [&](const data::Row& r) {
                         BattleMine m;
                         m.type = toInt(r, 0);
                         m.owner = toInt(r, 1);
                         m.pos = {toInt(r, 2), toInt(r, 3)};
                         m.armed = r.integer(4) != 0;
                         if (mineVisibility)
                             for (int p = 0; p < kPlayerSlots; ++p) m.hidden[static_cast<std::size_t>(p)] = r.integer(5 + p) != 0;
                         if (rules.mines.count(m.type) && s.inside(m.pos)) s.addMine(m);
                     });
    scenario.forEach("SELECT id, PosX, PosY FROM \"Ruins On Map\"",
                     [&](const data::Row& r) { s.addRuin({toUpper(r.text(0)), {toInt(r, 1), toInt(r, 2)}}); });
    scenario.forEach("SELECT id, buildingType, playerOwner, currentAR, PosX, PosY FROM \"Buildings On Map\"",
                     [&](const data::Row& r) {
                         const auto type = rules.buildings.find(toInt(r, 1));
                         if (type == rules.buildings.end()) return;
                         s.addBuilding({toInt(r, 0), &type->second, toInt(r, 2), toInt(r, 3), {toInt(r, 4), toInt(r, 5)}});
                     });
    const bool orders = hasColumn(scenario, "Units On Map", "OrderID");
    scenario.forEach(std::string("SELECT ID, unitType, unitTL, playerOwner, facing, currentAR, currentAP, negAP, PosX, PosY") +
                         (orders ? ", OrderID, Turn, Probability, EnemyUnitType" : "") + " FROM \"Units On Map\"",
                     [&](const data::Row& r) {
                         const auto type = rules.units.find({toInt(r, 1), toInt(r, 2)});
                         const Point p{toInt(r, 8), toInt(r, 9)};
                         if (type == rules.units.end() || !s.inside(p)) return;
                         BattleUnit u;
                         u.id = toInt(r, 0);
                         u.type = &type->second;
                         u.owner = toInt(r, 3);
                         u.ap = toInt(r, 6);
                         u.negAp = toInt(r, 7);
                         u.armor = toInt(r, 5);
                         u.pos = p;
                         u.facing = toInt(r, 4) & 7;
                         if (orders)
                             u.order = {r.isNull(10) ? 0 : toInt(r, 10), r.isNull(11) ? 0 : toInt(r, 11),
                                        r.isNull(12) ? 100 : toInt(r, 12), r.isNull(13) ? 65535 : toInt(r, 13)};
                         s.addUnit(u);
                     });
    // Order parameters, by unit id.
    auto unitById = [&](int id) -> BattleUnit* {
        for (std::size_t i = 0; i < s.units().size(); ++i)
            if (s.units()[i].id == id) return &s.unit(static_cast<int>(i));
        return nullptr;
    };
    if (scenario.hasTable("Unit Order Pos List"))
        scenario.forEach("SELECT \"Unit ID\", PosX, PosY FROM \"Unit Order Pos List\" ORDER BY Counter",
                         [&](const data::Row& r) {
                             if (BattleUnit* u = unitById(toInt(r, 0))) u->order.positions.push_back({toInt(r, 1), toInt(r, 2)});
                         });
    if (scenario.hasTable("Unit Order ID List"))
        scenario.forEach("SELECT \"Unit ID\", Id FROM \"Unit Order ID List\" ORDER BY Counter", [&](const data::Row& r) {
            if (BattleUnit* u = unitById(toInt(r, 0))) u->order.ids.push_back(toInt(r, 1));
        });
    if (scenario.hasTable("Objective")) {
        scenario.forEach("SELECT ObjectifId, TurnNumber FROM \"Objective\"", [&](const data::Row& r) {
            s.objective.kind = toInt(r, 0);
            s.objective.turns = r.isNull(1) ? 0 : toInt(r, 1);
        });
        if (scenario.hasTable("Objectives ID List"))
            scenario.forEach("SELECT Id FROM \"Objectives ID List\"",
                             [&](const data::Row& r) { s.objective.ids.push_back(toInt(r, 0)); });
        if (scenario.hasTable("Objectives Pos List"))
            scenario.forEach("SELECT PosX, PosY FROM \"Objectives Pos List\"",
                             [&](const data::Row& r) { s.objective.zone.push_back({toInt(r, 0), toInt(r, 1)}); });
    }
    // Deployment zones: a tile listed for several players belongs to the
    // lowest slot only.
    if (scenario.hasTable("Deployment")) {
        std::set<std::pair<int, int>> taken;
        std::vector<std::tuple<int, int, int>> rows;
        scenario.forEach("SELECT player, PosX, PosY FROM \"Deployment\"", [&](const data::Row& r) {
            rows.emplace_back(toInt(r, 0), toInt(r, 1), toInt(r, 2));
        });
        std::sort(rows.begin(), rows.end());
        for (const auto& [player, x, y] : rows)
            if (player >= 1 && player < kPlayerSlots && s.inside({x, y}) && taken.insert({x, y}).second)
                s.deployZone[static_cast<std::size_t>(player)].push_back({x, y});
    }
    if (scenario.hasTable("One Off Units Available"))
        scenario.forEach("SELECT Enum FROM \"One Off Units Available\" WHERE Available <> 0 ORDER BY Counter",
                         [&](const data::Row& r) { s.purchasable.push_back(toInt(r, 0)); });
    // Death match (objective kinds 8-10) has no computer player.
    s.computerPlayer = s.objective.kind < 8 || s.objective.kind > 10;
    if (scenario.hasTable("Reinforcement"))
        scenario.forEach("SELECT UnitID, type, posX, posY, turn, \"order\" FROM \"Reinforcement\"",
                         [&](const data::Row& r) {
                             s.reinforcements.push_back({toInt(r, 0), toInt(r, 1), {toInt(r, 2), toInt(r, 3)}, toInt(r, 4),
                                                         r.integer(5) != 0});
                         });
    if (scenario.hasTable("Scenario Info"))
        scenario.forEach("SELECT turn_qty FROM \"Scenario Info\"",
                         [&](const data::Row& r) { s.turnLimit = r.isNull(0) ? 0 : toInt(r, 0); });
    if (hasColumn(scenario, "Scenario Info", "One Off Deployment"))
        scenario.forEach("SELECT \"One Off Deployment\" FROM \"Scenario Info\"",
                         [&](const data::Row& r) { s.oneOffDeployment = r.isNull(0) ? 0 : toInt(r, 0); });

    // Automated units (automated-units.md 2.3): the scenario's list, or in
    // death match the prerequisite buildings on the map. Skirmish uses the
    // scenario's list *(?)*; campaign battles replace it (prepare).
    std::vector<int> oneOff;
    if (scenario.hasTable("One Off Automated Available"))
        scenario.forEach("SELECT Enum FROM \"One Off Automated Available\" WHERE Available <> 0 ORDER BY Counter",
                         [&](const data::Row& r) { oneOff.push_back(toInt(r, 0)); });
    std::vector<int> all;
    for (const auto& [id, a] : rules.automated) all.push_back(id);
    for (int p = 1; p < kPlayerSlots; ++p) {
        const bool byBuildings = s.objective.kind == 8 || (s.objective.kind == 9 && p == 1);
        s.automated[static_cast<std::size_t>(p)] = byBuildings ? all : oneOff;
        s.automatedNeedsBuildings[static_cast<std::size_t>(p)] = byBuildings;
    }
    if (scenario.hasTable("Automated Units"))
        scenario.forEach("SELECT AutomatedType, StartX, StartY, DestX, DestY, TurnArrival FROM \"Automated Units\" "
                         "ORDER BY Counter",
                         [&](const data::Row& r) {
                             s.scheduledAutomated.push_back(
                                 {toInt(r, 0), {toInt(r, 1), toInt(r, 2)}, {toInt(r, 3), toInt(r, 4)}, toInt(r, 5)});
                         });
    s.bridgeSites = findBridgeSites(s.size(), [&](Point p) { return s.classAt(p); });
    return s;
}

void refillAp(BattleState& state, int player) {
    for (std::size_t i = 0; i < state.units().size(); ++i) {
        BattleUnit& u = state.unit(static_cast<int>(i));
        if (!u.alive || u.owner != player) continue;
        u.ap = u.type->actionPoints;
        if (u.negAp > 0) {
            u.ap = std::max(0, u.type->actionPoints - u.negAp);
            u.negAp = 0;
        }
    }
}

void startBattle(BattleState& state) {
    std::set<int> slots{0};
    for (const BattleUnit& u : state.units())
        if (u.owner >= 0 && u.owner < kPlayerSlots) slots.insert(u.owner);
    for (const BattleBuilding& b : state.buildings())
        if (b.owner >= 0 && b.owner < kPlayerSlots) slots.insert(b.owner);
    state.players.assign(slots.begin(), slots.end());
    state.currentPlayer = 0;
    state.turn = 0;
    state.over = false;
    for (const int p : state.players) refillAp(state, p);
}

} // namespace openday::game
