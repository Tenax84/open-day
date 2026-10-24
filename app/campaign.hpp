// The campaign screens and their shared state (docs/mechanics/campaign.md,
// strategic-interface.md, city-building.md).
#pragma once

#include <memory>
#include <random>

#include "app/screens.hpp"
#include "campaign/campaign.hpp"
#include "render/smk_frames.hpp"

namespace openday::app {

struct CampaignContext {
    game::Campaign campaign;
    game::CampaignResources res;
    std::mt19937 rng{std::random_device{}()};
    bool continueShown = false; // the continue-mode continent screen of a new colony was shown
    // The decoded colony map animation (COLANMS\C<n>.SMK) of colony `mapColony`.
    std::shared_ptr<render::SmkFrames> colonyMap;
    int mapColony = 0;
};
using CampaignPtr = std::shared_ptr<CampaignContext>;

// Resources of the converted databases; empty colony paths when the
// campaign data is missing.
game::CampaignResources campaignResources(GameData& data);
bool campaignAvailable(const GameData& data);

// The screen for the campaign's current state (campaign.md 1.2).
std::unique_ptr<Screen> campaignScreen(App& app, CampaignPtr ctx);
// Replaces every screen with the one for the current state.
void campaignContinue(App& app, CampaignPtr ctx);

enum class ContinentMode { Normal, Continue, Win };
std::unique_ptr<Screen> makeCityScreen(App& app, CampaignPtr ctx);
std::unique_ptr<Screen> makeContinentScreen(App& app, CampaignPtr ctx, ContinentMode mode);
std::unique_ptr<Screen> makeIntelDialog(App& app, CampaignPtr ctx, int province);
void openCampaignArmy(App& app, CampaignPtr ctx);
std::unique_ptr<Screen> makeSaveMenu(App& app, CampaignPtr ctx);
// The in-game options of the strategic screens (with SAVE).
std::unique_ptr<Screen> makeCampaignOptions(App& app, CampaignPtr ctx, int resolution);

// Small shared helpers of the campaign screens.
std::string campaignText(App& app, int id, const char* fallback);
void fillBox(render::IndexedSurface& s, const ui::Rect& r, std::uint8_t color);
void frameBox(render::IndexedSurface& s, const ui::Rect& r, std::uint8_t color);
ui::Button makeButton(ui::Rect r, int frame, const std::string& caption, int key, std::function<void()> action,
                      bool toggle = false);
// A number indicator: clamped to 0 ... 99999, centred, white on black.
void drawNumber(render::IndexedSurface& s, const ui::UiContext& ui, const ui::Rect& r, int value);

} // namespace openday::app
