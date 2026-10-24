// A tactical battle in one process: the server with the authoritative
// state, and one client (scene, animations, the client's copy of the
// state). Used by the game and by the map viewer.
#pragma once

#include <array>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "anim/battle_animations.hpp"
#include "anim/battle_view.hpp"
#include "battle/combat.hpp"
#include "battle/server.hpp"
#include "data/sqlite_db.hpp"
#include "render/animation.hpp"

namespace openday::game {

class BattleSession {
public:
    // Loads a scenario database. The battle is not started yet (deployment
    // may come first); call begin().
    // `prepare` may change the state before the server takes it (deployment
    // budget, slot assignments).
    static std::unique_ptr<BattleSession> load(const std::filesystem::path& scenario, const GlobalRules& rules,
                                               render::AnimationCache* clips, std::uint32_t seed, CombatOptions options,
                                               const std::function<void(BattleState&)>& prepare = {});

    BattleSession(std::string name, TerrainMap terrain, std::vector<MapSprite> objects, MapEntities entities,
                  BattleState state, const GlobalRules& rules, render::AnimationCache* clips, std::uint32_t seed,
                  CombatOptions options);

    // Starts the first turn.
    void begin();
    // Sends a request and plays the answer. Returns the refusal, if any.
    std::string send(const Request& request);

    const std::string& name() const { return name_; }
    const BattleState& state() const { return view_.state(); } // the client's copy
    const BattleServer& server() const { return server_; }
    MapScene& scene() { return scene_; }
    const MapScene& scene() const { return scene_; }
    BattleAnimations& animations() { return animations_; }
    const BattleAnimations& animations() const { return animations_; }

    std::function<void(const Event&)> log; // optional, sees every event

    // The player whose turn the animations are showing (they lag behind
    // the state), and the battle statistics per player slot.
    int displayedPlayer() const { return displayedPlayer_; }
    bool displayedOver() const { return displayedOver_; }
    struct Stats {
        int unitsLost = 0, buildingsLost = 0;
    };
    const std::array<Stats, kPlayerSlots>& stats() const { return stats_; }

private:
    void apply(const std::vector<Event>& events);

    std::string name_;
    MapEntities entities_;
    MapScene scene_;
    BattleAnimations animations_;
    BattleServer server_;
    BattleView view_;
    int displayedPlayer_ = 0;
    bool displayedOver_ = false;
    std::array<Stats, kPlayerSlots> stats_{};
};

// Requests built from a client's state for the player whose turn it is.
MoveRequest moveOrder(const BattleState& s, const std::vector<int>& units, Point target);
ShootRequest shootOrder(const BattleState& s, const std::vector<int>& units, Point target, bool autofire);
// Should a right click on this tile fire rather than move? True for enemy
// units and buildings of the player whose turn it is.
bool isEnemyTarget(const BattleState& s, Point tile);

} // namespace openday::game
