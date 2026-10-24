// Import of an original save slot (SAVEGAME\savegame.NN: GAMEINFO.MDB,
// CITY.MDB, COLONY.MDB, SCENARIO.MDB, <name>.dsc) into OpenDay's campaign
// state, including a battle in progress. docs/mechanics/savegame.md 2-4.
//
// The slot's databases are first converted to SQLite (engine/data's Jet
// reader); the mapping works on those converted files, so it can be tested
// with synthetic SQLite databases. Anything unexpected is refused with a
// std::runtime_error whose message says why.
#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "campaign/campaign.hpp"

namespace openday::game {

struct OriginalSlot {
    int number = 0;               // 1-10
    std::filesystem::path folder; // the slot folder
    std::string description;      // the name of its .dsc file; empty if none
    bool used = false;            // GAMEINFO.MDB, CITY.MDB and COLONY.MDB present
};

// The ten slot folders of an installation (<gameDir>\SAVEGAME\savegame.NN,
// names matched case-insensitively). Missing folders are listed unused.
std::vector<OriginalSlot> listOriginalSlots(const std::filesystem::path& gameDir);

// The converted databases of one slot; `scenario` may be empty.
struct OriginalSaveDatabases {
    std::filesystem::path gameInfo, city, colony, scenario;
};

// Maps converted slot databases to a campaign. A saved battle (states 1, 4,
// 6) becomes Campaign::savedBattle and resumes as the original does
// (savegame.md 3.2-3.4).
Campaign importOriginalSave(const OriginalSaveDatabases& dbs, const CampaignResources& res, const std::string& description);

// Converts the slot's MDB files into `workDir` and maps them.
Campaign importOriginalSlot(const OriginalSlot& slot, const CampaignResources& res, const std::filesystem::path& workDir);

} // namespace openday::game
