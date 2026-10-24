#include "campaign/city.hpp"

#include <algorithm>
#include <cstdlib>

#include "battle/automated.hpp"

namespace openday::game {
namespace {

int toInt(const data::Row& row, int col) { return row.isNull(col) ? 0 : static_cast<int>(row.integer(col)); }

bool hasColumn(const data::SqliteDb& db, const std::string& table, const std::string& column) {
    bool has = false;
    db.forEach("SELECT name FROM pragma_table_info('" + table + "') WHERE name = '" + column + "' COLLATE NOCASE",
               [&](const data::Row&) { has = true; });
    return has;
}

int sign(int v) { return (v > 0) - (v < 0); }

// An inclusive tile rectangle.
struct Area {
    int x0, y0, x1, y1;
    bool contains(Point p) const { return p.x >= x0 && p.x <= x1 && p.y >= y0 && p.y <= y1; }
};

bool intersects(const Area& a, const Area& b) { return a.x0 <= b.x1 && b.x0 <= a.x1 && a.y0 <= b.y1 && b.y0 <= a.y1; }

Area footprint(const CityBuilding& b) { return {b.pos.x, b.pos.y, b.pos.x + b.w - 1, b.pos.y + b.h - 1}; }

TerrainClass classAt(const City& c, const GlobalRules& rules, Point p) {
    return c.inside(p) ? rules.classOf(c.terrain.at(p.x, p.y)) : TerrainClass::Unknown;
}

const BuildingType* buildingType(const GlobalRules& rules, int id) {
    const auto it = rules.buildings.find(id);
    return it == rules.buildings.end() ? nullptr : &it->second;
}

void sizeOf(const GlobalRules& rules, StructureKind kind, int type, int& w, int& h) {
    w = h = 1;
    if (kind == StructureKind::Building || kind == StructureKind::Tower)
        if (const BuildingType* t = buildingType(rules, type)) {
            w = t->sizeX;
            h = t->sizeY;
        }
}

std::string structureName(const GlobalRules& rules, StructureKind kind, int type) {
    switch (kind) {
    case StructureKind::Building:
    case StructureKind::Tower:
        if (const BuildingType* t = buildingType(rules, type)) return t->name;
        break;
    case StructureKind::Road:
        if (const auto it = rules.roads.find(type); it != rules.roads.end()) return it->second.name;
        break;
    case StructureKind::Wall:
        if (const auto it = rules.walls.find(type); it != rules.walls.end()) return it->second.name;
        break;
    case StructureKind::Mine:
        if (const auto it = rules.mines.find(type); it != rules.mines.end()) return it->second.name;
        break;
    }
    return {};
}

// Cost of one piece, or -1 for an unknown type.
int costOf(const GlobalRules& rules, StructureKind kind, int type) {
    switch (kind) {
    case StructureKind::Building:
    case StructureKind::Tower:
        if (const BuildingType* t = buildingType(rules, type)) return t->cost;
        break;
    case StructureKind::Road:
        if (const auto it = rules.roads.find(type); it != rules.roads.end()) return it->second.cost;
        break;
    case StructureKind::Wall:
        if (const auto it = rules.walls.find(type); it != rules.walls.end()) return it->second.cost;
        break;
    case StructureKind::Mine:
        if (const auto it = rules.mines.find(type); it != rules.mines.end()) return it->second.cost;
        break;
    }
    return -1;
}

int recycleOf(const GlobalRules& rules, StructureKind kind, int type) {
    switch (kind) {
    case StructureKind::Building:
    case StructureKind::Tower:
        if (const BuildingType* t = buildingType(rules, type)) return t->recycle;
        break;
    case StructureKind::Road:
        if (const auto it = rules.roads.find(type); it != rules.roads.end()) return it->second.recycle;
        break;
    case StructureKind::Wall:
        if (const auto it = rules.walls.find(type); it != rules.walls.end()) return it->second.recycle;
        break;
    case StructureKind::Mine:
        if (const auto it = rules.mines.find(type); it != rules.mines.end()) return it->second.recycle;
        break;
    }
    return 0;
}

std::pair<int, int> key(Point p) { return {p.x, p.y}; }

// Prerequisite rule of the building list: deployment centres stand in for
// each other (city-building.md 2.1).
bool prerequisiteMet(const City& city, int player, int prerequisite) {
    if (prerequisite == kNone) return true;
    if (isDeployCentre(prerequisite)) {
        for (const CityBuilding& b : city.buildings)
            if (b.owner == player && isDeployCentre(b.type)) return true;
        return false;
    }
    return city.owns(player, prerequisite);
}

} // namespace

// --- City -------------------------------------------------------------------

int City::buildingAt(Point p) const {
    for (std::size_t i = 0; i < buildings.size(); ++i)
        if (footprint(buildings[i]).contains(p)) return static_cast<int>(i);
    return -1;
}

int City::roadAt(Point p) const {
    for (std::size_t i = 0; i < roads.size(); ++i)
        if (roads[i].pos.x == p.x && roads[i].pos.y == p.y) return static_cast<int>(i);
    return -1;
}

int City::wallAt(Point p) const {
    for (std::size_t i = 0; i < walls.size(); ++i)
        if (walls[i].pos.x == p.x && walls[i].pos.y == p.y) return static_cast<int>(i);
    return -1;
}

int City::mineAt(Point p) const {
    for (std::size_t i = 0; i < mines.size(); ++i)
        if (mines[i].pos.x == p.x && mines[i].pos.y == p.y) return static_cast<int>(i);
    return -1;
}

int City::buildingById(int id) const {
    for (std::size_t i = 0; i < buildings.size(); ++i)
        if (buildings[i].id == id) return static_cast<int>(i);
    return -1;
}

int City::count(int player, int buildingType) const {
    return static_cast<int>(std::count_if(buildings.begin(), buildings.end(), [&](const CityBuilding& b) {
        return b.owner == player && b.type == buildingType;
    }));
}

bool City::isBridgeSite(Point p) const {
    return std::any_of(bridgeSites_.begin(), bridgeSites_.end(), [&](Point s) { return s.x == p.x && s.y == p.y; });
}

void City::computeBridgeSites(const GlobalRules& rules) {
    bridgeSites_ = findBridgeSites(size(), [&](Point p) { return classAt(*this, rules, p); });
}

void City::removeUnusedSlots(int humans) {
    std::erase_if(buildings, [&](const CityBuilding& b) { return b.type == kHeadquarters && b.owner > humans; });
    std::erase_if(hqSites, [&](const HqSite& h) { return h.slot > humans; });
}

ScenarioObjects City::objects() const {
    ScenarioObjects o;
    for (const CityRoad& r : roads) o.roads.push_back({r.type, r.pos.x, r.pos.y});
    for (const CityWall& w : walls) o.walls.push_back({w.type, w.pos.x, w.pos.y, w.armor, w.brokenEnd});
    for (const CityMine& m : mines) o.mines.push_back({m.type, m.pos.x, m.pos.y});
    for (const CityBuilding& b : buildings) o.buildings.push_back({b.type, b.owner, b.pos.x, b.pos.y, b.armor});
    return o;
}

City loadCity(const data::SqliteDb& db, const std::string& name, const GlobalRules& rules) {
    City c;
    c.scenario = name;
    c.terrain = loadTerrainMap(db);
    db.forEach("SELECT id, buildingType, playerOwner, currentAR, PosX, PosY FROM \"Buildings On Map\" ORDER BY Counter",
               [&](const data::Row& r) {
                   const BuildingType* t = buildingType(rules, toInt(r, 1));
                   if (!t) return;
                   c.buildings.push_back({toInt(r, 0), toInt(r, 1), toInt(r, 2), toInt(r, 3), {toInt(r, 4), toInt(r, 5)},
                                          t->sizeX, t->sizeY});
               });
    db.forEach("SELECT roadtype, playerOwner, PosX, PosY FROM \"Roads On Map\" ORDER BY Counter", [&](const data::Row& r) {
        if (rules.roads.count(toInt(r, 0))) c.roads.push_back({toInt(r, 0), toInt(r, 1), {toInt(r, 2), toInt(r, 3)}});
    });
    db.forEach("SELECT roadtype, playerOwner, currentAR, posX, posY, isDamaged FROM \"Walls On Map\" ORDER BY Counter",
               [&](const data::Row& r) {
                   if (rules.walls.count(toInt(r, 0)))
                       c.walls.push_back({toInt(r, 0), toInt(r, 1), toInt(r, 2), {toInt(r, 3), toInt(r, 4)}, r.integer(5) != 0});
               });
    const bool visibility = hasColumn(db, "Mines On Map", "hidden_AI");
    db.forEach(visibility ? "SELECT typeID, playerOwner, PosX, PosY, armed, hidden_AI, hidden_1, hidden_2, hidden_3, hidden_4 "
                            "FROM \"Mines On Map\" ORDER BY Counter"
                          : "SELECT typeID, playerOwner, PosX, PosY, armed FROM \"Mines On Map\" ORDER BY Counter",
               [&](const data::Row& r) {
                   CityMine m;
                   m.type = toInt(r, 0);
                   m.owner = toInt(r, 1);
                   m.pos = {toInt(r, 2), toInt(r, 3)};
                   m.armed = r.integer(4) != 0;
                   if (visibility)
                       for (int p = 0; p < kPlayerSlots; ++p) m.hidden[static_cast<std::size_t>(p)] = r.integer(5 + p) != 0;
                   if (rules.mines.count(m.type)) c.mines.push_back(m);
               });
    if (db.hasTable("HQ on Map"))
        db.forEach("SELECT playerOwner, PosX, PosY FROM \"HQ on Map\" ORDER BY Counter",
                   [&](const data::Row& r) { c.hqSites.push_back({toInt(r, 0), {toInt(r, 1), toInt(r, 2)}}); });
    if (db.hasTable("Counter Attack"))
        db.forEach("SELECT UnitID, type, posX, posY, turn, \"order\" FROM \"Counter Attack\" ORDER BY Counter",
                   [&](const data::Row& r) {
                       c.counterAttack.push_back(
                           {toInt(r, 0), toInt(r, 1), {toInt(r, 2), toInt(r, 3)}, toInt(r, 4), r.integer(5) != 0});
                   });
    if (db.hasTable("Scenario Info"))
        db.forEach("SELECT NextID_Units, NextID_Buildings FROM \"Scenario Info\"", [&](const data::Row& r) {
            c.nextUnitId = std::max(c.nextUnitId, toInt(r, 0));
            c.nextBuildingId = std::max(c.nextBuildingId, toInt(r, 1));
        });
    for (const CityBuilding& b : c.buildings) c.nextBuildingId = std::max(c.nextBuildingId, b.id + 1);
    c.computeBridgeSites(rules);
    return c;
}

bool isTower(const BuildingType& t) { return t.transformIntoUnit != kNone; }

bool isDeployCentre(int t) { return t == 131 || t == 173 || t == 174 || t == 175; }

// --- Placement check ------------------------------------------------------------

int roadState(TerrainClass c) {
    switch (c) {
    case TerrainClass::Plain: return 0;
    case TerrainClass::Riverside: return 1;
    case TerrainClass::DeepWater: return 2;
    default: return 3;
    }
}

bool areaOk(const City& city, const GlobalRules& rules, int player, StructureKind kind, int type, Point pos,
            const MovedObjects& moved) {
    auto otherBuilding = [&](Point p) {
        const int b = city.buildingAt(p);
        return b >= 0 && b != moved.building;
    };
    auto otherRoad = [&](Point p) {
        const int r = city.roadAt(p);
        return r >= 0 && r != moved.road;
    };
    auto otherWall = [&](Point p) {
        const int w = city.wallAt(p);
        return w >= 0 && w != moved.wall;
    };
    auto otherMine = [&](Point p) {
        const int m = city.mineAt(p);
        return m >= 0 && m != moved.mine;
    };
    switch (kind) {
    case StructureKind::Mine: {
        if (!city.inside(pos) || otherBuilding(pos) || otherWall(pos) || otherMine(pos)) return false;
        const TerrainClass c = classAt(city, rules, pos);
        bool terrainOk = c == TerrainClass::Plain;
        if (type != 0 && (c == TerrainClass::Mountains || c == TerrainClass::Forest)) terrainOk = true;
        return terrainOk || city.roadAt(pos) >= 0;
    }
    case StructureKind::Wall:
        return city.inside(pos) && !otherBuilding(pos) && !otherMine(pos) && !otherRoad(pos) && !otherWall(pos) &&
               classAt(city, rules, pos) == TerrainClass::Plain;
    case StructureKind::Building:
    case StructureKind::Tower: {
        const BuildingType* t = buildingType(rules, type);
        if (!t) return false;
        if (moved.building < 0 && t->uniqueOnMap && city.owns(player, type)) return false;
        for (int dy = 0; dy < t->sizeY; ++dy)
            for (int dx = 0; dx < t->sizeX; ++dx) {
                const Point p{pos.x + dx, pos.y + dy};
                if (!city.inside(p) || static_cast<int>(classAt(city, rules, p)) != t->position) return false;
                if (otherWall(p) || otherRoad(p) || otherMine(p) || otherBuilding(p)) return false;
            }
        return true;
    }
    case StructureKind::Road: {
        if (!city.inside(pos) || otherBuilding(pos) || otherWall(pos) || otherRoad(pos)) return false;
        const int state = roadState(classAt(city, rules, pos));
        if (state == 3) return false;
        if (state == 0) return true;
        if (!city.isBridgeSite(pos)) return false;
        if (state == 2) {
            static constexpr Point kOrtho[4] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
            return std::any_of(std::begin(kOrtho), std::end(kOrtho), [&](Point d) {
                const int r = city.roadAt({pos.x + d.x, pos.y + d.y});
                return r >= 0 && r != moved.road;
            });
        }
        return true;
    }
    }
    return false;
}

int checkPlacement(const City& city, const GlobalRules& rules, int player, StructureKind kind, int type, Point pos,
                   const MovedObjects& moved) {
    int w = 1, h = 1;
    sizeOf(rules, kind, type, w, h);
    const BuildingType* bt =
        kind == StructureKind::Building || kind == StructureKind::Tower ? buildingType(rules, type) : nullptr;
    // 1. Area and terrain.
    if (!areaOk(city, rules, player, kind, type, pos, moved)) return kBuildTerrain;
    // 2. Headquarters: only on an HQ site.
    if (bt && bt->id == kHeadquarters) {
        const bool site = std::any_of(city.hqSites.begin(), city.hqSites.end(),
                                      [&](const HqSite& s) { return s.pos.x == pos.x && s.pos.y == pos.y; });
        return site ? kBuildOk : kBuildHqSite;
    }
    const bool deepWater = bt && bt->position == static_cast<int>(TerrainClass::DeepWater);
    // 3. Near the player's other constructions.
    if (!deepWater) {
        const Area near{pos.x - 4, pos.y - 4, pos.x + w + 3, pos.y + h + 3};
        bool found = false;
        for (std::size_t i = 0; i < city.roads.size() && !found; ++i)
            found = static_cast<int>(i) != moved.road && city.roads[i].owner == player && near.contains(city.roads[i].pos);
        for (std::size_t i = 0; i < city.walls.size() && !found; ++i)
            found = static_cast<int>(i) != moved.wall && city.walls[i].owner == player && near.contains(city.walls[i].pos);
        for (std::size_t i = 0; i < city.buildings.size() && !found; ++i)
            found = static_cast<int>(i) != moved.building && city.buildings[i].owner == player &&
                    intersects(near, footprint(city.buildings[i]));
        if (!found) return kBuildTooFar;
    }
    // 4. Province border.
    const int n = city.size();
    if (pos.x < 13 || pos.x >= n - w - 13 || pos.y < 13 || pos.y >= n - h - 13) return kBuildBorder;
    const Area foot{pos.x, pos.y, pos.x + w - 1, pos.y + h - 1};
    auto ownRoadOnRing = [&](bool corners) {
        for (std::size_t i = 0; i < city.roads.size(); ++i) {
            const CityRoad& r = city.roads[i];
            if (static_cast<int>(i) == moved.road || r.owner != player) continue;
            const Point p = r.pos;
            const bool ring = p.x >= foot.x0 - 1 && p.x <= foot.x1 + 1 && p.y >= foot.y0 - 1 && p.y <= foot.y1 + 1 &&
                              !foot.contains(p);
            const bool corner = (p.x == foot.x0 - 1 || p.x == foot.x1 + 1) && (p.y == foot.y0 - 1 || p.y == foot.y1 + 1);
            if (ring && (corners || !corner)) return true;
        }
        return false;
    };
    if (kind != StructureKind::Road) {
        // 5.1 Around any HQ site.
        for (const HqSite& s : city.hqSites)
            if (intersects(foot, {s.pos.x - 1, s.pos.y - 1, s.pos.x + 4, s.pos.y + 4})) return kBuildAroundHq;
        if (kind == StructureKind::Wall || kind == StructureKind::Mine) return kBuildOk;
        // 5.3 Road connection.
        bool connected = deepWater || ownRoadOnRing(false);
        if (!connected && kind == StructureKind::Tower)
            for (const CityWall& wl : city.walls) {
                const Point p = wl.pos;
                const bool ring = p.x >= foot.x0 - 1 && p.x <= foot.x1 + 1 && p.y >= foot.y0 - 1 && p.y <= foot.y1 + 1 &&
                                  !foot.contains(p);
                const bool corner = (p.x == foot.x0 - 1 || p.x == foot.x1 + 1) && (p.y == foot.y0 - 1 || p.y == foot.y1 + 1);
                if (ring && !corner) connected = true;
            }
        if (!connected) return kBuildNoRoad;
        // 5.4 One free tile to every non-tower building.
        if (kind == StructureKind::Building) {
            const Area ring{foot.x0 - 1, foot.y0 - 1, foot.x1 + 1, foot.y1 + 1};
            for (std::size_t i = 0; i < city.buildings.size(); ++i) {
                if (static_cast<int>(i) == moved.building) continue;
                const BuildingType* t = buildingType(rules, city.buildings[i].type);
                if (t && isTower(*t)) continue;
                if (intersects(ring, footprint(city.buildings[i]))) return kBuildTooClose;
            }
        }
        // 5.5 Away from other players' buildings.
        const Area wide{foot.x0 - 6, foot.y0 - 6, foot.x1 + 6, foot.y1 + 6};
        for (std::size_t i = 0; i < city.buildings.size(); ++i)
            if (static_cast<int>(i) != moved.building && city.buildings[i].owner != player &&
                intersects(wide, footprint(city.buildings[i])))
                return kBuildOtherPlayer;
        return kBuildOk;
    }
    // 6. Roads: bridges need paved road; connection to the HQ ring or an own road.
    if (roadState(classAt(city, rules, pos)) != 0 && type != 1) return kBuildBridgeRoad;
    for (const HqSite& s : city.hqSites)
        if (Area{s.pos.x - 1, s.pos.y - 1, s.pos.x + 4, s.pos.y + 4}.contains(pos)) return kBuildOk;
    return ownRoadOnRing(false) ? kBuildOk : kBuildNoRoad;
}

// --- Lists ------------------------------------------------------------------

std::vector<BuildOption> buildingOptions(const City& city, const GlobalRules& rules, const std::vector<int>& available,
                                         int player) {
    std::vector<BuildOption> out;
    for (const int id : available) {
        const BuildingType* t = buildingType(rules, id);
        if (!t || t->race != 0 || !t->canBuild || isTower(*t)) continue;
        if (!prerequisiteMet(city, player, t->prerequisite)) continue;
        if (t->uniqueOnMap && city.owns(player, id)) continue;
        out.push_back({StructureKind::Building, id});
    }
    return out;
}

std::vector<BuildOption> defenseOptions(const City& city, const GlobalRules& rules, const std::vector<int>& available,
                                        int player) {
    std::vector<BuildOption> out;
    for (const auto& [id, r] : rules.roads)
        if (r.canBuild) out.push_back({StructureKind::Road, id});
    // Roads in table order: the GLOBAL table lists the paved road first.
    std::stable_sort(out.begin(), out.end(), [](const BuildOption& a, const BuildOption& b) { return a.type > b.type; });
    for (const int id : available) {
        const BuildingType* t = buildingType(rules, id);
        if (!t || t->race != 0 || !t->canBuild || !isTower(*t)) continue;
        if (!prerequisiteMet(city, player, t->prerequisite)) continue;
        if (t->uniqueOnMap && city.owns(player, id)) continue;
        out.push_back({StructureKind::Tower, id});
    }
    for (const auto& [id, w] : rules.walls)
        if (w.canBuild) out.push_back({StructureKind::Wall, id});
    for (const auto& [id, m] : rules.mines)
        if (m.canBuy) out.push_back({StructureKind::Mine, id});
    return out;
}

std::vector<int> deployableTypes(const City& city, const GlobalRules& rules, int player) {
    std::set<int> towers;
    for (const auto& [id, b] : rules.buildings)
        if (isTower(b)) towers.insert(b.transformIntoUnit);
    std::vector<int> out;
    for (const auto& [id, u] : rules.units) {
        const int typeId = id.first;
        if (towers.count(typeId) || u.isConvoy || u.prerequisite == kNone) continue;
        if (city.owns(player, u.prerequisite) && std::find(out.begin(), out.end(), typeId) == out.end())
            out.push_back(typeId);
    }
    return out;
}

std::vector<int> automatedTypes(const City& city, const GlobalRules& rules, int player) {
    std::vector<int> out;
    for (const auto& [id, a] : rules.automated)
        if (a.prerequisite != kNone && city.owns(player, a.prerequisite)) out.push_back(id);
    return out;
}

// --- Requests ---------------------------------------------------------------

BuildResult buildStructure(City& city, const GlobalRules& rules, int player, int& credits, BuiltThisPhase& built,
                           StructureKind kind, int type, Point pos) {
    BuildResult res;
    res.name = structureName(rules, kind, type);
    const int cost = costOf(rules, kind, type);
    if (cost < 0 || kind == StructureKind::Road || kind == StructureKind::Wall) {
        res.code = kBuildGeneric;
        return res;
    }
    // OpenDay also checks what the original's client list enforces.
    if (kind != StructureKind::Mine) {
        const BuildingType* t = buildingType(rules, type);
        if (!t->canBuild || !prerequisiteMet(city, player, t->prerequisite)) {
            res.code = kBuildGeneric;
            return res;
        }
    }
    res.code = checkPlacement(city, rules, player, kind, type, pos);
    if (res.code != kBuildOk) return res;
    if (kind != StructureKind::Mine && isDeployCentre(type)) {
        int owned = 0;
        for (const CityBuilding& b : city.buildings)
            if (b.owner == player && isDeployCentre(b.type)) ++owned;
        if (owned > 9) {
            res.code = kBuildDeployLimit;
            return res;
        }
    }
    if (credits < cost) {
        res.code = kBuildNoCredits;
        return res;
    }
    if (kind == StructureKind::Mine) {
        CityMine m;
        m.type = type;
        m.owner = player;
        m.pos = pos;
        m.hidden[0] = true; // visible to the human players, hidden from the computer
        city.mines.push_back(m);
        built.mines.insert(key(pos));
    } else {
        const BuildingType* t = buildingType(rules, type);
        const int id = city.nextBuildingId++;
        city.buildings.push_back({id, type, player, t->armor, pos, t->sizeX, t->sizeY});
        built.buildings.insert(id);
    }
    credits -= cost;
    res.credits = cost;
    res.pieces = 1;
    return res;
}

BuildResult buildSegment(City& city, const GlobalRules& rules, int player, int& credits, BuiltThisPhase& built,
                         StructureKind kind, int type, Point start, Point end) {
    BuildResult res;
    res.name = structureName(rules, kind, type);
    const int cost = costOf(rules, kind, type);
    if (cost < 0 || (kind != StructureKind::Road && kind != StructureKind::Wall)) {
        res.code = kBuildGeneric;
        return res;
    }
    const int dx = end.x - start.x, dy = end.y - start.y;
    const int n = std::max(std::abs(dx), std::abs(dy));
    for (int i = 0; i <= n; ++i) {
        const Point p{start.x + i * sign(dx), start.y + i * sign(dy)};
        const int code = checkPlacement(city, rules, player, kind, type, p);
        if (code != kBuildOk) {
            if (res.pieces == 0) res.code = code;
            break;
        }
        if (credits < cost) {
            if (res.pieces == 0) res.code = kBuildNoCredits;
            break;
        }
        if (kind == StructureKind::Road) {
            city.roads.push_back({type, player, p});
            built.roads.insert(key(p));
        } else {
            city.walls.push_back({type, player, rules.walls.at(type).armor, p, false});
            built.walls.insert(key(p));
        }
        credits -= cost;
        res.credits += cost;
        ++res.pieces;
    }
    return res;
}

BuildResult recycleAt(City& city, const GlobalRules& rules, int player, int& credits, BuiltThisPhase& built, Point pos) {
    BuildResult res;
    const int b = city.buildingAt(pos);
    const int w = city.wallAt(pos), r = city.roadAt(pos), m = city.mineAt(pos);
    const bool own = (b >= 0 && city.buildings[static_cast<std::size_t>(b)].owner == player) ||
                     (w >= 0 && city.walls[static_cast<std::size_t>(w)].owner == player) ||
                     (r >= 0 && city.roads[static_cast<std::size_t>(r)].owner == player) ||
                     (m >= 0 && city.mines[static_cast<std::size_t>(m)].owner == player);
    if (!own) {
        res.code = kBuildNotOwn;
        return res;
    }
    if (b >= 0 && city.buildings[static_cast<std::size_t>(b)].type == kHeadquarters) {
        res.code = kBuildRecycleHq;
        return res;
    }
    auto refund = [&](int amount, const std::string& name) {
        credits += amount;
        res.credits += amount;
        ++res.pieces;
        if (res.name.empty()) res.name = name;
    };
    // Mine, road and wall first (by index, highest first), then the building.
    if (m >= 0 && city.mines[static_cast<std::size_t>(m)].owner == player) {
        const CityMine mine = city.mines[static_cast<std::size_t>(m)];
        const bool fresh = built.mines.erase(key(mine.pos)) > 0;
        refund(fresh ? costOf(rules, StructureKind::Mine, mine.type) : recycleOf(rules, StructureKind::Mine, mine.type),
               structureName(rules, StructureKind::Mine, mine.type));
        city.mines.erase(city.mines.begin() + m);
    }
    if (r >= 0 && city.roads[static_cast<std::size_t>(r)].owner == player) {
        const CityRoad road = city.roads[static_cast<std::size_t>(r)];
        const bool fresh = built.roads.erase(key(road.pos)) > 0;
        refund(fresh ? costOf(rules, StructureKind::Road, road.type) : recycleOf(rules, StructureKind::Road, road.type),
               structureName(rules, StructureKind::Road, road.type));
        city.roads.erase(city.roads.begin() + r);
    }
    if (w >= 0 && city.walls[static_cast<std::size_t>(w)].owner == player) {
        const CityWall wall = city.walls[static_cast<std::size_t>(w)];
        const bool fresh = built.walls.erase(key(wall.pos)) > 0;
        refund(fresh ? costOf(rules, StructureKind::Wall, wall.type) : recycleOf(rules, StructureKind::Wall, wall.type),
               structureName(rules, StructureKind::Wall, wall.type));
        city.walls.erase(city.walls.begin() + w);
    }
    if (b >= 0 && city.buildings[static_cast<std::size_t>(b)].owner == player) {
        const CityBuilding bd = city.buildings[static_cast<std::size_t>(b)];
        const bool fresh = built.buildings.erase(bd.id) > 0;
        res.name.clear(); // the building names the message
        refund(fresh ? costOf(rules, StructureKind::Building, bd.type) : recycleOf(rules, StructureKind::Building, bd.type),
               structureName(rules, StructureKind::Building, bd.type));
        city.buildings.erase(city.buildings.begin() + b);
    }
    return res;
}

BuildResult moveAt(City& city, const GlobalRules& rules, int player, BuiltThisPhase& built, Point from, Point to) {
    BuildResult res;
    MovedObjects moved;
    const int b = city.buildingAt(from);
    if (b >= 0 && city.buildings[static_cast<std::size_t>(b)].owner == player) moved.building = b;
    if (const int w = city.wallAt(from); w >= 0 && city.walls[static_cast<std::size_t>(w)].owner == player) moved.wall = w;
    if (const int r = city.roadAt(from); r >= 0 && city.roads[static_cast<std::size_t>(r)].owner == player) moved.road = r;
    if (const int m = city.mineAt(from); m >= 0 && city.mines[static_cast<std::size_t>(m)].owner == player) moved.mine = m;
    if (moved.building < 0 && moved.wall < 0 && moved.road < 0 && moved.mine < 0) {
        res.code = kBuildNotOwn;
        return res;
    }
    if (moved.building >= 0 && city.buildings[static_cast<std::size_t>(moved.building)].type == kHeadquarters) {
        res.code = kBuildMoveHq;
        return res;
    }
    // Every picked object must fit at the new place, the others ignored.
    auto check = [&](StructureKind kind, int type) {
        const int code = checkPlacement(city, rules, player, kind, type, to, moved);
        if (code != kBuildOk && res.code == kBuildOk) res.code = code;
    };
    if (moved.building >= 0) {
        const CityBuilding& bd = city.buildings[static_cast<std::size_t>(moved.building)];
        const BuildingType* t = buildingType(rules, bd.type);
        check(t && isTower(*t) ? StructureKind::Tower : StructureKind::Building, bd.type);
        res.name = structureName(rules, StructureKind::Building, bd.type);
    }
    if (moved.wall >= 0) check(StructureKind::Wall, city.walls[static_cast<std::size_t>(moved.wall)].type);
    if (moved.road >= 0) check(StructureKind::Road, city.roads[static_cast<std::size_t>(moved.road)].type);
    if (moved.mine >= 0) check(StructureKind::Mine, city.mines[static_cast<std::size_t>(moved.mine)].type);
    if (res.code != kBuildOk) return res;
    // A moved road, wall or mine loses its full refund (city-building.md 7).
    if (moved.building >= 0) {
        city.buildings[static_cast<std::size_t>(moved.building)].pos = to;
        ++res.pieces;
    }
    if (moved.wall >= 0) {
        CityWall& w = city.walls[static_cast<std::size_t>(moved.wall)];
        built.walls.erase(key(w.pos));
        if (res.name.empty()) res.name = structureName(rules, StructureKind::Wall, w.type);
        w.pos = to;
        ++res.pieces;
    }
    if (moved.road >= 0) {
        CityRoad& r = city.roads[static_cast<std::size_t>(moved.road)];
        built.roads.erase(key(r.pos));
        if (res.name.empty()) res.name = structureName(rules, StructureKind::Road, r.type);
        r.pos = to;
        ++res.pieces;
    }
    if (moved.mine >= 0) {
        CityMine& m = city.mines[static_cast<std::size_t>(moved.mine)];
        built.mines.erase(key(m.pos));
        if (res.name.empty()) res.name = structureName(rules, StructureKind::Mine, m.type);
        m.pos = to;
        ++res.pieces;
    }
    return res;
}

} // namespace openday::game
