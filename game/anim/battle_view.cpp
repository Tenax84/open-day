#include "anim/battle_view.hpp"

#include <algorithm>
#include <utility>

namespace openday::game {
namespace {

template <class... Fs> struct Overloaded : Fs... {
    using Fs::operator()...;
};
template <class... Fs> Overloaded(Fs...) -> Overloaded<Fs...>;

} // namespace

BattleView::BattleView(BattleState initial, MapScene& scene, BattleAnimations& animations, int viewer)
    : state_(std::move(initial)), scene_(scene), anim_(animations), viewer_(viewer),
      wallShape_(state_.walls().size(), -1) {
    if (state_.currentPlayer > 0) lastHuman_ = state_.currentPlayer;
    else
        for (const int p : state_.players)
            if (p > 0) {
                lastHuman_ = p;
                break;
            }
    wallLayer(); // remember the loaded shapes
    scene_.replaceLayer(Layer::Mine, mineLayer());
    syncUnits(true);
}

void BattleView::syncUnits(bool immediately) {
    const std::size_t n = state_.units().size();
    shownAp_.resize(n, -2);
    shownMode_.resize(n, -1);
    deployed_.resize(n, false);
    for (std::size_t i = 0; i < n; ++i) {
        const BattleUnit& u = state_.units()[i];
        if (!u.alive) continue;
        if (!state_.deploying) deployed_[i] = false;
        // Units placed in the deployment view are drawn darkened (mode 1);
        // the others by their AP (map-objects.md, "Draw mode").
        const int ap = u.availableAp(), mode = deployed_[i] ? 1 : 0;
        if (ap == shownAp_[i] && mode == shownMode_[i]) continue;
        shownAp_[i] = ap;
        shownMode_[i] = mode;
        anim_.setUnitState(static_cast<int>(i), ap, mode, immediately);
    }
}

std::vector<MapSprite> BattleView::roadLayer() const {
    std::vector<RoadPiece> pieces;
    for (const BattleRoad& r : state_.roads())
        if (r.alive) pieces.push_back({r.type, r.pos.x, r.pos.y});
    return roadSprites(pieces, state_.rules(), state_.terrain());
}

std::vector<MapSprite> BattleView::wallLayer() {
    std::vector<WallPiece> pieces;
    std::vector<std::size_t> index;
    for (std::size_t i = 0; i < state_.walls().size(); ++i) {
        const BattleWall& w = state_.walls()[i];
        if (!w.alive) continue;
        pieces.push_back({w.type, w.pos.x, w.pos.y, w.armor, w.brokenEnd, wallShape_[i]});
        index.push_back(i);
    }
    std::vector<MapSprite> sprites = wallSprites(pieces, state_.rules(), state_.terrain());
    for (std::size_t k = 0; k < pieces.size(); ++k) wallShape_[index[k]] = pieces[k].previous;
    return sprites;
}

std::vector<MapSprite> BattleView::mineLayer() const {
    std::vector<MapSprite> out;
    const int v = viewer();
    for (const BattleMine& m : state_.mines()) {
        if (!m.alive || (v >= 0 && v < kPlayerSlots && m.hidden[static_cast<std::size_t>(v)])) continue;
        const auto type = state_.rules().mines.find(m.type);
        if (type != state_.rules().mines.end())
            out.push_back({m.pos.x, m.pos.y, Layer::Mine, SpriteSet::Mines, mineSpriteName(type->second, m.armed)});
    }
    return out;
}

void BattleView::redraw(Layer layer, std::vector<MapSprite> sprites) {
    anim_.schedule([this, layer, sprites = std::move(sprites)]() mutable { scene_.replaceLayer(layer, std::move(sprites)); });
}

bool BattleView::turnUnit(int unit) {
    BattleUnit& u = state_.unit(unit);
    if (!u.alive || !u.type->hasFacings) return false;
    u.facing = (u.facing + 1) % 8;
    anim_.turnUnit(unit, u.facing);
    return true;
}

void BattleView::apply(const std::vector<Event>& events) {
    for (const Event& event : events) {
        anim_.nextRecord();
        // Animations are started from the state before the event.
        std::visit(Overloaded{
                       [&](const UnitMoved& e) {
                           std::vector<Point> path{state_.unit(e.unit).pos};
                           path.insert(path.end(), e.tiles.begin(), e.tiles.end());
                           const BattleUnit& u = state_.unit(e.unit);
                           const int ap = std::max(0, e.ap - u.negAp);
                           anim_.move(e.unit, path, e.facing, ap);
                           // The end of the move shows the new AP already.
                           if (static_cast<std::size_t>(e.unit) < shownAp_.size()) shownAp_[static_cast<std::size_t>(e.unit)] = ap;
                       },
                       [&](const UnitShot& e) { anim_.attack(e.unit, e.aim, {e.impact}); },
                       [&](const UnitDestroyed& e) { anim_.destroyUnit(e.unit, e.wreck); },
                       [&](const BuildingDestroyed& e) {
                           std::vector<MapSprite> rubble;
                           for (const BattleRuin& r : e.rubble)
                               rubble.push_back({r.pos.x, r.pos.y, Layer::Ruin, SpriteSet::Ruins, r.name});
                           anim_.destroyBuilding(e.building, std::move(rubble));
                       },
                       [&](const UnitAdded& e) {
                           if (const UnitType* t = state_.unitType(e.typeId, e.techLevel)) {
                               const std::size_t index = state_.units().size();
                               deployed_.resize(index + 1, false);
                               deployed_[index] = state_.deploying;
                               anim_.addUnit({t, anim_.colors()(e.owner), e.facing, e.pos.x, e.pos.y, true});
                           }
                       },
                       [&](const MineExploded& e) {
                           const BattleMine& m = state_.mine(e.mine);
                           const auto type = state_.rules().mines.find(m.type);
                           if (type != state_.rules().mines.end()) anim_.mineExplosion(m.pos, type->second, true);
                           lastExploded_ = e.mine;
                       },
                       [&](const MineDestroyed& e) {
                           // A triggered mine plays its clip once, with the
                           // explosion record *(OpenDay choice)*.
                           const BattleMine& m = state_.mine(e.mine);
                           const auto type = state_.rules().mines.find(m.type);
                           if (lastExploded_ != e.mine && type != state_.rules().mines.end())
                               anim_.mineExplosion(m.pos, type->second, false);
                       },
                       [&](const UnitRemoved& e) { anim_.removeUnit(e.unit); },
                       [&](const TerrainBurned& e) {
                           anim_.schedule([this, e] { scene_.setTerrain(e.tile.x, e.tile.y, e.terrain); });
                       },
                       [&](const AutomatedArrived& e) {
                           const auto t = state_.rules().automated.find(e.typeId);
                           if (t != state_.rules().automated.end()) anim_.automated(t->second, e.outcome, e.tile, e.second);
                       },
                       [&](const UnitFiredAA& e) { anim_.antiAir(e.unit); },
                       [&](const UnitTransported& e) {
                           if (e.pickedUp) anim_.liftUnit(e.unit);
                           else if (state_.inside(e.pos)) anim_.placeUnit(e.unit, e.pos);
                           else anim_.removeUnit(e.unit);
                       },
                       [&](const auto&) {},
                   },
                   event);
        applyEvent(state_, event);
        // Layers drawn from the state are redrawn after the change.
        std::visit(Overloaded{
                       [&](const WallDamaged&) { redraw(Layer::Wall, wallLayer()); },
                       [&](const WallDestroyed&) { redraw(Layer::Wall, wallLayer()); },
                       [&](const RoadDestroyed&) { redraw(Layer::Road, roadLayer()); },
                       [&](const MineDestroyed&) { redraw(Layer::Mine, mineLayer()); },
                       [&](const MinesRevealed&) { redraw(Layer::Mine, mineLayer()); },
                       [&](const BridgeBuilt& e) {
                           redraw(Layer::Road, roadLayer());
                           anim_.refreshUnitsAt(e.tile);
                       },
                       [&](const Repaired& e) {
                           if (e.wall >= 0) redraw(Layer::Wall, wallLayer());
                           if (e.building >= 0) anim_.setBuildingArmor(e.building, e.buildingArmor);
                       },
                       [&](const BuildingDamaged& e) { anim_.setBuildingArmor(e.building, e.armor); },
                       [&](const TurnStarted& e) {
                           if (e.player > 0 && e.player != lastHuman_) {
                               lastHuman_ = e.player;
                               if (viewer_ < 0) redraw(Layer::Mine, mineLayer());
                           }
                       },
                       [&](const auto&) {},
                   },
                   event);
        syncUnits(false);
    }
}

} // namespace openday::game
