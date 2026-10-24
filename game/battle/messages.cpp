#include "battle/messages.hpp"

namespace openday::game {
namespace {

template <class... Fs> struct Overloaded : Fs... {
    using Fs::operator()...;
};
template <class... Fs> Overloaded(Fs...) -> Overloaded<Fs...>;

} // namespace

void applyEvent(BattleState& s, const Event& event) {
    std::visit(Overloaded{
                   [&](const UnitMoved& e) {
                       if (!e.tiles.empty()) s.moveUnit(e.unit, e.tiles.back());
                       s.unit(e.unit).facing = e.facing;
                       s.unit(e.unit).ap = e.ap;
                   },
                   [&](const UnitShot& e) {
                       s.unit(e.unit).facing = e.facing;
                       s.unit(e.unit).ap = e.ap;
                   },
                   [&](const UnitDamaged& e) {
                       BattleUnit& u = s.unit(e.unit);
                       u.armor = e.armor;
                       u.ap = e.ap;
                       u.negAp = e.negAp;
                   },
                   [&](const BuildingDamaged& e) { s.building(e.building).armor = e.armor; },
                   [&](const WallDamaged& e) { s.wall(e.wall).armor = e.armor; },
                   [&](const UnitDestroyed& e) {
                       const Point p = s.unit(e.unit).pos;
                       countUnitDestroyed(s, s.unit(e.unit).type->typeId, s.unit(e.unit).owner, e.cause.attacker);
                       s.unit(e.unit).armor = 0;
                       s.removeUnit(e.unit);
                       if (!e.wreck.empty()) s.addRuin({e.wreck, p});
                   },
                   [&](const BuildingDestroyed& e) {
                       countBuildingDestroyed(s, s.building(e.building).type->id, s.building(e.building).owner,
                                              e.cause.attacker);
                       s.building(e.building).armor = 0;
                       s.removeBuilding(e.building);
                       for (const BattleRuin& r : e.rubble) s.addRuin(r);
                   },
                   [&](const WallDestroyed& e) {
                       // Concrete walls next to a destroyed piece get a broken
                       // end (docs/mechanics/map-objects.md).
                       const Point p = s.wall(e.wall).pos;
                       s.wall(e.wall).armor = 0;
                       s.removeWall(e.wall);
                       for (const Point d : {Point{0, -1}, Point{1, 0}, Point{0, 1}, Point{-1, 0}}) {
                           const int w = s.wallAt({p.x + d.x, p.y + d.y});
                           if (w >= 0 && s.wall(w).type == 0) s.wall(w).brokenEnd = true;
                       }
                   },
                   [&](const RoadDestroyed& e) { s.removeRoad(e.road); },
                   [&](const MineDestroyed& e) { s.removeMine(e.mine); },
                   [&](const MineExploded&) {},
                   [&](const MinesRevealed& e) {
                       // Slot 0 clears only hidden_AI; human players share
                       // their knowledge of mines (map-objects.md, "Mines").
                       if (e.player < 0 || e.player >= kPlayerSlots) return;
                       for (const int m : e.mines) {
                           if (e.player == 0) s.mine(m).hidden[0] = false;
                           else
                               for (int p = 1; p < kPlayerSlots; ++p) s.mine(m).hidden[static_cast<std::size_t>(p)] = false;
                       }
                   },
                   [&](const TerrainBurned& e) { s.setTerrain(e.tile, e.terrain); },
                   [&](const TurnStarted& e) {
                       s.currentPlayer = e.player;
                       s.turn = e.turn;
                       s.objectiveProgress = e.progress;
                       s.deploying = false;
                       refillAp(s, e.player);
                   },
                   [&](const BattleOver& e) {
                       s.turn = e.turn;
                       s.over = true;
                       s.success = e.success;
                       s.loser = e.loser;
                       s.results = e.results;
                   },
                   [&](const UnitAdded& e) {
                       BattleUnit u;
                       u.id = e.id;
                       u.type = s.unitType(e.typeId, e.techLevel);
                       if (!u.type) return;
                       u.owner = e.owner;
                       u.ap = e.noAp ? 0 : u.type->actionPoints;
                       u.armor = u.type->armor;
                       u.pos = e.pos;
                       u.facing = e.facing;
                       u.order = e.order;
                       s.addUnit(u);
                       std::erase_if(s.reinforcements, [&](const Reinforcement& r) { return r.unitId == e.id; });
                   },
                   [&](const OrderChanged& e) { s.unit(e.unit).order = e.order; },
                   [&](const DeployBudget& e) {
                       if (e.player >= 0 && e.player < kPlayerSlots) s.deployBudget[static_cast<std::size_t>(e.player)] = e.budget;
                   },
                   [&](const UnitRemoved& e) { s.removeUnit(e.unit); },
                   [&](const RequestRejected&) {},
                   [&](const TechChanged& e) {
                       std::erase(s.techHeld, e.building);
                       if (e.held) s.techHeld.push_back(e.building);
                   },
                   [&](const AutomatedArrived&) {},
                   [&](const UnitFiredAA& e) { s.unit(e.unit).ap = e.ap; },
                   [&](const Repaired& e) {
                       if (e.building >= 0) s.building(e.building).armor = e.buildingArmor;
                       if (e.wall >= 0) s.wall(e.wall).armor = e.wallArmor;
                       if (e.unit >= 0) {
                           s.unit(e.unit).armor = e.unitArmor;
                           s.unit(e.unit).ap = e.unitAp;
                       }
                   },
                   [&](const BridgeBuilt& e) { s.addRoad({e.roadType, e.owner, e.tile}); },
                   [&](const UnitTransported& e) {
                       BattleUnit& u = s.unit(e.unit);
                       u.ap = e.ap;
                       u.armor = e.armor;
                       if (e.pickedUp || s.inside(e.pos)) s.moveUnit(e.unit, e.pos); // off the map while carried
                       else s.removeUnit(e.unit);
                   },
                   [&](const AutomatedUsed& e) {
                       for (int p = 0; p < kPlayerSlots; ++p)
                           if (e.player < 0 || e.player == p) s.automatedUsed[static_cast<std::size_t>(p)].push_back(e.typeId);
                   },
               },
               event);
}

} // namespace openday::game
