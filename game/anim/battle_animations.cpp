#include "anim/battle_animations.hpp"

#include <algorithm>
#include <cstdlib>

#include "battle/automated.hpp"

namespace openday::game {
namespace {

int lastFrame(const render::Animation* a) { return a && !a->frames.empty() ? static_cast<int>(a->frames.size()) - 1 : 0; }

// The next part of a sequence starts one tick after a part that showed
// frames, or on the same tick after an empty one.
int after(int end, const render::Animation* shown) { return shown ? end + 1 : end; }

} // namespace

BattleAnimations::BattleAnimations(MapScene& scene, MapEntities& entities, const GlobalRules& rules,
                                   render::AnimationCache* clips)
    : scene_(scene), entities_(entities), rules_(rules), clips_(clips), unitHidden_(entities.units.size(), 0),
      buildingHidden_(entities.buildings.size(), false) {
    for (const MapUnit& u : entities.units) planned_.push_back({{u.x, u.y}, u.facing});
    scene_.useRules(rules_); // Overlapping terrain for the occlusion repair
}

void BattleAnimations::beginPhase(bool staggerMoves) {
    phaseStart_ = startTick();
    record_ = 0;
    stagger_ = staggerMoves;
}

void BattleAnimations::setUnitState(int unit, int ap, int mode, bool immediately) {
    auto apply = [this, unit, ap, mode] {
        MapUnit& u = entities_.units[static_cast<std::size_t>(unit)];
        u.ap = ap;
        u.mode = mode;
        refreshUnit(unit);
    };
    if (immediately) apply();
    else schedule(apply);
}

void BattleAnimations::setBuildingArmor(int building, int armor) {
    schedule([this, building, armor] {
        MapBuilding& b = entities_.buildings[static_cast<std::size_t>(building)];
        b.armor = armor;
        if (b.alive) scene_.replaceEntity(Layer::Building, building, buildingSprites(b, building, rules_, scene_.terrain()));
    });
}

void BattleAnimations::mineExplosion(Point tile, const MineType& type, bool explosionRecord) {
    AnimTrack t;
    // The explosion record starts at its parent's end + 1.
    t.start = startTick() + (explosionRecord ? 1 : 0);
    t.clips[0] = clip(mineClipName(type));
    t.duration = lastFrame(t.clips[0]);
    const render::Animation* c = t.clips[0];
    t.pos = framePosition(tile.x, tile.y, scene_.terrain().size, c ? c->width : 0, c ? c->height : 0);
    t.tile = tile;
    t.group = 1; // on top, no occlusion repair
    t.sound = c;
    if (c) t.onStart = [this, tile] { scene_.removeObjects(tile.x, tile.y, Layer::Mine); };
    finish(animator_.add(std::move(t)), c);
}

Point BattleAnimations::plannedTile(int unit) const { return planned_[static_cast<std::size_t>(unit)].tile; }

const render::Animation* BattleAnimations::clip(const std::string& name) {
    return clips_ && !name.empty() ? clips_->get(name) : nullptr;
}

const render::Animation* BattleAnimations::unitClip(const UnitType& type, int color, UnitAction action, int facing) {
    for (const int c : {color, 1, 0})
        if (const render::Animation* a = clip(unitClipName(type, c, action, facing))) return a;
    return nullptr;
}

int BattleAnimations::startTick() const { return std::max(cursor_, animator_.now() + 1); }

void BattleAnimations::finish(int end, const render::Animation* shown) { cursor_ = std::max(cursor_, after(end, shown)); }

void BattleAnimations::schedule(std::function<void()> fn) {
    AnimTrack t;
    t.start = startTick();
    t.onStart = std::move(fn);
    finish(animator_.add(std::move(t)), nullptr);
}

int BattleAnimations::addUnit(MapUnit unit) {
    const int index = static_cast<int>(entities_.units.size());
    entities_.units.push_back(unit);
    unitHidden_.push_back(1);
    planned_.push_back({{unit.x, unit.y}, unit.facing});
    schedule([this, index] {
        refreshUnit(index);
        showUnit(index);
    });
    return index;
}

void BattleAnimations::removeUnit(int unit) {
    schedule([this, unit] {
        entities_.units[static_cast<std::size_t>(unit)].alive = false;
        refreshUnit(unit);
    });
}

void BattleAnimations::hideUnit(int unit) { ++unitHidden_[static_cast<std::size_t>(unit)]; }

void BattleAnimations::showUnit(int unit) {
    int& h = unitHidden_[static_cast<std::size_t>(unit)];
    h = std::max(0, h - 1);
}

void BattleAnimations::refreshUnit(int unit) {
    const MapUnit& u = entities_.units[static_cast<std::size_t>(unit)];
    std::vector<MapSprite> sprites;
    if (u.alive) {
        const TerrainMap& t = scene_.terrain();
        const bool onBridge = t.inside(u.x, u.y) && rules_.classOf(t.at(u.x, u.y)) == TerrainClass::DeepWater &&
                              scene_.hasObject(u.x, u.y, Layer::Road);
        sprites.push_back(unitSprite(u, unit, t, rules_, onBridge));
    }
    scene_.replaceEntity(Layer::Unit, unit, std::move(sprites));
}

int BattleAnimations::unitAt(Point tile) const {
    for (std::size_t i = 0; i < entities_.units.size(); ++i) {
        const MapUnit& u = entities_.units[i];
        if (u.alive && u.x == tile.x && u.y == tile.y) return static_cast<int>(i);
    }
    return -1;
}

int BattleAnimations::buildingAt(Point tile) const {
    for (std::size_t i = 0; i < entities_.buildings.size(); ++i) {
        const MapBuilding& b = entities_.buildings[i];
        if (b.alive && tile.x >= b.x && tile.y >= b.y && tile.x < b.x + b.type->sizeX && tile.y < b.y + b.type->sizeY)
            return static_cast<int>(i);
    }
    return -1;
}

void BattleAnimations::move(int unit, const std::vector<Point>& path, std::optional<int> arrivalFacing, int ap) {
    if (unit < 0 || path.size() < 2) return;
    MapUnit& u = entities_.units[static_cast<std::size_t>(unit)];
    if (!u.alive) return;
    const UnitType& type = *u.type;
    const int n = scene_.terrain().size;
    const Point from = path.front(), to = path.back();
    const bool adjacent = std::abs(to.x - path[path.size() - 2].x) <= 1 && std::abs(to.y - path[path.size() - 2].y) <= 1;
    const int lastFacing = arrivalFacing ? *arrivalFacing & 7
                           : adjacent    ? facingOfStep(to.x - path[path.size() - 2].x, to.y - path[path.size() - 2].y)
                                         : facingToward(path[path.size() - 2], to);

    AnimTrack begin;
    // In a staggered phase the begin part starts at tick 3 x (record id - 1).
    begin.start = stagger_ ? std::max(phaseStart_ + 3 * std::max(0, record_ - 1), animator_.now() + 1) : startTick();
    begin.clips[0] = unitClip(type, u.color, UnitAction::MoveStart, planned_[static_cast<std::size_t>(unit)].facing);
    begin.duration = lastFrame(begin.clips[0]);
    begin.pos = framePosition(from.x, from.y, n, begin.clips[0] ? begin.clips[0]->width : 0,
                              begin.clips[0] ? begin.clips[0]->height : 0);
    begin.tile = from;
    begin.occlusion = Occlusion::AtRest;
    begin.sound = begin.clips[0];
    begin.onStart = [this, unit] { hideUnit(unit); };
    const render::Animation* beginClip = begin.clips[0];
    const int beginEnd = animator_.add(std::move(begin));

    AnimTrack slide;
    slide.start = after(beginEnd, beginClip);
    slide.loop = true;
    slide.occlusion = Occlusion::Translate;
    const render::Animation* any = nullptr;
    for (int f = 0; f < 8; ++f) {
        slide.clips[static_cast<std::size_t>(f)] = unitClip(type, u.color, UnitAction::Move, f);
        if (!any) any = slide.clips[static_cast<std::size_t>(f)];
    }
    if (any) slide.path = slidePositions(path, type.movePixSpeed, n, any->width, any->height);
    slide.duration = slide.path.empty() ? 0 : static_cast<int>(slide.path.size()) - 1;
    // The original plays the engine sound from a looping facing-0 copy; here it plays once.
    slide.sound = slide.path.empty() ? nullptr : slide.clips[0];
    const bool slideShown = !slide.path.empty();
    const int slideEnd = animator_.add(std::move(slide));

    AnimTrack end;
    end.start = slideShown ? slideEnd + 1 : slideEnd;
    end.clips[0] = unitClip(type, u.color, UnitAction::MoveEnd, lastFacing);
    end.duration = lastFrame(end.clips[0]);
    end.pos = framePosition(to.x, to.y, n, end.clips[0] ? end.clips[0]->width : 0, end.clips[0] ? end.clips[0]->height : 0);
    end.tile = to; // the refresh list of the destination *(OpenDay choice)*
    end.occlusion = Occlusion::AtRest;
    end.sound = end.clips[0];
    end.onEnd = [this, unit, to, lastFacing, ap] {
        MapUnit& mu = entities_.units[static_cast<std::size_t>(unit)];
        mu.x = to.x;
        mu.y = to.y;
        mu.facing = lastFacing;
        if (ap >= 0) mu.ap = ap;
        refreshUnit(unit);
        showUnit(unit);
    };
    const render::Animation* endClip = end.clips[0];
    finish(animator_.add(std::move(end)), endClip);
    planned_[static_cast<std::size_t>(unit)] = {to, lastFacing};
}

void BattleAnimations::attack(int unit, Point target, std::vector<Point> impacts) {
    if (unit < 0) return;
    if (impacts.empty()) impacts.push_back(target);
    MapUnit& u = entities_.units[static_cast<std::size_t>(unit)];
    if (!u.alive) return;
    const UnitType& type = *u.type;
    const int n = scene_.terrain().size;
    const Point from = plannedTile(unit);
    const int facing = type.hasFacings ? facingToward(from, target) : 0;
    planned_[static_cast<std::size_t>(unit)].facing = facing;
    // Unit clips are ground animations with the at-rest refresh list; the
    // projectile and the impact float on top.
    auto place = [&](AnimTrack& t, Point tile) {
        const render::Animation* c = t.clips[0];
        t.pos = framePosition(tile.x, tile.y, n, c ? c->width : 0, c ? c->height : 0);
        t.tile = tile;
        t.sound = c;
        t.occlusion = Occlusion::AtRest;
    };

    AnimTrack begin;
    begin.start = startTick();
    begin.clips[0] = unitClip(type, u.color, UnitAction::ShootBegin, facing);
    begin.duration = lastFrame(begin.clips[0]);
    place(begin, from);
    begin.onStart = [this, unit, facing] {
        hideUnit(unit);
        entities_.units[static_cast<std::size_t>(unit)].facing = facing;
        refreshUnit(unit);
    };
    const render::Animation* prevClip = begin.clips[0];
    int prevEnd = animator_.add(std::move(begin));

    const render::Animation* shotClip = unitClip(type, u.color, UnitAction::Shoot, facing);
    const render::Animation* projClip = unitClip(type, u.color, UnitAction::Projectile, 0);
    const render::Animation* impactClip = unitClip(type, u.color, UnitAction::Impact, 0);
    int lastImpact = 0;
    const render::Animation* lastImpactClip = nullptr;
    for (const Point impactTile : impacts) {
        AnimTrack shot;
        shot.start = after(prevEnd, prevClip);
        shot.clips[0] = shotClip;
        shot.duration = lastFrame(shotClip);
        place(shot, from);
        const int shotStart = std::max(shot.start, animator_.now() + 1);
        prevEnd = animator_.add(std::move(shot));
        prevClip = shotClip;

        AnimTrack proj;
        proj.group = 1;
        proj.start = shotStart + type.projectileStart;
        proj.clips[0] = projClip;
        if (projClip)
            for (const Point& p : projectilePath(from, impactTile, type, n, projClip->width, projClip->height))
                proj.path.push_back({p, {-1, -1}, 0});
        proj.duration = proj.path.empty() ? 0 : static_cast<int>(proj.path.size()) - 1;
        proj.sound = proj.path.empty() ? nullptr : projClip;
        const bool projShown = !proj.path.empty();
        const int projEnd = animator_.add(std::move(proj));

        AnimTrack impact;
        impact.start = projShown ? projEnd + 1 : projEnd;
        impact.clips[0] = impactClip;
        impact.duration = lastFrame(impactClip);
        place(impact, impactTile);
        impact.group = 1;
        impact.occlusion = Occlusion::None;
        lastImpact = std::max(lastImpact, animator_.add(std::move(impact)));
        lastImpactClip = impactClip;
    }

    AnimTrack end;
    end.start = after(prevEnd, prevClip);
    end.clips[0] = unitClip(type, u.color, UnitAction::ShootEnd, facing);
    end.duration = lastFrame(end.clips[0]);
    place(end, from);
    end.onEnd = [this, unit] { showUnit(unit); };
    const render::Animation* endClip = end.clips[0];
    finish(animator_.add(std::move(end)), endClip);
    finish(lastImpact, lastImpactClip);
}

void BattleAnimations::destroyUnit(int unit, std::optional<std::string> wreck) {
    if (unit < 0) return;
    const MapUnit& u = entities_.units[static_cast<std::size_t>(unit)];
    if (!u.alive) return;
    AnimTrack t;
    t.start = startTick();
    t.clips[0] = unitClip(*u.type, u.color, UnitAction::Explosion, 0);
    t.duration = lastFrame(t.clips[0]);
    const render::Animation* c = t.clips[0];
    const Point at = plannedTile(unit);
    t.pos = framePosition(at.x, at.y, scene_.terrain().size, c ? c->width : 0, c ? c->height : 0);
    t.tile = at;
    t.group = 1;
    t.sound = c;
    t.onStart = [this, unit] { hideUnit(unit); };
    t.onEnd = [this, unit, wreck] {
        MapUnit& mu = entities_.units[static_cast<std::size_t>(unit)];
        if (!mu.alive) return;
        mu.alive = false;
        refreshUnit(unit);
        const std::string name = wreck ? *wreck : mu.type->ruinImage;
        if (!name.empty()) scene_.addObject({mu.x, mu.y, Layer::Ruin, SpriteSet::Ruins, name});
    };
    finish(animator_.add(std::move(t)), c);
}

void BattleAnimations::destroyBuilding(int building, std::vector<MapSprite> rubble) {
    if (building < 0) return;
    const MapBuilding& b = entities_.buildings[static_cast<std::size_t>(building)];
    if (!b.alive) return;
    const BuildingType& type = *b.type;
    AnimTrack t;
    t.start = startTick();
    t.clips[0] = clip(buildingClipName(type));
    t.duration = lastFrame(t.clips[0]);
    const render::Animation* c = t.clips[0];
    t.pos = buildingFramePosition(b.x, b.y, type.sizeX, type.sizeY, scene_.terrain().size, c ? c->width : 0,
                                  c ? c->height : 0);
    t.group = 1;
    t.sound = c;
    // With a clip the building disappears while the clip plays; without one
    // (towers) it stays until it is removed.
    if (c) t.onStart = [this, building] { buildingHidden_[static_cast<std::size_t>(building)] = true; };
    t.onEnd = [this, building, rubble = std::move(rubble)] {
        entities_.buildings[static_cast<std::size_t>(building)].alive = false;
        scene_.replaceEntity(Layer::Building, building, {});
        for (const MapSprite& r : rubble) scene_.addObject(r);
    };
    finish(animator_.add(std::move(t)), c);
}

SceneExtras BattleAnimations::extras() const {
    SceneExtras e;
    e.hidden = [this](const MapSprite& s) {
        if (s.entity < 0) return false;
        const auto i = static_cast<std::size_t>(s.entity);
        if (s.layer == Layer::Unit) return i < unitHidden_.size() && unitHidden_[i] > 0;
        if (s.layer == Layer::Building) return i < buildingHidden_.size() && buildingHidden_[i];
        return false;
    };
    e.frames = animator_.frames();
    return e;
}

} // namespace openday::game

namespace openday::game {

void BattleAnimations::setView(int screenWidth, int viewWidth, int viewHeight) {
    screenWidth_ = screenWidth;
    viewW_ = viewWidth;
    viewH_ = viewHeight;
}

void BattleAnimations::automated(const AutomatedType& type, int outcome, Point tile, bool second) {
    const int n = scene_.terrain().size;
    AnimTrack t;
    // One clip after the other; the second clip of a transport starts when
    // the first one ends (automated-units.md 8.2).
    t.start = std::max(startTick(), autoClipEnd_ + 1);
    t.clips[0] = clip(automatedClipName(type, second, static_cast<AutomatedOutcome>(std::clamp(outcome, 1, 3))));
    t.duration = lastFrame(t.clips[0]);
    const render::Animation* c = t.clips[0];
    t.pos = framePosition(tile.x, tile.y, n, c ? c->width : 0, c ? c->height : 0);
    t.group = 1; // drawn over everything
    t.sound = c;
    t.onStart = [this, tile] {
        if (onCenter) onCenter(tile);
    };
    const int start = t.start;
    autoClipEnd_ = animator_.add(std::move(t));
    // Frame numbers are 1-based; without a clip everything happens at once.
    auto at = [&](int frame) { return c ? start + std::clamp(frame - 1, 0, lastFrame(c)) : start; };
    aaTick_ = at(second ? type.secondFrameAA : type.frameAA);
    const int action = at(second ? type.secondFrameAction : type.frameAction);
    // The AA units aim at a screen point of the view centred on the tile,
    // scaled by the screen width (8.4).
    const int ax = (second ? type.secondAAX : type.aaX) * screenWidth_ / 1024;
    const int ay = (second ? type.secondAAY : type.aaY) * screenWidth_ / 1024;
    const Point o = iso::tileOrigin(tile.x, tile.y, n);
    aaAim_ = iso::tileAt(o.x + 27 - viewW_ / 2 + ax, o.y + 42 - viewH_ / 2 + ay, n);
    // The effect records play from the action frame on.
    cursor_ = std::max(cursor_, action);
}

void BattleAnimations::antiAir(int unit) {
    if (unit < 0) return;
    const MapUnit& u = entities_.units[static_cast<std::size_t>(unit)];
    if (!u.alive) return;
    const Point from = plannedTile(unit);
    const int facing = u.type->hasFacings ? facingToward(from, aaAim_) : planned_[static_cast<std::size_t>(unit)].facing;
    planned_[static_cast<std::size_t>(unit)].facing = facing;
    AnimTrack t;
    t.start = std::max(aaTick_, animator_.now() + 1);
    t.clips[0] = unitClip(*u.type, u.color, UnitAction::Shoot, facing);
    t.duration = lastFrame(t.clips[0]);
    const render::Animation* c = t.clips[0];
    t.pos = framePosition(from.x, from.y, scene_.terrain().size, c ? c->width : 0, c ? c->height : 0);
    t.tile = from;
    t.occlusion = Occlusion::AtRest;
    t.sound = c;
    t.onStart = [this, unit, facing] {
        hideUnit(unit);
        entities_.units[static_cast<std::size_t>(unit)].facing = facing;
        refreshUnit(unit);
    };
    t.onEnd = [this, unit] { showUnit(unit); };
    animator_.add(std::move(t)); // does not delay the effect
}

void BattleAnimations::liftUnit(int unit) {
    schedule([this, unit] { hideUnit(unit); });
}

void BattleAnimations::placeUnit(int unit, Point tile) {
    planned_[static_cast<std::size_t>(unit)].tile = tile;
    schedule([this, unit, tile] {
        MapUnit& mu = entities_.units[static_cast<std::size_t>(unit)];
        mu.x = tile.x;
        mu.y = tile.y;
        refreshUnit(unit);
        showUnit(unit);
    });
}

void BattleAnimations::refreshUnitsAt(Point tile) {
    schedule([this, tile] {
        for (std::size_t i = 0; i < entities_.units.size(); ++i)
            if (entities_.units[i].alive && entities_.units[i].x == tile.x && entities_.units[i].y == tile.y)
                refreshUnit(static_cast<int>(i));
    });
}

} // namespace openday::game
