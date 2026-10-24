// A battle in progress in OpenDay's own text save format: the complete
// server state (objects, turn, resources, objective, reinforcements,
// statistics, automated units, random-number state) and what the screens
// need to show it again. docs/mechanics/savegame.md ("Notes for OpenDay").
//
// The section is a block of lines between "battle <version>" and
// "battle-end", embedded in a campaign save (campaign.hpp). Objects that no
// longer exist (destroyed units, buildings, walls, roads, mines) are not
// written; the others keep their relative order, so the restored indices
// are the old ones with the gaps closed.
#pragma once

#include <memory>
#include <string>

#include "battle/combat.hpp"
#include "battle/server.hpp"

namespace openday::game {

inline constexpr int kBattleSaveVersion = 1;

// What the screens and the campaign need besides the state.
struct BattleHeader {
    std::string scenario; // scenario database name ("s1-2-1"), for the overview picture
    std::string title, province, description, aftermath;
    bool city = false;     // a battle on the player's city
    int provinceNumber = 0;
    bool techLine = false; // the report shows "New technologies found"
    CombatOptions options;
};

// A battle read back from a save.
struct SavedBattle {
    BattleHeader header;
    std::shared_ptr<BattleState> state; // not started again: resume it (BattleServer::resume)
    std::string rng;                    // the server's random-number state; empty = none stored
    int aiBudget = 0;                   // the computer player's remaining automated-unit budget
};

// Writes the battle section of a running server. Throws std::runtime_error
// when the state cannot be saved (deployment in progress, battle over).
std::string saveBattle(const BattleServer& server, const BattleHeader& header);
std::string saveBattle(const BattleState& state, const BattleHeader& header, const std::string& rng, int aiBudget);

// Reads a battle section (the lines from "battle <version>" to
// "battle-end"). Throws std::runtime_error on anything unexpected: unknown
// version, unknown unit or building types, objects off the map, a
// truncated section.
SavedBattle loadBattleSave(const std::string& section, const GlobalRules& rules);

// A server for a saved battle: the state, the random numbers and the
// computer player's budget restored. Call resume() instead of start().
std::unique_ptr<BattleServer> restoreServer(const SavedBattle& saved);

// Escapes a text for one line of a save file (\\, \n, \r) and back.
std::string escapeSaveText(const std::string& s);
std::string unescapeSaveText(const std::string& s);

} // namespace openday::game
