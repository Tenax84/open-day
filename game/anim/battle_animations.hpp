// Plays unit actions on a map scene: moves, attacks and destruction, with
// the clip sequences of docs/mechanics/unit-animation.md. The actions change
// the scene when their animations end (units arrive, objects disappear).
#pragma once

#include <memory>
#include <vector>

#include "anim/animator.hpp"
#include "map/map_objects.hpp"
#include "map/map_renderer.hpp"
#include "map/rules.hpp"
#include "render/animation.hpp"

namespace openday::game {

class BattleAnimations {
public:
    // All references must outlive this object. `clips` may be null: then
    // every action runs without pictures, as with NoIsoAnim=1.
    BattleAnimations(MapScene& scene, MapEntities& entities, const GlobalRules& rules, render::AnimationCache* clips);

    Animator& animator() { return animator_; }
    bool busy() const { return !animator_.idle(); }
    void tick() { animator_.tick(); }

    // Moves a unit along an 8-connected path (start tile first).
    void move(int unit, const std::vector<Point>& path);
    // Fires `shots` shots at a tile.
    void attack(int unit, Point target, int shots = 1);
    void destroyUnit(int unit);
    void destroyBuilding(int building);

    // Unit / building at a tile, or -1.
    int unitAt(Point tile) const;
    int buildingAt(Point tile) const;

    // Hidden static sprites and the frames of the current tick, for MapScene::draw.
    SceneExtras extras() const;

private:
    const render::Animation* clip(const std::string& name);
    // A unit clip; falls back to color variants 1 and 0 when the unit's own
    // variant does not exist (OpenDay behavior, see unit-animation.md).
    const render::Animation* unitClip(const UnitType& type, int color, UnitAction action, int facing);
    void showUnit(int unit);
    void hideUnit(int unit);
    void refreshUnit(int unit);

    MapScene& scene_;
    MapEntities& entities_;
    const GlobalRules& rules_;
    render::AnimationCache* clips_;
    Animator animator_;
    std::vector<int> unitHidden_;     // hide counter per unit
    std::vector<bool> buildingHidden_;
};

} // namespace openday::game
