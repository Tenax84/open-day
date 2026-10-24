// The client side of a tactical battle: keeps a copy of the battle state
// up to date from the server's events and turns the events into
// animations and scene changes.
#pragma once

#include <vector>

#include "anim/battle_animations.hpp"
#include "battle/messages.hpp"

namespace openday::game {

class BattleView {
public:
    // `viewer`: the local player, whose knowledge of mines is shown; -1 =
    // the human player whose turn it is or was last (hot seat). The
    // references must outlive this object.
    BattleView(BattleState initial, MapScene& scene, BattleAnimations& animations, int viewer = -1);

    // The client's copy of the state. It runs ahead of the animations: it
    // already holds the result of every event received.
    const BattleState& state() const { return state_; }

    void apply(const std::vector<Event>& events);

private:
    int viewer() const { return viewer_ >= 0 ? viewer_ : lastHuman_; }
    // Schedules a redraw of the units whose usable AP or draw mode changed.
    void syncUnits(bool immediately);
    std::vector<MapSprite> roadLayer() const;
    std::vector<MapSprite> wallLayer();
    std::vector<MapSprite> mineLayer() const;
    void redraw(Layer layer, std::vector<MapSprite> sprites);

    BattleState state_;
    MapScene& scene_;
    BattleAnimations& anim_;
    int viewer_;
    int lastHuman_ = 1;
    std::vector<int> wallShape_; // per wall: sprite number of the last redraw
    // Per unit: the usable AP and draw mode last given to the scene, and
    // whether it was placed in the deployment view (drawn darkened).
    std::vector<int> shownAp_, shownMode_;
    std::vector<bool> deployed_;
    int lastExploded_ = -1; // mine of the last explosion record
};

} // namespace openday::game
