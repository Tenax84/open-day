// The authoritative side of a tactical battle: checks requests, resolves
// moves, shots and their consequences, and runs the turn order.
// docs/mechanics/tactical-movement.md, docs/mechanics/tactical-combat.md.
#pragma once

#include <cstdint>
#include <functional>
#include <random>
#include <vector>

#include "battle/combat.hpp"
#include "battle/messages.hpp"

namespace openday::game {

class BattleServer {
public:
    // `state` should have been through startBattle.
    BattleServer(BattleState state, CombatOptions options, std::uint32_t seed);

    const BattleState& state() const { return state_; }

    // Random numbers in 0 ... n-1; tests may replace the generator.
    std::function<int(int n)> random;

    // Events for the start of the battle (the first player's turn).
    std::vector<Event> start();
    // Handles one request. The events have already been applied to the
    // server's state when this returns.
    std::vector<Event> handle(const Request& request);

private:
    void emit(Event e);
    int roll(int n) { return n > 1 ? random(n) : 0; }

    void handleMove(const MoveRequest& r);
    void handleShoot(const ShootRequest& r);
    void handleEndTurn(const EndTurnRequest& r);

    // Walks a unit along a path with all interruptions. Returns true when
    // the whole path was walked.
    bool walk(int unit, const std::vector<PathStep>& path);
    // Fires one shot at `target` (legality already checked).
    void fire(int shooter, Point aim);
    void reactionFire(int mover);
    void damageArea(Point impact, int damage, int area, bool apDamage);
    void damageTile(Point tile, int damage, bool apDamage);
    void burn(Point tile);
    void destroyUnit(int unit);
    void destroyBuilding(int building);
    int rollDamage(int base, int random, bool scaled);

    BattleState state_;
    CombatOptions options_;
    std::mt19937 rng_;
    std::vector<Event> out_;
};

} // namespace openday::game
