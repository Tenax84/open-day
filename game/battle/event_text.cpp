#include "battle/messages.hpp"

#include <string>

namespace openday::game {
namespace {

template <class... Fs> struct Overloaded : Fs... {
    using Fs::operator()...;
};
template <class... Fs> Overloaded(Fs...) -> Overloaded<Fs...>;

std::string pt(Point p) { return "(" + std::to_string(p.x) + "," + std::to_string(p.y) + ")"; }

std::string cause(const DamageCause& c) {
    std::string s = " by=" + std::to_string(c.attacker);
    if (c.automatedType >= 0) s += " automated=" + std::to_string(c.automatedType);
    return s;
}

std::string ids(const std::vector<int>& v) {
    std::string s = "[";
    for (std::size_t i = 0; i < v.size(); ++i) s += (i ? "," : "") + std::to_string(v[i]);
    return s + "]";
}

} // namespace

std::string describeEvent(const Event& event) {
    using std::to_string;
    return std::visit(
        Overloaded{
            [](const UnitMoved& e) {
                return "UnitMoved unit=" + to_string(e.unit) + " to=" + (e.tiles.empty() ? std::string("-") : pt(e.tiles.back())) +
                       " steps=" + to_string(e.tiles.size()) + " facing=" + to_string(e.facing) + " ap=" + to_string(e.ap);
            },
            [](const UnitShot& e) {
                return "UnitShot unit=" + to_string(e.unit) + " aim=" + pt(e.aim) + " impact=" + pt(e.impact) +
                       " ap=" + to_string(e.ap);
            },
            [](const UnitDamaged& e) {
                return "UnitDamaged unit=" + to_string(e.unit) + " armor=" + to_string(e.armor) + " ap=" + to_string(e.ap) +
                       " negAp=" + to_string(e.negAp);
            },
            [](const BuildingDamaged& e) {
                return "BuildingDamaged building=" + to_string(e.building) + " armor=" + to_string(e.armor);
            },
            [](const WallDamaged& e) { return "WallDamaged wall=" + to_string(e.wall) + " armor=" + to_string(e.armor); },
            [](const UnitDestroyed& e) {
                return "UnitDestroyed unit=" + to_string(e.unit) + cause(e.cause) + (e.wreck.empty() ? "" : " wreck=" + e.wreck);
            },
            [](const BuildingDestroyed& e) {
                return "BuildingDestroyed building=" + to_string(e.building) + cause(e.cause) +
                       " rubble=" + to_string(e.rubble.size());
            },
            [](const WallDestroyed& e) { return "WallDestroyed wall=" + to_string(e.wall); },
            [](const RoadDestroyed& e) { return "RoadDestroyed road=" + to_string(e.road); },
            [](const MineDestroyed& e) { return "MineDestroyed mine=" + to_string(e.mine); },
            [](const MineExploded& e) { return "MineExploded mine=" + to_string(e.mine); },
            [](const MinesRevealed& e) { return "MinesRevealed player=" + to_string(e.player) + " mines=" + ids(e.mines); },
            [](const TerrainBurned& e) { return "TerrainBurned tile=" + pt(e.tile) + " terrain=" + e.terrain; },
            [](const TurnStarted& e) {
                return "TurnStarted player=" + to_string(e.player) + " turn=" + to_string(e.turn) +
                       " progress=" + to_string(e.progress);
            },
            [](const BattleOver& e) {
                return "BattleOver turn=" + to_string(e.turn) + " success=" + (e.success ? "1" : "0") +
                       " loser=" + to_string(e.loser) + " techFound=" + to_string(e.techFound);
            },
            [](const UnitAdded& e) {
                return "UnitAdded id=" + to_string(e.id) + " type=" + to_string(e.typeId) + " owner=" + to_string(e.owner) +
                       " pos=" + pt(e.pos) + (e.noAp ? " noAp" : "");
            },
            [](const OrderChanged& e) { return "OrderChanged unit=" + to_string(e.unit) + " order=" + to_string(e.order.order); },
            [](const DeployBudget& e) {
                return "DeployBudget player=" + to_string(e.player) + " budget=" + to_string(e.budget);
            },
            [](const UnitRemoved& e) { return "UnitRemoved unit=" + to_string(e.unit); },
            [](const RequestRejected& e) { return "RequestRejected reason=" + e.reason; },
            [](const TechChanged& e) {
                return "TechChanged building=" + to_string(e.building) + " held=" + (e.held ? "1" : "0");
            },
            [](const AutomatedArrived& e) {
                return "AutomatedArrived type=" + to_string(e.typeId) + " owner=" + to_string(e.owner) +
                       " outcome=" + to_string(e.outcome) + " tile=" + pt(e.tile) + (e.second ? " second" : "");
            },
            [](const UnitFiredAA& e) { return "UnitFiredAA unit=" + to_string(e.unit) + " ap=" + to_string(e.ap); },
            [](const Repaired& e) {
                return "Repaired tile=" + pt(e.tile) + " building=" + to_string(e.building) + " unit=" + to_string(e.unit) +
                       " wall=" + to_string(e.wall);
            },
            [](const BridgeBuilt& e) {
                return "BridgeBuilt tile=" + pt(e.tile) + " roadType=" + to_string(e.roadType) + " owner=" + to_string(e.owner);
            },
            [](const UnitTransported& e) {
                return "UnitTransported unit=" + to_string(e.unit) + (e.pickedUp ? " pickedUp" : " pos=" + pt(e.pos)) +
                       " ap=" + to_string(e.ap) + " armor=" + to_string(e.armor);
            },
            [](const AutomatedUsed& e) {
                return "AutomatedUsed player=" + to_string(e.player) + " type=" + to_string(e.typeId);
            },
        },
        event);
}

} // namespace openday::game
