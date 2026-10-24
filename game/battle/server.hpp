// The authoritative side of a tactical battle: checks requests, resolves
// moves, shots and their consequences, and runs the turn order.
// docs/mechanics/tactical-movement.md, docs/mechanics/tactical-combat.md.
#pragma once

#include <cstdint>
#include <functional>
#include <random>
#include <string>
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
    // Continues a battle restored from a save (savegame.md 3.4, resume
    // mode): no objective check, no turn step, no income and no AP refill.
    // If the saved current player is the computer player, it plays its
    // turn from the first phase; otherwise nothing happens until the next
    // request.
    std::vector<Event> resume();

    // The parts of the server's own state that a save keeps besides
    // state() (battle_save.hpp): the random-number generator and the
    // computer player's automated-unit budget.
    std::string rngState() const;
    int aiBudget() const { return aiAutomatedBudget_; }
    // Puts them back; an empty `rng` keeps the seeded generator. Throws
    // std::runtime_error if `rng` is not a generator state.
    void restore(const std::string& rng, int aiBudget);
    // Handles one request. The events have already been applied to the
    // server's state when this returns.
    std::vector<Event> handle(const Request& request);

private:
    friend class TacticalAi;

    void emit(Event e);
    // Plays the computer player's turns while it is its turn.
    void runComputerTurns();
    int roll(int n) { return n > 1 ? random(n) : 0; }

    void handleMove(const MoveRequest& r);
    void handleShoot(const ShootRequest& r);
    void handleEndTurn(const EndTurnRequest& r);
    void handleDeploy(const DeployRequest& r);
    void handleUndeploy(const UndeployRequest& r);
    void handleAutomated(const AutomatedRequest& r);
    // Runs the objective check; ends the battle when it is decided.
    bool checkEnd();
    // battle-flow.md 1.3: objective check, next participant, new turn.
    void beginNextTurn();
    void placeReinforcements();
    // Re-evaluates the tech buildings held by the human side.
    void updateTech();
    // Reinforcement income of a human player at the start of his turn.
    int turnIncome(int player) const;

    // Walks a unit along a path with all interruptions. Returns true when
    // the whole path was walked.
    bool walk(int unit, const std::vector<PathStep>& path);
    // Fires one shot at `target` (legality already checked).
    void fire(int shooter, Point aim);
    // Fires at a tile if the shot is legal; with `repeat`, keeps firing as
    // autofire does (tactical-combat.md 2). Returns false if it could not fire.
    bool fireAt(int shooter, Point target, bool repeat);
    void reactionFire(int mover);
    void damageArea(Point impact, int damage, int area, bool apDamage);
    void damageTile(Point tile, int damage, bool apDamage);
    void burn(Point tile);
    void destroyUnit(int unit);
    void destroyBuilding(int building);
    int rollDamage(int base, int random, bool scaled);
    // An armed mine goes off (tactical-combat.md 7): damage with the mine's
    // owner as attacker, then the mines around it become visible to `revealFor`.
    void explodeMine(int mine, int revealFor);

    // --- Automated units (server_automated.cpp, automated-units.md 4-6) ---
    struct AutomatedCall {
        const AutomatedType* type = nullptr;
        int owner = 0;
        int acc = 0;                 // AA damage taken
        std::vector<int> candidates; // AA units, sorted
        int passes = 0;
    };
    // Runs a call: AA fire and the effect of its category, tech buildings,
    // battle-over check and DeployOnce (4.1). Shared with the computer player.
    void callAutomated(const AutomatedType& type, int owner, Point p1, Point p2);
    // One AA pass at `tile` (5.2); returns false when the automated unit was
    // shot down.
    bool antiAir(AutomatedCall& call, Point tile);
    void automatedDestroy(AutomatedCall& call, Point target);
    void automatedRepair(AutomatedCall& call, Point target);
    void automatedBridge(AutomatedCall& call, Point target);
    void automatedMines(AutomatedCall& call, Point target);
    void automatedDrop(AutomatedCall& call, Point target);
    void automatedTransport(AutomatedCall& call, Point source, Point destination);

    BattleState state_;
    CombatOptions options_;
    std::mt19937 rng_;
    std::vector<Event> out_;
    std::vector<int> deployed_; // units placed during deployment (may be taken back)
    DamageCause cause_;         // credited for the destructions being resolved
    int aiAutomatedBudget_ = 0; // the computer player's automated-unit budget (tactical-ai.md 6.1)
};

} // namespace openday::game
