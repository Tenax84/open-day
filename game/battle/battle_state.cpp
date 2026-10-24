#include "battle/battle_state.hpp"

#include <algorithm>
#include <set>

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
