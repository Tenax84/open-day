// Starting a battle from the front end.
#include "app/screens.hpp"

namespace openday::app {

std::unique_ptr<Screen> makeBriefing(App& app, const ScenarioEntry& scenario) {
    // The original has no separate briefing screen: the loading picture
    // ("loading a skirmish", WAITING.SMK id 4) leads to the deployment view,
    // where GOAL shows the objectives (tactical-interface.md 4.1, 8).
    return makeWait(app, 4, [&app, scenario] { return makeBattle(app, scenario); });
}

} // namespace openday::app
