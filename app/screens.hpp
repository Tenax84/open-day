// Factories of the game's screens.
#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "app/app.hpp"
#include "battle/battle_state.hpp"
#include "battle/combat.hpp"

namespace openday::app {

std::unique_ptr<Screen> makeResolutionMenu(App& app);
std::unique_ptr<Screen> makeSkirmishMenu(App& app);
// A WAITING.SMK loading picture (id 1-14) shown for one frame before `next`.
std::unique_ptr<Screen> makeWait(App& app, int id, std::function<std::unique_ptr<Screen>()> next);
// A full-screen video from VIDEO\ (skippable); `next` runs when it ends.
std::unique_ptr<Screen> makeVideo(App& app, const std::string& name, std::function<void()> next);

// The outcome of a battle, handed to BattleConfig::onFinished.
struct BattleResult {
    bool success = false;
    int loser = 5;
    int techFound = 0;
    int unitsDestroyed = 0, unitsLost = 0, buildingsDestroyed = 0, buildingsLost = 0; // the human side
    const game::BattleState* finalState = nullptr; // valid during the callback
};

// Everything a battle screen needs.
struct BattleConfig {
    ScenarioEntry entry;
    std::function<void(game::BattleState&)> prepare; // budgets, slots, unit lists
    game::CombatOptions options;
    std::string aftermath;  // report text on a win (campaign); empty = generic
    bool techLine = false;  // report shows "New technologies found"
    bool canSave = false;   // the in-game options allow saving
    std::function<void(const BattleResult&)> onFinished; // after the report; default: main menu
};

// Mission briefing, then deployment and the battle.
std::unique_ptr<Screen> makeBriefing(App& app, const ScenarioEntry& scenario);
// The battle screens of a skirmish scenario (deployment, turns, report).
std::unique_ptr<Screen> makeBattle(App& app, const ScenarioEntry& scenario);
std::unique_ptr<Screen> makeBattle(App& app, BattleConfig config);

// The army screen (army-tech.md 4). Variants: strategic (upgrades if
// `canUpgrade`), skirmish (CONTINUE), deployment (browsing only).
struct ArmyConfig {
    enum class Variant { Strategic, Skirmish, Deployment } variant = Variant::Strategic;
    std::vector<int> units;     // unit typeIDs in list order
    std::vector<int> automated; // automated type ids
    std::map<int, int>* levels = nullptr; // type -> level, changed by upgrades
    int* techPoints = nullptr;            // the pool, changed by upgrades
    bool canUpgrade = true;
    int resolution = 0; // 0 = 640x480, 1 = 1024x768
    std::function<void(int selectedType)> onClose;
};
std::unique_ptr<Screen> makeArmy(App& app, ArmyConfig config);

// Campaign entry points (app/campaign_flow.cpp).
std::unique_ptr<Screen> makeNewCampaignMenu(App& app);
std::unique_ptr<Screen> makeLoadMenu(App& app);

} // namespace openday::app
