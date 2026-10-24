// The encyclopedia's data and rules, independent of the screen: the five
// entry lists, the race and frame filters, first / next / previous / last,
// the unit page's levels and gauges and the prerequisite text
// (docs/mechanics/encyclopedia.md).
#pragma once

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "data/sqlite_db.hpp"

namespace openday::game {

// The page kinds; the categories (lamps) are Units, Automated, Buildings and
// Defenses = Wall + Mine.
enum class EncyclopediaPage { Unit, Automated, Building, Wall, Mine };
enum class EncyclopediaCategory { Units, Automated, Buildings, Defenses };

// One row of Unit Types with the columns the unit page shows.
struct EncyclopediaUnit {
    int typeId = 0, level = 0;
    int race = 0, frame = -1;
    std::string name, description, weapon, image;
    int category = 0; // 0 grav, 1 ground, 2 water
    int cost = 0, armor = 0, actionPoints = 0, shotAp = 0;
    int damageBase = 0, damageRandom = 0;
    bool isAA = false;
    int damageAA = 0;
    int range = 0, mineDetection = 0, movement = 0, jammer = 0, weaponType = 0;
    int prerequisite = 65535;
};

struct EncyclopediaAutomated {
    int id = 0, race = 0, frame = -1;
    std::string name, description, image;
    int cost = 0, armor = 0; // armor: Automated Units Infos property 0
    int prerequisite = 65535;
};

struct EncyclopediaBuilding {
    int id = 0, race = 0, frame = -1;
    std::string name, description, production, image;
    std::string code; // BUILDING TYPE ID LOOKUP name, e.g. H_Center_Deployment1
    int cost = 0, recycle = 0, armor = 0, sizeX = 1, sizeY = 1, position = 3;
    int prerequisite = 65535, transformIntoUnit = 65535;
};

struct EncyclopediaWall {
    int id = 0, frame = -1;
    std::string name, description, image;
    int cost = 0, armor = 0;
    int prerequisite = 65535;
};

struct EncyclopediaMine {
    int id = 0, frame = -1;
    std::string name, description, image;
    int cost = 0, damageBase = 0, damageRandom = 0;
    int prerequisite = 65535;
};

// The five type tables in load order (the physical row order, 4.1).
struct EncyclopediaData {
    std::vector<EncyclopediaUnit> units;
    std::vector<EncyclopediaAutomated> automated;
    std::vector<EncyclopediaBuilding> buildings;
    std::vector<EncyclopediaWall> walls;
    std::vector<EncyclopediaMine> mines;

    // `language`: English, German or French (the text column suffix).
    static EncyclopediaData load(const data::SqliteDb& global, const std::string& language = "English");

    const EncyclopediaUnit* unit(int typeId, int level) const;
    const EncyclopediaBuilding* building(int id) const;
    // The unit types, one entry per typeID in the order of the type's first
    // row (OpenDay's list, encyclopedia.md 4.3 and "Notes for OpenDay").
    std::vector<int> unitTypes() const;
    // The number of consecutive levels 0, 1, 2, ... of a type (at most 11).
    int levels(int typeId) const;
};

// Race filter test (4.1): 4 matches everything, 0 matches 0, non-zero
// matches non-zero.
bool encyclopediaRacePasses(int entryRace, int filter);

// The current entry and the navigation of the dialog (sections 2 to 4).
class EncyclopediaNavigator {
public:
    // `unitLevel`: the viewing player's tech level of a unit type (missing
    // types: 0); may be empty.
    EncyclopediaNavigator(const EncyclopediaData& data, std::function<int(int)> unitLevel);

    EncyclopediaPage page() const { return page_; }
    EncyclopediaCategory category() const;
    bool alien() const { return alien_; }
    // The current entry: a unit typeID, or an index into the automated,
    // building, wall or mine table; -1 = nothing passes.
    int entry() const { return entry_; }

    // The default page: units, humans, the first unit that passes.
    void openDefault();
    // Opening at a type (3); the entry is shown even if the filters would
    // skip it. Unknown types give the default page.
    void openUnit(int typeId);
    void openAutomated(int typeId);
    void openBuilding(int typeId);
    void openWall(int wallType);
    void openMine(int typeId);

    void next();
    void previous();
    // Race lamp: sets the filter, then steps to the next passing entry.
    void setAlien(bool alien);
    // Category lamp: the first passing entry of the category.
    void setCategory(EncyclopediaCategory c);

    // The row the unit page shows for a type: (type, viewer level), or the
    // nearest lower level that exists.
    const EncyclopediaUnit* displayedUnit(int typeId) const;
    int unitLevel(int typeId) const;

private:
    int size(EncyclopediaPage p) const;
    bool passes(EncyclopediaPage p, int index) const; // index into the page's list
    int keyOf(EncyclopediaPage p, int index) const;   // entry value of a list index
    int indexOf(EncyclopediaPage p, int key) const;
    bool first(EncyclopediaPage p);
    bool last(EncyclopediaPage p);

    const EncyclopediaData& data_;
    std::function<int(int)> level_;
    std::vector<int> unitTypes_;
    EncyclopediaPage page_ = EncyclopediaPage::Unit;
    bool alien_ = false;
    int entry_ = -1;
};

// The unit page's three rows (5.1).
struct UnitPageRows {
    const EncyclopediaUnit* cur = nullptr;
    const EncyclopediaUnit* max = nullptr;
    const EncyclopediaUnit* next = nullptr;
    int level = 0;  // L
    int levels = 1; // n
};
UnitPageRows unitPageRows(const EncyclopediaData& data, int typeId, int viewerLevel);

// Average damage, damageBase + damageRandom div 2.
int averageDamage(const EncyclopediaUnit& u);
// Rate of fire, actionPoints div actionPointsShoot (0 without a shot cost).
int rateOfFire(const EncyclopediaUnit& u);
// "%d - %d" of base and base + random.
std::string damageRange(int base, int random);

// The widths of a gauge's red bar (from the left) and dark red bar (from
// the right) inside its 74 px wide inner area, for maximum M, current A and
// next B (-1 = A = M, B = A; M = 0: full red if A > 0).
struct GaugeBars {
    int red = 0, dark = 0;
};
GaugeBars gaugeBars(int maximum, int current, int next, int width = 74);

// The ability icons of a unit row (5.1) as resource ids, in drawing order.
std::vector<int> abilityIcons(const EncyclopediaUnit& u);

// The PREREQUISITE line (6).
struct PrerequisiteText {
    enum class Kind { Hidden, None, Building, NotAvailable } kind = Kind::Hidden;
    std::string name; // Building: the building's name
};
// `context`: the colony's building types, or nullopt (no line drawn).
// `buildingPage`: the deployment-centre rule of the building page.
PrerequisiteText prerequisiteText(const EncyclopediaData& data, int prerequisite,
                                  const std::optional<std::vector<int>>& context, bool buildingPage);

} // namespace openday::game
