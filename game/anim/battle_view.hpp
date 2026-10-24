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
    // `viewer`: the player whose knowledge of mines is shown; -1 = whoever's
    // turn it is (hot seat). The references must outlive this object.
    BattleView(BattleState initial, MapScene& scene, BattleAnimations& animations, int viewer = -1);

    // The client's copy of the state. It runs ahead of the animations: it
    // already holds the result of every event received.
    const BattleState& state() const { return state_; }

    void apply(const std::vector<Event>& events);

private:
    int viewer() const { return viewer_ >= 0 ? viewer_ : state_.currentPlayer; }
    std::vector<MapSprite> roadLayer() const;
    std::vector<MapSprite> wallLayer();
    std::vector<MapSprite> mineLayer() const;
    void redraw(Layer layer, std::vector<MapSprite> sprites);

    BattleState state_;
    MapScene& scene_;
    BattleAnimations& anim_;
    int viewer_;
    std::vector<int> wallShape_; // per wall: sprite number of the last redraw
};

} // namespace openday::game
