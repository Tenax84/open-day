// Plays unit actions on a map scene: moves, attacks and destruction, with
// the clip sequences of docs/mechanics/unit-animation.md. The actions change
// the scene when their animations end (units arrive, objects disappear).
#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
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

    // Actions play one after another, each starting when the previous one
    // has ended, except the moves of a staggered phase.
    //
    // A phase is the answer to one request (unit-animation.md 4, "Start
    // ticks within a phase"); every event of it is one record. With
    // `staggerMoves` the moves start at tick 3 x (record id - 1) of the
    // phase instead of after the previous record (the units of a group
    // order set off 3 ticks apart).
    void beginPhase(bool staggerMoves);
    void nextRecord() { ++record_; }

    // Moves a unit along a path (start tile first). `ap`: the unit's usable
    // AP on arrival, -1 = unchanged.
    void move(int unit, const std::vector<Point>& path, std::optional<int> arrivalFacing = std::nullopt, int ap = -1);
    // Aims at `aim` and fires one shot per impact tile (default: one at `aim`).
    void attack(int unit, Point aim, std::vector<Point> impacts = {});
    // Leaves `wreck` (RUINS.DBI) on the tile; by default the type's ruinImageID.
    void destroyUnit(int unit, std::optional<std::string> wreck = std::nullopt);
    // Places `rubble` (Ruin layer sprites) when the explosion ends.
    void destroyBuilding(int building, std::vector<MapSprite> rubble = {});
    // A mine explosion clip on `tile` (unit-animation.md 7). Its start
    // removes the mine's sprite. `explosionRecord`: the record of a
    // triggered mine, which starts one tick after the previous record.
    void mineExplosion(Point tile, const MineType& type, bool explosionRecord);
    // Changes that redraw a unit or a building when the actions scheduled so
    // far have played: the usable AP and the explicit draw mode of a unit
    // (the darkened sprite), the armor of a building (the damaged sprite).
    void setUnitState(int unit, int ap, int mode, bool immediately = false);
    void setBuildingArmor(int building, int armor);
    // The color table of the scene's owners.
    const ColorTable& colors() const { return entities_.colors; }
    // Adds a unit that appears when the actions scheduled so far have
    // played. Returns its entity index.
    int addUnit(MapUnit unit);
    // Removes a unit without an explosion (taken back during deployment).
    void removeUnit(int unit);
    // Runs `fn` when the actions scheduled so far have played.
    void schedule(std::function<void()> fn);

    // Automated units (docs/mechanics/automated-units.md 8). The clip of a
    // call starts after the previous clip; the actions scheduled next (the
    // effect) start at its action frame.
    void automated(const AutomatedType& type, int outcome, Point tile, bool second);
    // A unit fires at the automated unit of the last automated() call.
    void antiAir(int unit);
    // A transported unit leaves the map / is put down on `tile`.
    void liftUnit(int unit);
    void placeUnit(int unit, Point tile);
    // Shows a unit with a new facing (turning it by a click).
    void turnUnit(int unit, int facing);
    // Redraws the units on a tile (a new bridge under them).
    void refreshUnitsAt(Point tile);
    // The tactical view, for the AA aim point: screen width and viewport size.
    void setView(int screenWidth, int viewWidth, int viewHeight);
    // Called when an automated-unit clip starts: centre the view on the tile.
    std::function<void(Point)> onCenter;

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
    // Where a unit stands once the actions scheduled so far have played.
    Point plannedTile(int unit) const;
    int startTick() const;
    void finish(int end, const render::Animation* shown);

    MapScene& scene_;
    MapEntities& entities_;
    const GlobalRules& rules_;
    render::AnimationCache* clips_;
    Animator animator_;
    int cursor_ = 0; // first tick of the next action
    std::vector<int> unitHidden_;     // hide counter per unit
    // Tile and facing per unit after the scheduled actions; the entities
    // only change when the animations get there.
    struct Planned {
        Point tile;
        int facing;
    };
    std::vector<Planned> planned_;
    std::vector<bool> buildingHidden_;
    int phaseStart_ = 0;   // tick 0 of the current phase
    int record_ = 0;       // record id of the current event in the phase
    bool stagger_ = false; // moves of this phase start 3 ticks apart
    int autoClipEnd_ = -1; // last tick of the latest automated-unit clip
    int aaTick_ = 0;       // when the AA units of the current call fire
    Point aaAim_{};        // the tile they face
    int screenWidth_ = 640, viewW_ = 504, viewH_ = 448;
};

} // namespace openday::game
