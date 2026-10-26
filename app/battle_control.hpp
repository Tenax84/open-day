// What the debug server (app/mcp_server.hpp, docs/debug-mcp.md) sees of and
// does with a running battle. The battle screen implements it; the orders go
// the same way as the interface's clicks.
#pragma once

#include <string>
#include <vector>

#include "anim/battle_session.hpp"
#include "ui/widgets.hpp"

namespace openday::app {

class BattleControl {
public:
    virtual ~BattleControl() = default;

    virtual const game::BattleSession& session() const = 0;
    virtual int localPlayer() const = 0;
    // "deploy" (also buying reinforcements), "tactic" (your turn, ready for
    // orders), "wait" (another player's turn or animations playing),
    // "automated" (the automated units view).
    virtual std::string modeName() const = 0;
    virtual std::string dialogName() const = 0;  // "none", "goal", "menu"
    virtual std::string messageText() const = 0; // the message strip's text while shown
    virtual std::vector<int> selected() const = 0;

    // The map view on the screen, and tile <-> screen pixel conversion (the
    // centre of the tile's ground diamond; it may lie outside the view).
    virtual ui::Rect mapView() const = 0;
    virtual game::Point tileAt(int x, int y) const = 0;
    virtual game::Point screenPos(game::Point tile) const = 0;
    virtual void centerOn(game::Point tile) = 0;

    // Orders. Each returns the refusal, or an empty string. Open dialogs
    // (goal, options) are closed first.
    virtual std::vector<int> deployTypes() = 0; // unit types for sale (deployment or reinforcement)
    virtual std::string deployUnit(int typeId, game::Point tile) = 0;
    virtual std::string undeployUnit(int unit) = 0;
    virtual std::string endDeployment() = 0;
    virtual std::string moveUnits(const std::vector<int>& units, game::Point target) = 0;
    virtual std::string shootAt(const std::vector<int>& units, game::Point target) = 0;
    virtual std::string finishTurn() = 0;
    virtual std::string forceResult(bool win) = 0;

    // Every event the battle client received, described (describeEvent).
    virtual const std::vector<std::string>& eventLog() const = 0;
};

} // namespace openday::app
