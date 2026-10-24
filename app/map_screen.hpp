// The map screens: the shared frame (docs/mechanics/tactical-interface.md
// 7.1) and its two variants, the tactical map screen (7.2) and the city map
// screen of the campaign (strategic-interface.md 4.2).
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "app/app.hpp"
#include "formats/bmp_palette.hpp"
#include "info/overview.hpp"

namespace openday::game {
class BattleState;
}

namespace openday::app {

struct CampaignContext;

// One item of the player selector.
struct MapScreenItem {
    int color = 0; // 0 alien (red), 1 blue, 2 yellow, 3 purple, 4 green
    int owner = 0; // player slot
    std::string name;
};

// One object dot: a filled square of side b + 1 + extra at the tile.
struct MapScreenDot {
    game::Point tile;
    int extra = 0; // +1 buildings, 0 roads and walls, -1 units
    game::Rgb color;
    int owner = 0;
    int type = -1; // the list type it blinks with; -1 = never
};

struct MapScreenConfig {
    int resolution = 0;
    int panelFrame = 41, emblemFrame = 88;
    int closeFrame = 84;
    ui::Rect closeRect{10, 458, 53, 17}; // relative to the panel
    std::string closeCaption = "Back";
    int blinkMs = 175;
    bool updatesBelow = false;              // the owning view keeps running
    std::function<bool()> clickable;        // empty: always clickable
    formats::BmpImage picture;              // the overview picture (may be empty)
    int mapSize = 1;
    std::vector<MapScreenItem> items;
    int initialItem = 0;
    std::function<int()> resource;                                  // the indicator
    std::function<std::vector<game::OverviewRow>(int owner)> rows; // list of the selected owner
    std::function<std::string(int type)> typeName;
    std::function<void(std::vector<MapScreenDot>&)> dots; // in drawing order
    // Markers: drawn once into the picture, and every frame (the bulbs).
    std::function<void(render::IndexedSurface& picture, double scale, int block)> staticMarkers;
    std::function<void(render::IndexedSurface& target, int ox, int oy, double scale, int phase)> markers;
    // The owning view's visible part: top-left tile and size in tiles.
    std::function<void(game::Point& topLeft, int& span)> viewport;
    std::function<void(game::Point tile)> centre;
    std::function<void(int type)> encyclopedia; // the selected row's type, -1 = none
    std::function<void()> onClose;
};

std::unique_ptr<Screen> makeMapScreen(App& app, MapScreenConfig config);

// The scenario's overview picture (Scenario Info.mapScreen640 / 1024).
formats::BmpImage loadOverviewPicture(const fs::path& scenario, int resolution);

// The tactical map screen (7.2) over a battle view. `first`: opened by the
// first deployment (CONTINUE instead of BACK).
struct TacticalMapConfig {
    const game::BattleState* state = nullptr;
    int localPlayer = 1;
    bool first = false;
    int resolution = 0;
    fs::path scenario; // the scenario database (overview picture)
    std::function<std::string(int slot)> playerName;
    std::function<bool()> clickable;
    std::function<void(game::Point& topLeft, int& span)> viewport;
    std::function<void(game::Point tile)> centre;
    std::function<void()> onClose;
};
std::unique_ptr<Screen> makeTacticalMap(App& app, TacticalMapConfig config);

// The city map screen (4.2) over the city screen.
struct CityMapConfig {
    std::shared_ptr<CampaignContext> ctx;
    int resolution = 0;
    std::function<void(game::Point& topLeft, int& span)> viewport;
    std::function<void(game::Point tile)> centre;
};
std::unique_ptr<Screen> makeCityMap(App& app, CityMapConfig config);

} // namespace openday::app
