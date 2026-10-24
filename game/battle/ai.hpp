// The computer player (slot 0) in a tactical battle. It runs inside the
// server and uses the server's own move and shot resolution.
// docs/mechanics/tactical-ai.md.
#pragma once

#include <vector>

#include "battle/movement.hpp"
#include "battle/server.hpp"

namespace openday::game {

class TacticalAi {
public:
    explicit TacticalAi(BattleServer& server);

    // Plays one turn of slot 0 (section 1.2). Ending the turn is up to the
    // caller.
    void playTurn();

private:
    // A target with the firing tile chosen for it (section 3).
    struct Candidate {
        int score = -65535;
        bool found = false;
        Point target{-1, -1}; // tile the shot goes to
        int unit = -1;        // target unit, or
        int building = -1;    // target building
        Point firing{-1, -1};
        std::vector<PathStep> path; // to the firing tile; empty = fire from here
    };
    enum class Moved { Nothing, Arrived, Interrupted };

    bool over() const;
    bool isEnemy(const BattleUnit& u) const;
    bool canFireNow(const BattleUnit& u) const;
    bool legalFrom(int shooter, Point from, Point target);
    bool targetCanHit(int target, int shooter, Point at);
    int score(int shooter, int unit, int building, int steps, int weight) const;
    void consider(Candidate& best, Candidate c, Point shooterPos) const;
    Candidate evaluateUnits(int shooter, const PathMap& map, const std::vector<int>& targets);
    Candidate evaluateBuilding(int shooter, const PathMap& map, int building);
    std::vector<int> enemyUnits() const;
    Moved moveToFire(int shooter, const Candidate& c);
    bool fireAtTarget(int shooter, const Candidate& c); // true when the target was destroyed
    bool targetExists(const Candidate& c) const;
    bool walkTo(int unit, const std::vector<PathStep>& path, std::size_t steps);
    void setOrder(int unit, ScriptedOrder order);
    // Looks up an order id among the units, then the buildings.
    bool findObject(int id, int& unit, int& building) const;

    void stationaryFire(int unit);
    void hunt(int unit, int& budget);
    void moveToCenter(int unit, const PathMap& map);
    void escort(int unit, bool defend);
    void patrol(int unit);
    void attack(int unit);
    void gotoGroup(const std::vector<int>& units, bool fast);

    // Automated units (section 6).
    int draw(int n); // rand() mod n
    // The call routine: drops human types other than 4, 5 and 6.
    bool callAutomated(int typeId, Point p1, Point p2);
    void randomCall();
    void emergencyDrop();
    void scheduledCalls(int turn);
    bool dropTarget(Point& target);
    void callTransport(int unit);

    BattleServer& server_;
    BattleState& state_;
};

} // namespace openday::game
