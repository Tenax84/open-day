#include "info/encyclopedia.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstdio>
#include <set>

namespace openday::game {
namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return s;
}

// The columns of a table, lower case (column names differ in case between
// the tables, data-model.md).
std::set<std::string> columnsOf(const data::SqliteDb& db, const std::string& table) {
    std::set<std::string> out;
    db.forEach("PRAGMA table_info(" + data::quoteIdent(table) + ")", [&](const data::Row& r) { out.insert(lower(r.text(1))); });
    return out;
}

// A text column in the wanted language: `base + language`, else the
// English one, else `base` itself (Building Types.Production is English),
// else an empty literal.
std::string textColumn(const std::set<std::string>& cols, const std::string& base, const std::string& language) {
    for (const std::string& c : {base + language, base + "English", base})
        if (cols.count(lower(c))) return data::quoteIdent(c);
    return "''";
}

std::string column(const std::set<std::string>& cols, const std::string& name, const char* fallback = "0") {
    return cols.count(lower(name)) ? data::quoteIdent(name) : std::string(fallback);
}

std::string str(const data::Row& r, int c) { return r.isNull(c) ? std::string() : r.text(c); }
int num(const data::Row& r, int c) { return r.isNull(c) ? 0 : static_cast<int>(r.integer(c)); }
int numOr(const data::Row& r, int c, int fallback) { return r.isNull(c) ? fallback : static_cast<int>(r.integer(c)); }

// The code name without its last character (the deployment-centre variants
// Mk1-Mk4 share "H_Center_Deployment").
std::string codeStem(const std::string& code) { return code.empty() ? code : code.substr(0, code.size() - 1); }

} // namespace

EncyclopediaData EncyclopediaData::load(const data::SqliteDb& db, const std::string& language) {
    EncyclopediaData d;
    {
        const auto c = columnsOf(db, "Unit Types");
        db.forEach("SELECT typeID, unitTechLevel, Race, " + column(c, "EncyclopediaFrame", "-1") + ", " + textColumn(c, "Name", language) +
                       ", " + textColumn(c, "Encyclopedia", language) + ", " + textColumn(c, "weaponName", language) +
                       ", facingsImageID, Category, Cost, Armor, actionPoints, actionPointsShoot, damageBase, damageRandom, IsAA, "
                       "DamageAA, weaponRange, MineDetectionRange, Movement, Jammer, weaponType, Prerequisite "
                       "FROM \"Unit Types\" ORDER BY Counter",
                   [&](const data::Row& r) {
                       EncyclopediaUnit u;
                       u.typeId = num(r, 0);
                       u.level = num(r, 1);
                       u.race = num(r, 2);
                       u.frame = numOr(r, 3, -1);
                       u.name = str(r, 4);
                       u.description = str(r, 5);
                       u.weapon = str(r, 6);
                       u.image = upper(str(r, 7));
                       u.category = num(r, 8);
                       u.cost = num(r, 9);
                       u.armor = num(r, 10);
                       u.actionPoints = num(r, 11);
                       u.shotAp = num(r, 12);
                       u.damageBase = num(r, 13);
                       u.damageRandom = num(r, 14);
                       u.isAA = num(r, 15) != 0;
                       u.damageAA = num(r, 16);
                       u.range = num(r, 17);
                       u.mineDetection = num(r, 18);
                       u.movement = num(r, 19);
                       u.jammer = num(r, 20);
                       u.weaponType = num(r, 21);
                       u.prerequisite = numOr(r, 22, 65535);
                       d.units.push_back(std::move(u));
                   });
    }
    if (db.hasTable("Automated Units")) {
        std::map<int, int> armor;
        if (db.hasTable("Automated Units Infos"))
            db.forEach("SELECT typeID, Value FROM \"Automated Units Infos\" WHERE Property = 0", [&](const data::Row& r) {
                armor[num(r, 0)] = std::atoi(str(r, 1).c_str());
            });
        const auto c = columnsOf(db, "Automated Units");
        db.forEach("SELECT typeID, Race, " + column(c, "EncyclopediaFrame", "-1") + ", " + textColumn(c, "Name", language) + ", " +
                       textColumn(c, "Encyclopedia", language) + ", facingsImageID, Cost, Prerequisite "
                       "FROM \"Automated Units\" ORDER BY Counter",
                   [&](const data::Row& r) {
                       EncyclopediaAutomated a;
                       a.id = num(r, 0);
                       a.race = num(r, 1);
                       a.frame = numOr(r, 2, -1);
                       a.name = str(r, 3);
                       a.description = str(r, 4);
                       a.image = upper(str(r, 5));
                       a.cost = num(r, 6);
                       a.prerequisite = numOr(r, 7, 65535);
                       const auto it = armor.find(a.id);
                       a.armor = it == armor.end() ? 0 : it->second;
                       d.automated.push_back(std::move(a));
                   });
    }
    {
        std::map<int, std::string> codes;
        if (db.hasTable("BUILDING TYPE ID LOOKUP"))
            db.forEach("SELECT Enum, BuildingTypeId FROM \"BUILDING TYPE ID LOOKUP\"",
                       [&](const data::Row& r) { codes[num(r, 0)] = str(r, 1); });
        const auto c = columnsOf(db, "Building Types");
        db.forEach("SELECT TypeID, Race, " + column(c, "EncyclopediaFrame", "-1") + ", " + textColumn(c, "Name", language) + ", " +
                       textColumn(c, "Encyclopedia", language) + ", " + textColumn(c, "Production", language) +
                       ", ImageID, Cost, Recycle, Armor, SizeX, SizeY, Position, Prerequisite, TransformIntoUnit "
                       "FROM \"Building Types\" ORDER BY Counter",
                   [&](const data::Row& r) {
                       EncyclopediaBuilding b;
                       b.id = num(r, 0);
                       b.race = num(r, 1);
                       b.frame = numOr(r, 2, -1);
                       b.name = str(r, 3);
                       b.description = str(r, 4);
                       b.production = str(r, 5);
                       b.image = upper(str(r, 6));
                       b.cost = num(r, 7);
                       b.recycle = num(r, 8);
                       b.armor = num(r, 9);
                       b.sizeX = num(r, 10);
                       b.sizeY = num(r, 11);
                       b.position = num(r, 12);
                       b.prerequisite = numOr(r, 13, 65535);
                       b.transformIntoUnit = numOr(r, 14, 65535);
                       const auto it = codes.find(b.id);
                       if (it != codes.end()) b.code = it->second;
                       d.buildings.push_back(std::move(b));
                   });
    }
    {
        const auto c = columnsOf(db, "Wall Types");
        db.forEach("SELECT WallType, " + column(c, "EncyclopediaFrame", "-1") + ", " + textColumn(c, "name", language) + ", " +
                       textColumn(c, "Encyclopedia", language) + ", img_id, Cost, Armor, prerequisite "
                       "FROM \"Wall Types\" ORDER BY counter",
                   [&](const data::Row& r) {
                       EncyclopediaWall w;
                       w.id = num(r, 0);
                       w.frame = numOr(r, 1, -1);
                       w.name = str(r, 2);
                       w.description = str(r, 3);
                       w.image = upper(str(r, 4));
                       w.cost = num(r, 5);
                       w.armor = num(r, 6);
                       w.prerequisite = numOr(r, 7, 65535);
                       d.walls.push_back(std::move(w));
                   });
    }
    {
        const auto c = columnsOf(db, "Mine Types");
        db.forEach("SELECT typeID, " + column(c, "EncyclopediaFrame", "-1") + ", " + textColumn(c, "name", language) + ", " +
                       textColumn(c, "Encyclopedia", language) + ", listboxImageID, Cost, damageBase, damageRandom, prerequisite "
                       "FROM \"Mine Types\" ORDER BY Counter",
                   [&](const data::Row& r) {
                       EncyclopediaMine m;
                       m.id = num(r, 0);
                       m.frame = numOr(r, 1, -1);
                       m.name = str(r, 2);
                       m.description = str(r, 3);
                       m.image = upper(str(r, 4));
                       m.cost = num(r, 5);
                       m.damageBase = num(r, 6);
                       m.damageRandom = num(r, 7);
                       m.prerequisite = numOr(r, 8, 65535);
                       d.mines.push_back(std::move(m));
                   });
    }
    return d;
}

const EncyclopediaUnit* EncyclopediaData::unit(int typeId, int level) const {
    for (const EncyclopediaUnit& u : units)
        if (u.typeId == typeId && u.level == level) return &u;
    return nullptr;
}

const EncyclopediaBuilding* EncyclopediaData::building(int id) const {
    for (const EncyclopediaBuilding& b : buildings)
        if (b.id == id) return &b;
    return nullptr;
}

std::vector<int> EncyclopediaData::unitTypes() const {
    std::vector<int> out;
    std::set<int> seen;
    for (const EncyclopediaUnit& u : units)
        if (seen.insert(u.typeId).second) out.push_back(u.typeId);
    return out;
}

int EncyclopediaData::levels(int typeId) const {
    int n = 0;
    while (n < 11 && unit(typeId, n)) ++n;
    return n;
}

bool encyclopediaRacePasses(int entryRace, int filter) {
    return entryRace == 4 || filter == 4 || (entryRace == 0 && filter == 0) || (entryRace != 0 && filter != 0);
}

// --- Navigation --------------------------------------------------------------

EncyclopediaNavigator::EncyclopediaNavigator(const EncyclopediaData& data, std::function<int(int)> unitLevel)
    : data_(data), level_(std::move(unitLevel)), unitTypes_(data.unitTypes()) {
    openDefault();
}

EncyclopediaCategory EncyclopediaNavigator::category() const {
    switch (page_) {
    case EncyclopediaPage::Unit: return EncyclopediaCategory::Units;
    case EncyclopediaPage::Automated: return EncyclopediaCategory::Automated;
    case EncyclopediaPage::Building: return EncyclopediaCategory::Buildings;
    case EncyclopediaPage::Wall:
    case EncyclopediaPage::Mine: return EncyclopediaCategory::Defenses;
    }
    return EncyclopediaCategory::Units;
}

int EncyclopediaNavigator::unitLevel(int typeId) const { return level_ ? std::max(0, level_(typeId)) : 0; }

const EncyclopediaUnit* EncyclopediaNavigator::displayedUnit(int typeId) const {
    for (int l = unitLevel(typeId); l >= 0; --l)
        if (const EncyclopediaUnit* u = data_.unit(typeId, l)) return u;
    // A type without level 0 (not in the shipped data): its first row.
    for (const EncyclopediaUnit& u : data_.units)
        if (u.typeId == typeId) return &u;
    return nullptr;
}

int EncyclopediaNavigator::size(EncyclopediaPage p) const {
    switch (p) {
    case EncyclopediaPage::Unit: return static_cast<int>(unitTypes_.size());
    case EncyclopediaPage::Automated: return static_cast<int>(data_.automated.size());
    case EncyclopediaPage::Building: return static_cast<int>(data_.buildings.size());
    case EncyclopediaPage::Wall: return static_cast<int>(data_.walls.size());
    case EncyclopediaPage::Mine: return static_cast<int>(data_.mines.size());
    }
    return 0;
}

bool EncyclopediaNavigator::passes(EncyclopediaPage p, int i) const {
    const int filter = alien_ ? 1 : 0;
    const auto at = static_cast<std::size_t>(i);
    switch (p) {
    case EncyclopediaPage::Unit: {
        const EncyclopediaUnit* u = displayedUnit(unitTypes_[at]);
        return u && u->frame != -1 && encyclopediaRacePasses(u->race, filter);
    }
    case EncyclopediaPage::Automated:
        return data_.automated[at].frame != -1 && encyclopediaRacePasses(data_.automated[at].race, filter);
    case EncyclopediaPage::Building: {
        const EncyclopediaBuilding& b = data_.buildings[at];
        return b.frame != -1 && b.transformIntoUnit == 65535 && encyclopediaRacePasses(b.race, filter);
    }
    case EncyclopediaPage::Wall: return data_.walls[at].frame != -1;
    case EncyclopediaPage::Mine: return data_.mines[at].frame != -1;
    }
    return false;
}

int EncyclopediaNavigator::keyOf(EncyclopediaPage p, int i) const {
    return p == EncyclopediaPage::Unit ? unitTypes_[static_cast<std::size_t>(i)] : i;
}

int EncyclopediaNavigator::indexOf(EncyclopediaPage p, int key) const {
    if (p != EncyclopediaPage::Unit) return key;
    const auto it = std::find(unitTypes_.begin(), unitTypes_.end(), key);
    return it == unitTypes_.end() ? -1 : static_cast<int>(it - unitTypes_.begin());
}

bool EncyclopediaNavigator::first(EncyclopediaPage p) {
    for (int i = 0; i < size(p); ++i)
        if (passes(p, i)) {
            page_ = p;
            entry_ = keyOf(p, i);
            return true;
        }
    return false;
}

bool EncyclopediaNavigator::last(EncyclopediaPage p) {
    for (int i = size(p) - 1; i >= 0; --i)
        if (passes(p, i)) {
            page_ = p;
            entry_ = keyOf(p, i);
            return true;
        }
    return false;
}

void EncyclopediaNavigator::next() {
    const int cur = entry_ < 0 ? -1 : indexOf(page_, entry_);
    for (int i = cur + 1; i < size(page_); ++i)
        if (passes(page_, i)) {
            entry_ = keyOf(page_, i);
            return;
        }
    // Off the end (4.2): walls continue with the mines and back.
    if (page_ == EncyclopediaPage::Wall) {
        if (!first(EncyclopediaPage::Mine)) first(EncyclopediaPage::Wall);
    } else if (page_ == EncyclopediaPage::Mine) {
        if (!first(EncyclopediaPage::Wall)) first(EncyclopediaPage::Mine);
    } else {
        first(page_);
    }
}

void EncyclopediaNavigator::previous() {
    const int cur = entry_ < 0 ? size(page_) : indexOf(page_, entry_);
    for (int i = cur - 1; i >= 0; --i)
        if (passes(page_, i)) {
            entry_ = keyOf(page_, i);
            return;
        }
    if (page_ == EncyclopediaPage::Wall) {
        if (!last(EncyclopediaPage::Mine)) last(EncyclopediaPage::Wall);
    } else if (page_ == EncyclopediaPage::Mine) {
        if (!last(EncyclopediaPage::Wall)) last(EncyclopediaPage::Mine);
    } else {
        last(page_);
    }
}

void EncyclopediaNavigator::setAlien(bool alien) {
    alien_ = alien;
    next();
}

void EncyclopediaNavigator::setCategory(EncyclopediaCategory c) {
    EncyclopediaPage p = EncyclopediaPage::Unit;
    switch (c) {
    case EncyclopediaCategory::Units: p = EncyclopediaPage::Unit; break;
    case EncyclopediaCategory::Automated: p = EncyclopediaPage::Automated; break;
    case EncyclopediaCategory::Buildings: p = EncyclopediaPage::Building; break;
    case EncyclopediaCategory::Defenses: p = EncyclopediaPage::Wall; break;
    }
    page_ = p;
    entry_ = -1;
    if (first(p)) return;
    if (p == EncyclopediaPage::Wall) first(EncyclopediaPage::Mine);
}

void EncyclopediaNavigator::openDefault() {
    alien_ = false;
    page_ = EncyclopediaPage::Unit;
    entry_ = -1;
    first(EncyclopediaPage::Unit);
}

void EncyclopediaNavigator::openUnit(int typeId) {
    if (!data_.unit(typeId, 0) || indexOf(EncyclopediaPage::Unit, typeId) < 0) return openDefault();
    alien_ = data_.unit(typeId, 0)->race != 0;
    page_ = EncyclopediaPage::Unit;
    entry_ = typeId;
}

void EncyclopediaNavigator::openAutomated(int typeId) {
    for (std::size_t i = 0; i < data_.automated.size(); ++i)
        if (data_.automated[i].id == typeId) {
            alien_ = data_.automated[i].race != 0;
            page_ = EncyclopediaPage::Automated;
            entry_ = static_cast<int>(i);
            return;
        }
    openDefault();
}

void EncyclopediaNavigator::openBuilding(int typeId) {
    for (std::size_t i = 0; i < data_.buildings.size(); ++i)
        if (data_.buildings[i].id == typeId) {
            alien_ = data_.buildings[i].race != 0;
            page_ = EncyclopediaPage::Building;
            entry_ = static_cast<int>(i);
            return;
        }
    openDefault();
}

void EncyclopediaNavigator::openWall(int wallType) {
    for (std::size_t i = 0; i < data_.walls.size(); ++i)
        if (data_.walls[i].id == wallType) {
            alien_ = false;
            page_ = EncyclopediaPage::Wall;
            entry_ = static_cast<int>(i);
            return;
        }
    openDefault();
}

void EncyclopediaNavigator::openMine(int typeId) {
    for (std::size_t i = 0; i < data_.mines.size(); ++i)
        if (data_.mines[i].id == typeId) {
            alien_ = false;
            page_ = EncyclopediaPage::Mine;
            entry_ = static_cast<int>(i);
            return;
        }
    openDefault();
}

// --- Page content ------------------------------------------------------------

UnitPageRows unitPageRows(const EncyclopediaData& data, int typeId, int viewerLevel) {
    UnitPageRows r;
    r.levels = std::max(1, data.levels(typeId));
    r.level = std::clamp(viewerLevel, 0, r.levels - 1);
    r.cur = data.unit(typeId, r.level);
    r.max = data.unit(typeId, r.levels - 1);
    r.next = r.level < r.levels - 1 ? data.unit(typeId, r.level + 1) : r.cur;
    if (!r.max) r.max = r.cur;
    if (!r.next) r.next = r.cur;
    return r;
}

int averageDamage(const EncyclopediaUnit& u) { return u.damageBase + u.damageRandom / 2; }

int rateOfFire(const EncyclopediaUnit& u) { return u.shotAp > 0 ? u.actionPoints / u.shotAp : 0; }

std::string damageRange(int base, int random) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%d - %d", base, base + random);
    return buf;
}

GaugeBars gaugeBars(int m, int a, int b, int width) {
    if (a == -1) a = m;
    if (b == -1) b = a;
    GaugeBars g;
    if (m == 0) {
        // OpenDay: no division (the original divides by zero here).
        g.red = a > 0 ? width : 0;
        return g;
    }
    const double w = width;
    g.red = std::clamp(static_cast<int>(static_cast<double>(a) / m * w), 0, width);
    g.dark = std::clamp(static_cast<int>(static_cast<double>(m - b) / m * w), 0, width);
    return g;
}

std::vector<int> abilityIcons(const EncyclopediaUnit& u) {
    std::vector<int> out;
    if (u.isAA) out.push_back(185);
    if (u.mineDetection > 0) out.push_back(184);
    if (u.movement == 2) out.push_back(182);
    if (u.jammer <= 0) out.push_back(181);
    if (u.weaponType == 1) out.push_back(183);
    if (u.jammer > 0 && u.jammer < 100) out.push_back(309);
    return out;
}

PrerequisiteText prerequisiteText(const EncyclopediaData& data, int p, const std::optional<std::vector<int>>& context,
                                  bool buildingPage) {
    PrerequisiteText t;
    if (!context) return t;
    if (p == 65535) {
        t.kind = PrerequisiteText::Kind::None;
        return t;
    }
    t.kind = PrerequisiteText::Kind::NotAvailable;
    const EncyclopediaBuilding* pb = data.building(p);
    if (buildingPage && pb && codeStem(pb->code) == "H_Center_Deployment") {
        // Any deployment centre of the colony satisfies and names it.
        for (const int id : *context)
            if (const EncyclopediaBuilding* b = data.building(id); b && codeStem(b->code) == "H_Center_Deployment") {
                t.kind = PrerequisiteText::Kind::Building;
                t.name = b->name;
                return t;
            }
        return t;
    }
    if (std::find(context->begin(), context->end(), p) != context->end()) {
        t.kind = PrerequisiteText::Kind::Building;
        t.name = pb ? pb->name : std::string();
    }
    return t;
}

} // namespace openday::game
