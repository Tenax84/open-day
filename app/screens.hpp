// Factories of the game's screens.
#pragma once

#include <functional>
#include <memory>

#include "app/app.hpp"

namespace openday::app {

std::unique_ptr<Screen> makeResolutionMenu(App& app);
std::unique_ptr<Screen> makeSkirmishMenu(App& app);
// A WAITING.SMK loading picture (id 1-14) shown for one frame before `next`.
std::unique_ptr<Screen> makeWait(App& app, int id, std::function<std::unique_ptr<Screen>()> next);

// Mission briefing, then deployment and the battle.
std::unique_ptr<Screen> makeBriefing(App& app, const ScenarioEntry& scenario);
// The battle screens of a skirmish scenario (deployment, turns, report).
std::unique_ptr<Screen> makeBattle(App& app, const ScenarioEntry& scenario);

} // namespace openday::app
