// One colony (continent) of the campaign: the colony database C1 ... C4
// (docs/mechanics/data-model.md, "Colony databases"; the campaign rules are
// in docs/mechanics/campaign.md).
#pragma once

#include <array>
#include <cstdint>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "data/sqlite_db.hpp"

namespace openday::game {

struct Province {
    int number = 0; // NoProvince, 1 ... n
    std::string name, title, description, objective;
    int mapSize = 0;
    int credits = 0; // industrial credits for taking the province
    int tech = 0;    // tech points for taking the province
    std::array<std::vector<std::uint8_t>, 4> mapScreen{}; // BMP files, mapScreen1 ... 4
    bool captured = false;   // Province List Captured: initial state
    int variant = 1;         // RandomChosen: the scenario variant
};

struct Colony {
    int number = 1; // 1 ... 4 (`No Continent` + 1)
    std::string name;
    int startCash = 0;
    int playerCity = 1; // province of the player's capital
    int alienCity = 0;  // province of the alien capital
    int aiAutoChance = 0, aiAutoBudget = 0;
    std::string intro, end;     // VIDEO\<name>.SMK
    std::string animation;      // COLANMS\<name>.SMK
    int animationFirst = 0;     // first frame of the looping background
    int animationFrames = 0;    // number of background frames
    std::array<int, 3> deployPercent{100, 100, 100}; // DeployEasy, DeployAvg, DeployHard
    std::vector<Province> provinces;                  // in province order
    std::vector<std::pair<int, int>> adjacent;        // Adjacent Provinces pairs as stored
    std::vector<int> buildingsAvailable;              // building TypeIDs in table order
    std::vector<int> aiAutomated;                     // automated type ids the AI may call

    const Province* province(int n) const;
    bool areAdjacent(int a, int b) const;
};

Colony loadColony(const data::SqliteDb& db);

} // namespace openday::game
