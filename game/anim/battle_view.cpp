#include "anim/battle_view.hpp"

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
    wallLayer(); // remember the loaded shapes
    scene_.replaceLayer(Layer::Mine, mineLayer());
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
            out.push_back({m.pos.x, m.pos.y, Layer::Mine, SpriteSet::Mines, type->second.image + "000"});
    }
    return out;
}

void BattleView::redraw(Layer layer, std::vector<MapSprite> sprites) {
    anim_.schedule([this, layer, sprites = std::move(sprites)]() mutable { scene_.replaceLayer(layer, std::move(sprites)); });
}

void BattleView::apply(const std::vector<Event>& events) {
    for (const Event& event : events) {
        // Animations are started from the state before the event.
        std::visit(Overloaded{
                       [&](const UnitMoved& e) {
                           std::vector<Point> path{state_.unit(e.unit).pos};
                           path.insert(path.end(), e.tiles.begin(), e.tiles.end());
                           anim_.move(e.unit, path, e.facing);
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
                           if (const UnitType* t = state_.unitType(e.typeId, e.techLevel))
                               anim_.addUnit({t, e.owner, e.facing, e.pos.x, e.pos.y, true});
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
                       },
                       [&](const TurnStarted&) {
                           if (viewer_ < 0) redraw(Layer::Mine, mineLayer());
                       },
                       [&](const auto&) {},
                   },
                   event);
    }
}

} // namespace openday::game
