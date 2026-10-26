// OpenDay's own options (docs/mechanics/interface.md, "OpenDay options").
// The dialog is the panel of the in-game options (frame 648) with its title
// re-lettered and its controls covered by one black box, like the one of the
// CD player; the settings are rows of the options' on/off lamps (665).
#include "app/openday_options.hpp"

#include <algorithm>
#include <map>

namespace openday::app {
namespace {

constexpr int kPanelFrame = 648, kBackFrame = 653, kLampFrame = 665;

// Pieces of the options panel (panel coordinates).
constexpr ui::Rect kTitleText{152, 12, 100, 23};  // "OPTIONS" in the title strip
constexpr ui::Rect kTitleBlank{88, 12, 64, 23};   // the same strip without lettering
constexpr ui::Rect kTitleStrip{84, 13, 231, 19};  // between the strip's end bars
constexpr ui::Rect kCdBox{167, 67, 220, 225};     // the CD player's box, edges included
constexpr int kCdBoxEdge = 3;
constexpr ui::Rect kContent{14, 41, 373, 289};    // the box over the options' controls
// QUIT ends and BACK starts here; the OPENDAY button is centred between them.
constexpr int kQuitRight = 250, kBackLeft = 336, kButtonY = 340, kButtonH = 17;

struct Option {
    bool Settings::*value;
    const char* label;
    const char* description;
};

constexpr Option kOptions[] = {
    {&Settings::edgeScroll, "Edge scrolling", "The map scrolls at the edge of the screen."},
    {&Settings::smoothEdgeScroll, "Smooth edge scrolling", "Pixel by pixel instead of a tile at a time."},
    {&Settings::dragScroll, "Drag the map with the right button", "Hold the right button and move the mouse."},
    {&Settings::keepViewOnSelect, "Keep the view when selecting", "Clicking a unit does not move the map to it."},
    {&Settings::exactMinimap, "Exact minimap", "A click centres the view on the exact point."},
    {&Settings::minimapDrag, "Drag on the minimap", "The view follows the held left button."},
    {&Settings::resizableWindow, "Resizable window", "The map screens grow with the window."},
};

// Fonts for re-lettering the art: bold capitals, like the captions drawn
// into it.
const render::Font& boldFont(int height) {
    static std::map<int, std::unique_ptr<render::Font>> fonts;
    std::unique_ptr<render::Font>& f = fonts[height];
    if (!f) f = std::make_unique<render::Font>(render::FontFace::Bold, height);
    return *f;
}

// Text with extra space between the letters, horizontally centred in `box`
// and vertically centred on its middle.
void drawSpaced(render::IndexedSurface& s, const render::Font& f, const ui::Rect& box, const std::string& text, int spacing,
                std::uint8_t color) {
    int total = 0;
    for (const char c : text) total += f.width(std::string(1, c)) + spacing;
    total -= spacing;
    int x = box.x + (box.w - total) / 2;
    const int y = box.y + (box.h - f.lineHeight()) / 2;
    for (const char c : text) {
        const std::string ch(1, c);
        f.draw(s, x, y, ch, color);
        x += f.width(ch) + spacing;
    }
}

// `src` resized to `dst`: corners of `edge` pixels kept, edges and middle
// repeated.
void stretch(render::IndexedSurface& s, const formats::DbiSprite& from, const ui::Rect& src, int edge, const ui::Rect& dst) {
    const int sx[3] = {src.x, src.x + edge, src.x + src.w - edge};
    const int sw[3] = {edge, src.w - 2 * edge, edge};
    const int dx[3] = {dst.x, dst.x + edge, dst.x + dst.w - edge};
    const int dw[3] = {edge, dst.w - 2 * edge, edge};
    const int sy[3] = {src.y, src.y + edge, src.y + src.h - edge};
    const int sh[3] = {edge, src.h - 2 * edge, edge};
    const int dy[3] = {dst.y, dst.y + edge, dst.y + dst.h - edge};
    const int dh[3] = {edge, dst.h - 2 * edge, edge};
    for (int j = 0; j < 3; ++j)
        for (int i = 0; i < 3; ++i) ui::drawTiled(s, from, {sx[i], sy[j], sw[i], sh[j]}, {dx[i], dy[j], dw[i], dh[j]});
}

ui::Rect offset(const ui::Rect& r, const ui::Rect& panel) { return {panel.x + r.x, panel.y + r.y, r.w, r.h}; }

// The colours of the lettering in the art, from the game palette.
std::uint8_t gold(App& app) { return nearestColor(app.data().palette, 162, 154, 103); }
std::uint8_t caption(App& app) { return nearestColor(app.data().palette, 206, 196, 148); }

const render::Font& captionFont() { return boldFont(10); }
constexpr int kCaptionSpacing = 1;

int captionWidth(const std::string& text) {
    int w = 0;
    for (const char c : text) w += captionFont().width(std::string(1, c)) + kCaptionSpacing;
    return w - kCaptionSpacing;
}

const char* const kButtonCaption = "OPENDAY";

class OpenDayOptions : public Screen {
public:
    // Over a resizable screen the dialog is resized with it and asks `panel`
    // for the options dialog's new place.
    OpenDayOptions(App& app, int resolution, std::function<ui::Rect()> panel)
        : Screen(app), resolution_(resolution), panelOf_(std::move(panel)),
          resizable_(app.top() && app.top()->resizable()), width_(app.top() ? app.top()->width() : 640),
          height_(app.top() ? app.top()->height() : 480) {
        build();
    }

    int width() const override { return resizable_ ? width_ : resolution_ == 1 ? 1024 : 640; }
    int height() const override { return resizable_ ? height_ : resolution_ == 1 ? 768 : 480; }
    bool overlay() const override { return true; }
    bool resizable() const override { return resizable_; }
    void resize(int w, int h) override {
        width_ = w;
        height_ = h;
        build();
    }

    void handle(const ui::InputEvent& e) override {
        if (back_.handle(e)) return;
        for (Row& r : rows_) {
            r.lamp.onClick = [this, &r] { flip(r); };
            if (r.lamp.handle(e)) return;
            // The label and the description switch the option too.
            if (e.type == ui::InputEvent::Type::MouseDown && e.button == 1 && r.area.contains(e.x, e.y)) return flip(r);
        }
    }

    void draw(render::IndexedSurface& s) override {
        const ui::UiContext& ui = app_.ui(resolution_);
        ui::drawFrame(s, ui, kPanelFrame, panel_);
        const ui::Rect box = offset(kContent, panel_);
        if (const formats::DbiSprite* art = ui.atlas ? ui.atlas->get(kPanelFrame) : nullptr) {
            ui::drawTiled(s, *art, kTitleBlank, offset(kTitleText, panel_));
            stretch(s, *art, kCdBox, kCdBoxEdge, box);
        } else {
            ui::drawFrame(s, ui, -1, box);
        }
        drawSpaced(s, boldFont(17), offset(kTitleStrip, panel_), "OPENDAY", 3, gold(app_));
        drawSpaced(s, boldFont(12), {box.x + 16, box.y + 10, box.w - 32, 16}, "CAMERA", 2, gold(app_));
        for (const Row& r : rows_) {
            r.lamp.draw(s, ui);
            const int x = r.lamp.rect.x + kTextX;
            ui.standard->draw(s, x, r.lamp.rect.y + 1, r.option->label, ui.white);
            ui.standard->drawWrapped(s, x, r.lamp.rect.y + kDescriptionY, descriptionWidth(box), 2 * ui.standard->lineHeight(),
                                     r.option->description, ui.grey);
        }
        back_.draw(s, ui);
    }

private:
    void build() {
        panel_ = panelOf_();
        const ui::Rect& panel = panel_;
        back_.rect = {panel.x + kBackLeft, panel.y + kButtonY, 53, kButtonH};
        back_.firstFrame = kBackFrame;
        back_.caption = "Back";
        back_.hotkey = ui::kKeyEscape;
        back_.onClick = [this] { app_.pop(); };
        const ui::Rect box = offset(kContent, panel);
        const render::Font& font = *app_.ui(resolution_).standard;
        rows_.clear();
        int y = box.y + 32;
        for (const Option& o : kOptions) {
            Row r;
            r.option = &o;
            r.lamp.rect = {box.x + 16, y, 19, 19};
            r.lamp.firstFrame = kLampFrame;
            r.lamp.toggle = true;
            r.lamp.checked = app_.settings().*o.value;
            // A row is as tall as its description (one or two lines).
            const int lines = std::clamp(static_cast<int>(font.wrap(o.description, descriptionWidth(box)).size()), 1, 2);
            const int h = kDescriptionY + lines * font.lineHeight();
            r.area = {box.x + 8, y - 3, box.w - 16, h + 4};
            rows_.push_back(r);
            y += h + kRowGap;
        }
    }

    // The name and description start kTextX right of the lamp; the
    // description kDescriptionY under the row's top; kRowGap between rows.
    static constexpr int kTextX = 30, kDescriptionY = 18, kRowGap = 4;
    static int descriptionWidth(const ui::Rect& box) { return box.w - 32 - kTextX; }

    struct Row {
        const Option* option = nullptr;
        ui::Button lamp;
        ui::Rect area;
    };

    void flip(Row& r) {
        bool& v = app_.settings().*r.option->value;
        v = !v;
        r.lamp.checked = v;
    }

    int resolution_;
    std::function<ui::Rect()> panelOf_;
    bool resizable_;
    int width_, height_;
    ui::Rect panel_;
    ui::Button back_;
    std::vector<Row> rows_;
};

} // namespace

OpenDayButton::OpenDayButton(App& app, std::function<ui::Rect()> panel, int resolution) {
    const ui::Rect p = panel();
    const int w = std::max(53, captionWidth(kButtonCaption) + 12);
    button_.rect = {p.x + (kQuitRight + kBackLeft - w) / 2, p.y + kButtonY, w, kButtonH};
    button_.firstFrame = kBackFrame;
    button_.caption = kButtonCaption;
    button_.hotkey = 'o';
    button_.onClick = [&app, panel, resolution] { app.push(makeOpenDayOptions(app, resolution, panel)); };
    color_ = caption(app);
}

// The art of BACK without its lettering: both ends kept, the plain part
// next to the left end repeated in between; then the caption.
void OpenDayButton::draw(render::IndexedSurface& s, const ui::UiContext& ui) const {
    const formats::DbiSprite* art = ui.atlas ? ui.atlas->get(button_.firstFrame + button_.imageOffset()) : nullptr;
    if (!art) return button_.draw(s, ui);
    const ui::Rect& r = button_.rect;
    constexpr int kEnd = 5, kPlain = 8;
    ui::drawTiled(s, *art, {0, 0, kEnd, r.h}, {r.x, r.y, kEnd, r.h});
    ui::drawTiled(s, *art, {kEnd, 0, kPlain, r.h}, {r.x + kEnd, r.y, r.w - 2 * kEnd, r.h});
    ui::drawTiled(s, *art, {art->width - kEnd, 0, kEnd, r.h}, {r.x + r.w - kEnd, r.y, kEnd, r.h});
    drawSpaced(s, captionFont(), r, button_.caption, kCaptionSpacing, button_.enabled ? color_ : ui.grey);
}

std::unique_ptr<Screen> makeOpenDayOptions(App& app, int resolution, std::function<ui::Rect()> panel) {
    return std::make_unique<OpenDayOptions>(app, resolution, std::move(panel));
}

} // namespace openday::app
