// The encyclopedia's lists, filters, navigation, unit page rows, gauges and
// prerequisite text, and the map screens' list rows, blink, click and tile
// grouping, on a synthetic database.

#include "info/encyclopedia.hpp"
#include "info/overview.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
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

std::filesystem::path makeGlobal() {
    const auto path = std::filesystem::temp_directory_path() / "openday_info_test.sqlite";
    std::filesystem::remove(path);
    data::SqliteDb db(path, data::SqliteDb::Mode::ReadWriteCreate);
    db.exec("CREATE TABLE \"Unit Types\" (Counter INTEGER, typeID INTEGER, unitTechLevel INTEGER, Race INTEGER, "
            "EncyclopediaFrame INTEGER, NameEnglish TEXT, NameGerman TEXT, EncyclopediaEnglish TEXT, weaponNameEnglish TEXT, "
            "facingsImageID TEXT, Category INTEGER, Cost INTEGER, Armor INTEGER, actionPoints INTEGER, actionPointsShoot INTEGER, "
            "damageBase INTEGER, damageRandom INTEGER, IsAA INTEGER, DamageAA INTEGER, weaponRange INTEGER, "
            "MineDetectionRange INTEGER, Movement INTEGER, Jammer INTEGER, weaponType INTEGER, Prerequisite INTEGER)");
    // Rows of one type are not contiguous (encyclopedia.md 4.3).
    db.exec("INSERT INTO \"Unit Types\" VALUES "
            "(1, 0, 0, 0, 1, 'Alpha', 'AlphaDe', 'Text', 'Gun', 'hcom', 1, 50, 30, 40, 20, 10, 5, 0, 0, 3, 0, 2, 0, 0, 65535),"
            "(2, 1, 0, 1, 2, 'Beast', '', '', '', 'tbea', 1, 60, 40, 40, 20, 10, 5, 0, 0, 3, 0, 1, 50, 0, 65535),"
            "(3, 0, 1, 0, 1, 'Alpha', '', '', '', 'hcom', 1, 50, 40, 50, 20, 12, 6, 0, 0, 4, 0, 2, 0, 0, 65535),"
            "(4, 2, 0, 0, -1, 'Hidden', '', '', '', 'hhid', 1, 0, 1, 1, 1, 1, 1, 0, 0, 1, 0, 1, 50, 0, 65535),"
            "(5, 3, 0, 0, 3, 'Gamma', '', '', '', 'hgam', 1, 70, 50, 30, 10, 20, 0, 1, 15, 6, 2, 1, 10, 1, 131),"
            "(6, 0, 2, 0, 1, 'Alpha', '', '', '', 'hcom', 1, 50, 50, 60, 20, 14, 8, 0, 0, 5, 0, 2, 0, 0, 65535),"
            "(7, 4, 0, 2, 4, 'Crawler', '', '', '', 'bcra', 1, 40, 20, 40, 20, 10, 5, 0, 0, 3, 0, 1, 50, 0, 65535)");
    db.exec("CREATE TABLE \"Automated Units\" (Counter INTEGER, typeID INTEGER, NameEnglish TEXT, Race INTEGER, Cost INTEGER, "
            "Prerequisite INTEGER, facingsImageID TEXT, EncyclopediaEnglish TEXT, EncyclopediaFrame INTEGER)");
    db.exec("INSERT INTO \"Automated Units\" VALUES (1, 0, 'Transport', 0, 100, 65535, 'htra', '', 10),"
            "(2, 1, 'Worm', 1, 100, 65535, 'twor', '', 11)");
    db.exec("CREATE TABLE \"Automated Units Infos\" (Counter INTEGER, typeID INTEGER, Property INTEGER, Value TEXT)");
    db.exec("INSERT INTO \"Automated Units Infos\" VALUES (1, 0, 0, '40'), (2, 0, 1, '99')");
    db.exec("CREATE TABLE \"BUILDING TYPE ID LOOKUP\" (Enum INTEGER, BuildingTypeId TEXT)");
    db.exec("INSERT INTO \"BUILDING TYPE ID LOOKUP\" VALUES (130, 'H_Center_HQ'), (131, 'H_Center_Deployment1'), "
            "(173, 'H_Center_Deployment2')");
    db.exec("CREATE TABLE \"Building Types\" (Counter INTEGER, TypeID INTEGER, NameEnglish TEXT, Race INTEGER, "
            "EncyclopediaEnglish TEXT, EncyclopediaFrame INTEGER, Production TEXT, ImageID TEXT, Cost INTEGER, Recycle INTEGER, "
            "Armor INTEGER, SizeX INTEGER, SizeY INTEGER, Position INTEGER, Prerequisite INTEGER, TransformIntoUnit INTEGER)");
    db.exec("INSERT INTO \"Building Types\" VALUES "
            "(1, 130, 'Headquarters', 0, '', 40, 'Deploy', 'hhq', 500, 250, 300, 4, 4, 3, 65535, 65535),"
            "(2, 131, 'Deploy Mk1', 0, '', 31, '', 'hdep', 300, 150, 200, 2, 2, 3, 130, 65535),"
            "(3, 173, 'Deploy Mk2', 0, '', -1, '', 'hdep', 300, 150, 200, 2, 2, 3, 131, 65535),"
            "(4, 100, 'Tower', 0, '', 5, '', 'htow', 100, 50, 100, 1, 1, 3, 65535, 20),"
            "(5, 500, 'Hive', 1, '', 6, '', 'thiv', 100, 50, 100, 2, 3, 3, 65535, 65535)");
    db.exec("CREATE TABLE \"Wall Types\" (counter INTEGER, nameEnglish TEXT, WallType INTEGER, prerequisite INTEGER, Cost INTEGER, "
            "Armor INTEGER, img_id TEXT, EncyclopediaEnglish TEXT, EncyclopediaFrame INTEGER)");
    db.exec("INSERT INTO \"Wall Types\" VALUES (1, 'Concrete', 0, 65535, 10, 50, 'wallc', '', 119),"
            "(2, 'Ruined', 3, 65535, 0, 0, 'wallr', '', -1)");
    db.exec("CREATE TABLE \"Mine Types\" (Counter INTEGER, nameEnglish TEXT, typeID INTEGER, prerequisite INTEGER, "
            "damageBase INTEGER, damageRandom INTEGER, Cost INTEGER, listboxImageID TEXT, EncyclopediaEnglish TEXT, "
            "EncyclopediaFrame INTEGER)");
    db.exec("INSERT INTO \"Mine Types\" VALUES (1, 'Mine', 0, 65535, 30, 10, 20, 'mine', '', 77),"
            "(2, 'Grav Mine', 1, 65535, 50, 20, 40, 'gmine', '', 78)");
    return path;
}

void testData(const EncyclopediaData& d) {
    CHECK(d.units.size() == 7 && d.automated.size() == 2 && d.buildings.size() == 5 && d.walls.size() == 2 && d.mines.size() == 2);
    CHECK(d.unitTypes() == (std::vector<int>{0, 1, 2, 3, 4}));
    CHECK(d.levels(0) == 3 && d.levels(3) == 1 && d.levels(9) == 0);
    CHECK(d.units[0].name == "Alpha" && d.units[0].weapon == "Gun" && d.units[0].image == "HCOM");
    CHECK(d.automated[0].armor == 40 && d.automated[1].armor == 0);
    CHECK(d.buildings[1].code == "H_Center_Deployment1" && d.buildings[0].production == "Deploy");
    CHECK(d.walls[1].frame == -1 && d.mines[1].name == "Grav Mine" && d.mines[1].image == "GMINE");
}

void testRaceFilter() {
    CHECK(encyclopediaRacePasses(0, 0) && !encyclopediaRacePasses(1, 0) && !encyclopediaRacePasses(0, 1));
    CHECK(encyclopediaRacePasses(3, 1) && encyclopediaRacePasses(4, 0) && encyclopediaRacePasses(0, 4));
}

void testNavigation(const EncyclopediaData& d) {
    EncyclopediaNavigator n(d, {});
    // Default: humans, units, the first passing unit.
    CHECK(n.page() == EncyclopediaPage::Unit && !n.alien() && n.entry() == 0);
    n.next(); // skips the alien Beast and the frame -1 type
    CHECK(n.entry() == 3);
    n.next(); // wraps to the first
    CHECK(n.entry() == 0);
    n.previous(); // wraps to the last
    CHECK(n.entry() == 3);
    n.setAlien(true); // the next passing entry after Gamma
    CHECK(n.alien() && n.entry() == 4);
    n.next(); // wraps to the first alien unit
    CHECK(n.entry() == 1);
    n.next();
    CHECK(n.entry() == 4);

    // Opening at a type ignores the filters; unknown types give the default.
    n.openUnit(2);
    CHECK(n.entry() == 2 && !n.alien());
    n.openUnit(4);
    CHECK(n.entry() == 4 && n.alien() && n.category() == EncyclopediaCategory::Units);
    n.openUnit(77);
    CHECK(n.entry() == 0 && !n.alien());

    // Buildings: towers and frame -1 are skipped.
    n.setCategory(EncyclopediaCategory::Buildings);
    CHECK(n.page() == EncyclopediaPage::Building && n.entry() == 0);
    n.next();
    CHECK(n.entry() == 1);
    n.next();
    CHECK(n.entry() == 0);
    n.openBuilding(500);
    CHECK(n.alien() && n.entry() == 4);

    // Defenses: walls, then the mines, and back.
    n.setCategory(EncyclopediaCategory::Defenses);
    CHECK(n.page() == EncyclopediaPage::Wall && n.entry() == 0);
    n.next();
    CHECK(n.page() == EncyclopediaPage::Mine && n.entry() == 0);
    n.next();
    CHECK(n.page() == EncyclopediaPage::Mine && n.entry() == 1);
    n.next();
    CHECK(n.page() == EncyclopediaPage::Wall && n.entry() == 0);
    n.previous();
    CHECK(n.page() == EncyclopediaPage::Mine && n.entry() == 1);

    // Automated units with the race filter.
    n.openAutomated(1);
    CHECK(n.alien() && n.entry() == 1);
    n.setAlien(false);
    CHECK(n.entry() == 0);

    // The viewer's level decides the displayed row.
    EncyclopediaNavigator leveled(d, [](int type) { return type == 0 ? 2 : 0; });
    CHECK(leveled.displayedUnit(0)->level == 2 && leveled.displayedUnit(3)->level == 0);
}

void testUnitPage(const EncyclopediaData& d) {
    UnitPageRows r = unitPageRows(d, 0, 1);
    CHECK(r.level == 1 && r.levels == 3 && r.cur->level == 1 && r.max->level == 2 && r.next->level == 2);
    r = unitPageRows(d, 0, 7); // clamped to the last level; next = cur
    CHECK(r.level == 2 && r.next == r.cur);
    r = unitPageRows(d, 3, 0);
    CHECK(r.levels == 1 && r.cur == r.max && r.next == r.cur);
    CHECK(averageDamage(*d.unit(0, 0)) == 12 && rateOfFire(*d.unit(0, 0)) == 2 && rateOfFire(*d.unit(3, 0)) == 3);
    CHECK(damageRange(10, 5) == "10 - 15");
    // Gamma: AA, detects mines, jammer 10, indirect fire.
    CHECK(abilityIcons(*d.unit(3, 0)) == (std::vector<int>{185, 184, 183, 309}));
    // Alpha: foot unit, stealth (Jammer 0).
    CHECK(abilityIcons(*d.unit(0, 0)) == (std::vector<int>{182, 181}));
    CHECK(abilityIcons(*d.unit(1, 0)) == (std::vector<int>{309}));
}

void testGauges() {
    GaugeBars g = gaugeBars(10, 5, 7);
    CHECK(g.red == 37 && g.dark == 22);
    g = gaugeBars(10, -1, -1);
    CHECK(g.red == 74 && g.dark == 0);
    g = gaugeBars(10, 5, -1); // B = A
    CHECK(g.red == 37 && g.dark == 37);
    g = gaugeBars(0, 3, 3);
    CHECK(g.red == 74 && g.dark == 0);
    g = gaugeBars(0, 0, 0);
    CHECK(g.red == 0 && g.dark == 0);
}

void testPrerequisite(const EncyclopediaData& d) {
    using K = PrerequisiteText::Kind;
    CHECK(prerequisiteText(d, 131, std::nullopt, false).kind == K::Hidden);
    CHECK(prerequisiteText(d, 65535, std::vector<int>{}, false).kind == K::None);
    const PrerequisiteText inList = prerequisiteText(d, 130, std::vector<int>{130}, false);
    CHECK(inList.kind == K::Building && inList.name == "Headquarters");
    CHECK(prerequisiteText(d, 130, std::vector<int>{131}, false).kind == K::NotAvailable);
    // The deployment-centre rule of the building page: any variant counts.
    const PrerequisiteText dep = prerequisiteText(d, 131, std::vector<int>{130, 173}, true);
    CHECK(dep.kind == K::Building && dep.name == "Deploy Mk2");
    CHECK(prerequisiteText(d, 131, std::vector<int>{130}, true).kind == K::NotAvailable);
    CHECK(prerequisiteText(d, 131, std::vector<int>{173}, false).kind == K::NotAvailable);
}

void testOverview() {
    const std::vector<OverviewRow> rows = overviewRows({5, 3, 5, 5, 1, 3});
    CHECK(rows.size() == 3 && rows[0].type == 5 && rows[0].count == 3 && rows[1].type == 3 && rows[1].count == 2 && rows[2].count == 1);
    CHECK(blinkOffset(0) == -255 && blinkOffset(1) == 0 && blinkOffset(2) == 255 && blinkOffset(3) == 0);
    const Rgb black = blinkColor({10, 200, 30}, 0), white = blinkColor({10, 200, 30}, 2), same = blinkColor({10, 200, 30}, 3);
    CHECK(black.r == 0 && black.g == 0 && white.r == 255 && white.b == 255 && same.g == 200);
    // 396 px for a 72-tile map: s = 5.5.
    const auto t = overviewTile(11, 395, 396, 72);
    CHECK(t && t->x == 2 && t->y == 71);
    CHECK(!overviewTile(396, 0, 396, 72) && !overviewTile(-1, 0, 396, 72));
    // An L shape: a 3x2 block and a single tile below it.
    const std::vector<TileRect> rects = groupTiles({{4, 4}, {5, 4}, {6, 4}, {4, 5}, {5, 5}, {6, 5}, {4, 6}, {10, 10}});
    CHECK(rects.size() == 3);
    CHECK(rects[0].x == 4 && rects[0].y == 4 && rects[0].w == 3 && rects[0].h == 2);
    CHECK(rects[0].centre().x == 5 && rects[0].centre().y == 4);
    CHECK(rects[1].x == 4 && rects[1].y == 6 && rects[1].w == 1 && rects[2].x == 10);
}

// With the converted databases of a local installation: the human unit list
// of the shipped data (encyclopedia.md 4.3) and the defense cycle.
void realData(const std::filesystem::path& dataDir) {
    std::filesystem::path global;
    for (const auto& e : std::filesystem::recursive_directory_iterator(dataDir))
        if (e.path().filename() == "GLOBAL.sqlite") global = e.path();
    CHECK(!global.empty());
    if (global.empty()) return;
    const EncyclopediaData d = EncyclopediaData::load(data::SqliteDb(global));
    EncyclopediaNavigator n(d, {});
    std::vector<std::string> names;
    for (int i = 0; i < 40; ++i) {
        const std::string name = n.displayedUnit(n.entry())->name;
        if (!names.empty() && name == names.front()) break;
        names.push_back(name);
        n.next();
    }
    CHECK(names.size() == 23 && names.front() == "Commando" && names[1] == "Trooper" && names.back() == "Elite Trooper");
    std::printf("human units:");
    for (const std::string& name : names) std::printf(" %s;", name.c_str());
    std::printf("\n");
    n.setCategory(EncyclopediaCategory::Defenses);
    std::vector<std::string> defenses;
    for (int i = 0; i < 5; ++i) {
        defenses.push_back(n.page() == EncyclopediaPage::Wall ? d.walls[static_cast<std::size_t>(n.entry())].name
                                                              : d.mines[static_cast<std::size_t>(n.entry())].name);
        n.next();
    }
    CHECK(n.page() == EncyclopediaPage::Wall && n.entry() == 0 && defenses.size() == 5);
}

} // namespace

int main(int argc, char** argv) {
    for (int i = 1; i + 1 < argc; ++i)
        if (std::string(argv[i]) == "--data") realData(argv[i + 1]);
    const auto path = makeGlobal();
    {
        const data::SqliteDb db(path);
        const EncyclopediaData d = EncyclopediaData::load(db);
        testData(d);
        testRaceFilter();
        testNavigation(d);
        testUnitPage(d);
        testGauges();
        testPrerequisite(d);
        const EncyclopediaData de = EncyclopediaData::load(db, "German");
        CHECK(de.units[0].name == "AlphaDe" && de.units[0].weapon == "Gun"); // missing German column: English
    }
    testOverview();
    std::filesystem::remove(path);
    if (failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    std::puts("all info tests passed");
    return EXIT_SUCCESS;
}
