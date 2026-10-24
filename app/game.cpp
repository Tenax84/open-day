// Starting a battle from the front end.
#include <memory>

#include "app/screens.hpp"
#include "data/sqlite_db.hpp"

namespace openday::app {
namespace {

// The skirmish army phase (army-tech.md 2.2): every player gets
// One Off Deployment x the ratio of its Tech Ratio bracket, and every unit
// type offered by the scenario starts at level 0.
struct SkirmishArmy {
    std::map<int, int> levels;
    int tech = 0;
    int income = 0; // ReinfRes (battle-flow.md 4.2; share 100 % for one player)
    std::vector<int> units;
};

int techRatio(const game::GlobalRules& rules, int v) {
    for (const game::TechRatio& r : rules.techRatios)
        if ((r.min == -1 || r.min <= v) && (r.max == -1 || v <= r.max)) return r.ratio;
    return 1;
}

} // namespace

std::unique_ptr<Screen> makeBriefing(App& app, const ScenarioEntry& scenario) {
    auto army = std::make_shared<SkirmishArmy>();
    army->tech = scenario.budget * techRatio(app.data().rules, scenario.budget);
    try {
        const data::SqliteDb db(scenario.database);
        db.forEach("SELECT ReinfRes FROM \"Scenario Info\"", [&](const data::Row& r) {
            army->income = r.isNull(0) ? 0 : static_cast<int>(r.integer(0));
        });
        if (db.hasTable("One Off Units Available"))
            db.forEach("SELECT Enum FROM \"One Off Units Available\" WHERE Available <> 0 ORDER BY Counter",
                       [&](const data::Row& r) {
                           const int id = static_cast<int>(r.integer(0));
                           const auto it = app.data().rules.units.find({id, 0});
                           if (it != app.data().rules.units.end() && it->second.race == 0) {
                               army->units.push_back(id);
                               army->levels[id] = 0;
                           }
                       });
    } catch (const data::DbError&) {
    }
    // The original has no separate briefing screen: the army screen, then the
    // loading picture ("loading a skirmish", WAITING.SMK id 4) leads to the
    // deployment view, where GOAL shows the objectives (tactical-interface.md 4.1, 8).
    ArmyConfig a;
    a.variant = ArmyConfig::Variant::Skirmish;
    a.units = army->units;
    a.levels = &army->levels;
    a.techPoints = &army->tech;
    a.canUpgrade = true;
    a.resolution = app.settings().resolution == 1 ? 1 : 0;
    a.onClose = [&app, scenario, army](int) {
        app.push(makeWait(app, 4, [&app, scenario, army] {
            BattleConfig config;
            config.entry = scenario;
            config.options.aiDamage = false; // only campaign battles scale AI damage (campaign.md 4.1)
            const std::map<int, int> levels = army->levels;
            const int income = army->income;
            config.prepare = [scenario, levels, income](game::BattleState& s) {
                // Single player: the other human slots join slot 1 *(OpenDay)*.
                for (std::size_t i = 0; i < s.units().size(); ++i) {
                    game::BattleUnit& u = s.unit(static_cast<int>(i));
                    if (u.owner >= 2) u.owner = 1;
                }
                for (std::size_t i = 0; i < s.buildings().size(); ++i) {
                    game::BattleBuilding& b = s.building(static_cast<int>(i));
                    if (b.owner >= 2) b.owner = 1;
                }
                s.players = {0, 1};
                s.deployBudget[1] = scenario.budget;
                s.income[1] = income;
                s.techLevels[1] = levels;
                s.deploying = !s.deployZone[1].empty();
            };
            return makeBattle(app, std::move(config));
        }));
    };
    return makeArmy(app, std::move(a));
}

} // namespace openday::app
