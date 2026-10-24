// Atlas elements used by the screens: frame, width, height
// (docs/formats/INTERFACE-SMK.md, docs/mechanics/tactical-interface.md).
#include "app/app.hpp"

namespace openday::app {
namespace {

// A control with `count` consecutive frames of the same size.
void add(std::vector<ui::AtlasElement>& out, int first, int count, int w, int h) {
    for (int i = 0; i < count; ++i) out.push_back({first + i, w, h});
}

} // namespace

std::vector<ui::AtlasElement> atlasElements(int resolution) {
    std::vector<ui::AtlasElement> e;
    const bool large = resolution == 1;
    // Main menu.
    add(e, 710, 1, 286, 281);
    for (const int f : {711, 715, 719, 723, 731, 735, 765}) add(e, f, 4, 256, 28);
    // Resolution.
    add(e, 777, 1, 286, 228);
    add(e, 778, 5, 256, 28);
    add(e, 783, 5, 256, 28);
    add(e, 799, 4, 100, 24);
    add(e, 755, 4, 100, 24);
    // Skirmish (single player).
    add(e, 1046, 1, 364, 416);
    add(e, 842, 4, 100, 24);
    add(e, 846, 4, 100, 24);

    // Battle: side panels and the message strip depend on the resolution.
    for (const int panel : {0, 226, 310}) add(e, panel, 1, large ? 184 : 136, large ? 768 : 480);
    add(e, 498, 1, large ? 840 : 504, large ? 40 : 32);
    // Deployment view.
    for (const int f : {5, 9, 13, 17}) add(e, f, 4, 53, 17);
    add(e, 21, 4, 110, 17);
    add(e, 25, 4, 44, 43);
    add(e, 29, 4, 110, 17);
    // Waiting view: player lamps (six frames per colour), MAP, GOAL, emblem.
    add(e, 46, 30, 19, 19);
    add(e, 315, 4, 53, 17);
    add(e, 319, 4, 53, 17);
    add(e, 323, 4, 44, 43);
    // Tactical view.
    add(e, 231, 4, 53, 17);  // MAP
    add(e, 235, 4, 53, 17);  // GOAL
    add(e, 241, 6, 53, 17);  // GROUP
    add(e, 247, 4, 110, 17); // MOVE ALL
    add(e, 251, 6, 19, 19);  // overwatch lamp
    add(e, 258, 6, 110, 17); // FIRE
    add(e, 264, 6, 19, 19);  // auto-fire lamp
    add(e, 270, 4, 19, 17);  // previous
    add(e, 274, 4, 19, 17);  // next
    add(e, 278, 4, 53, 17);  // GUARD
    add(e, 282, 4, 110, 17); // REINFORCEMENT
    add(e, 286, 4, 110, 17); // AUTOMATED UNITS
    add(e, 290, 4, 53, 17);  // END TURN
    add(e, 294, 4, 44, 43);  // emblem
    add(e, 298, 4, 53, 17);  // CENTER
    // Message strip buttons.
    add(e, 499, 4, 53, 17); // CHAT
    add(e, 693, 4, 53, 17); // MENU
    // Objectives dialog.
    add(e, 633, 1, 448, 268);
    add(e, 634, 4, 110, 17);
    add(e, 638, 6, 19, 19);
    // Battle report.
    add(e, 365, 1, 448, 480);
    add(e, 366, 4, 110, 17);
    // In-game options.
    add(e, 648, 1, 403, 371);
    for (const int f : {649, 653, 661}) add(e, f, 4, 53, 17);
    add(e, 665, 6, 19, 19);
    add(e, 689, 4, 110, 17);
    return e;
}

} // namespace openday::app
