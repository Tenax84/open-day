// OpenDay's own options (docs/mechanics/interface.md, "OpenDay options"): a
// dialog opened by the OPENDAY button of the in-game options, drawn with the
// art of the options dialog.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>

#include "app/app.hpp"

namespace openday::app {

// The OPENDAY button of an in-game options dialog whose panel (frame 648) is
// at `panel()`: a captioned button made from the art of BACK, between QUIT and
// BACK. It opens the OpenDay options over the dialog.
class OpenDayButton {
public:
    OpenDayButton() = default;
    // `panel`: where the in-game options dialog is (asked again when the
    // screen is resized).
    OpenDayButton(App& app, std::function<ui::Rect()> panel, int resolution);

    bool handle(const ui::InputEvent& e) { return button_.handle(e); }
    void draw(render::IndexedSurface& s, const ui::UiContext& ui) const;

private:
    ui::Button button_;
    std::uint8_t color_ = 255; // the caption
};

// The OpenDay options over the in-game options dialog at `panel()`.
std::unique_ptr<Screen> makeOpenDayOptions(App& app, int resolution, std::function<ui::Rect()> panel);

} // namespace openday::app
