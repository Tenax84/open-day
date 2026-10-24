// The campaign: colony progress, the player's resources and tech levels,
// the city, and the state machine that leads from battle to battle
// (docs/mechanics/campaign.md, army-tech.md). Single player: the human
// player is slot 1.
#pragma once

#include <filesystem>
#include <functional>
#include <map>
#include <random>
#include <string>
#include <vector>

#include "battle/combat.hpp"
#include "campaign/city.hpp"
#include "campaign/colony.hpp"

namespace openday::game {

inline constexpr int kCampaignPlayer = 1;

enum class CampaignState {
    FirstColony = 0,      // intro video of colony 1
    FirstCityAttack = 1,  // battle on the city
    NewColony = 2,        // intro video of colony 2-4
    Strategic = 3,        // city, continent and army screens
    Tactic = 4,           // province battle
    PreCounterAttack = 5, // COUNTERA video
    CounterAttack = 6,    // battle on the city
    StrategicResults = 7, // a province was taken
    Won = 8,              // campaign over (OpenDay keeps the end in the state)
    Lost = 9,
};

struct PlayerRecord {
    int credits = 0;
    int tech = 0;
    int unitsDestroyed = 0, unitsLost = 0, buildingsDestroyed = 0, buildingsLost = 0;
    std::map<int, int> techLevels; // unit typeID -> level
};

struct ProvinceState {
    bool captured = false;
    int variant = 1;
};

// Where the converted databases are.
struct CampaignResources {
    const GlobalRules* rules = nullptr;
    std::function<std::filesystem::path(const std::string&)> scenario; // "s1-2-1" -> database
    std::function<std::filesystem::path(int)> colony;                    // 1-4 -> C<n> database
    std::filesystem::path tempDir;                                       // battle maps written here
};

// What a campaign battle needs to start.
struct CampaignBattle {
    std::filesystem::path database;
    std::function<void(BattleState&)> prepare;
    CombatOptions options;
    bool city = false; // fought on the player's city
    int province = 0;
    std::string title, provinceName, description, aftermath;
};

// The outcome of a campaign battle.
struct BattleOutcome {
    bool success = false;
    int techFound = 0;
    int unitsDestroyed = 0, unitsLost = 0, buildingsDestroyed = 0, buildingsLost = 0;
    const BattleState* finalState = nullptr; // city battles: the map afterwards
};

class Campaign {
public:
    // Saved state.
    std::string playerName;
    int difficulty = 4; // 0-10
    int colonyNumber = 1;
    CampaignState state = CampaignState::FirstColony;
    PlayerRecord player;
    std::vector<ProvinceState> provinces; // index = province - 1
    int battleProvince = 0;               // province of the current or last battle
    City city;
    BuiltThisPhase built;
    bool counterAttack = false;           // the battle just won has a counter-attack
    std::vector<AttackRow> pendingAttack; // its attackers

    // Loaded with the colony.
    Colony colony;
    const GlobalRules* rules = nullptr;

    static Campaign create(const CampaignResources& res, const std::string& name, int difficulty, std::mt19937& rng);

    // Loads colony `number` and its city, sets the credits, randomises the
    // province variants (campaign.md 2).
    void startColony(const CampaignResources& res, int number, std::mt19937& rng);
    // Reloads the colony data after loading a saved game.
    void reloadColony(const CampaignResources& res);

    // Difficulty multipliers (campaign.md "Terms").
    int deployMultiplier() const;
    int creditMultiplier() const;

    bool captured(int province) const;
    bool attackable(int province) const;
    int provinceCredits(int province) const; // reward as shown and paid
    std::string provinceScenario(int province) const;
    bool ownsSatellite() const { return city.owns(kCampaignPlayer, kSatellite); }
    bool ownsLaboratory() const { return city.owns(kCampaignPlayer, kLaboratory); }
    std::vector<int> deployableTypes(const GlobalRules& rules) const;
    std::vector<int> automatedTypes(const GlobalRules& rules) const;
    int techLevel(int typeId) const;
    // Deployment points of a battle on the city (campaign.md 6.4).
    int cityBudget(const GlobalRules& rules) const;

    // --- Transitions ---------------------------------------------------------
    // The intro video ended (FirstColony -> FirstCityAttack, NewColony -> Strategic).
    void introDone();
    // Strategic -> Tactic on `province`. False if it cannot be attacked.
    bool attack(int province);
    // The COUNTERA video ended.
    void counterAttackVideoDone();
    // A battle (Tactic, FirstCityAttack or CounterAttack) ended.
    void battleFinished(const GlobalRules& rules, const BattleOutcome& outcome);
    // The win view was closed. True when a new colony was started.
    bool resultsDone(const CampaignResources& res, std::mt19937& rng);
    // The battle report should end the campaign (campaign.md 7).
    bool endsCampaign(bool success) const;

    // Prepares the battle of the current state.
    CampaignBattle battle(const CampaignResources& res, std::mt19937& rng);

    // Upgrades a unit type by one level (army-tech.md 4.6). False when not
    // possible (maximum, not enough points).
    bool upgrade(const GlobalRules& rules, int typeId);
};

// Linear interpolation of the difficulty tables: E + ((H - E) * d) / 10.
int interpolate(int easy, int hard, int difficulty);
// (v * m) / 100 and the human-player share (one player: 100 %).
int scalePercent(int value, int multiplier);

// Deployment zones on the city for a battle there (campaign.md 6.2): zone
// tiles per player slot 1-4 and the AI zone (slot 0).
std::array<std::vector<Point>, kPlayerSlots> cityZones(const City& city, const GlobalRules& rules);

// Saving: OpenDay's own text format.
std::string saveCampaign(const Campaign& c, const std::string& description);
Campaign loadCampaign(const std::string& text, const CampaignResources& res, std::string* description = nullptr);
// The description line of a save file, without loading it.
std::string saveDescription(const std::string& text);

} // namespace openday::game
