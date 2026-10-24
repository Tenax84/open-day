// The scenario objective: decides when a battle is over and who won.
// docs/mechanics/battle-flow.md section 2.
#pragma once

#include "battle/battle_state.hpp"

namespace openday::game {

struct ObjectiveResult {
    bool decided = false;
    bool success = false; // the human side won
    int loser = 5;        // death match: the slot that lost; 5 = none
};

// Evaluates the objective for the current turn counter. Updates the Hold
// Position bookkeeping in `state`, so it must run only on the server, at
// the points listed in battle-flow.md 2.1.
ObjectiveResult checkObjective(BattleState& state);

// The objective's progress counter sent with each turn (tactical-interface.md
// 2.5): the Hold Position counter while it is current, otherwise 0.
int objectiveProgress(const BattleState& state);

// Short English name of an objective kind ("Destroy", "Hold Position", ...).
const char* objectiveName(int kind);

} // namespace openday::game
