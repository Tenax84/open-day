// Front-end screens: intro video, main menu, resolution, skirmish list,
// loading picture (docs/mechanics/interface.md sections 3-4).
#include <algorithm>
#include <fstream>
#include <iterator>

#include "app/app.hpp"
#include "app/screens.hpp"
#include "formats/smk.hpp"

namespace openday::app {
namespace {

// Main menu, panel frame 710 (286 x 281), centred at 640 x 480.
constexpr int kMenuW = 286, kMenuH = 281;

ui::Rect centred(int w, int h) { return {(640 - w) / 2, (480 - h) / 2, w, h}; }

void clear(render::IndexedSurface& s, std::uint8_t color) { s.fill(color); }

// ---------------------------------------------------------------------------

class IntroScreen : public Screen {
public:
    explicit IntroScreen(App& app) : Screen(app) {
        const fs::path path = app.data().gameDir / "VIDEO" / "FH2INTRO.SMK";
        std::ifstream f(path, std::ios::binary);
        if (!f) return;
        std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        try {
            video_ = std::make_unique<formats::SmkVideo>(std::move(bytes));
            if (video_->audioTrack(0).present) app.audio().play(video_->decodeAudio(0));
            next();
        } catch (const formats::SmkError&) {
            video_.reset();
        }
    }

    void handle(const ui::InputEvent& e) override {
        if (e.type == ui::InputEvent::Type::KeyDown || e.type == ui::InputEvent::Type::MouseUp) done();
    }

    void update(int ms) override {
        if (!video_) return done();
        clock_ += ms * 1000;
        while (video_ && clock_ >= static_cast<long long>(video_->frameMicros())) {
            clock_ -= video_->frameMicros();
            if (!next()) return done();
        }
    }

    void draw(render::IndexedSurface& s) override {
        clear(s, 0);
        if (!video_ || video_->currentFrame() < 0) return;
        const int w = video_->width(), h = video_->height();
        const int x0 = (640 - w) / 2, y0 = (480 - h) / 2; // centred, black bars
        for (int y = 0; y < h && y0 + y < s.height(); ++y)
            std::copy_n(video_->frame().begin() + static_cast<std::ptrdiff_t>(y) * w, std::min(w, s.width() - x0),
                        s.row(y0 + y) + x0);
    }

    const formats::Palette* palette() const override { return video_ ? &palette_ : nullptr; }

private:
    bool next() {
        if (!video_->nextFrame()) return false;
        palette_ = video_->palette();
        palette_[0] = {0, 0, 0}; // keep the bars black
        return true;
    }
    void done() {
        if (finished_) return;
        finished_ = true;
        app_.audio().stopSounds(); // cut the video's sound
        app_.replace(makeMainMenu(app_));
    }

    std::unique_ptr<formats::SmkVideo> video_;
    formats::Palette palette_{};
    long long clock_ = 0;
    bool finished_ = false;
};

// ---------------------------------------------------------------------------

class MainMenu : public Screen {
public:
    explicit MainMenu(App& app) : Screen(app), panel_(centred(kMenuW, kMenuH)) {
        struct Item {
            int frame;
            const char* caption;
            int key;
            std::function<void()> action;
            bool enabled;
        };
        const Item items[] = {
            {711, "New campaign", 0, nullptr, false},
            {715, "Load campaign", 0, nullptr, false},
            {765, "Skirmish", 0, [this] { app_.push(makeSkirmishMenu(app_)); }, true},
            {719, "Multi player", 0, nullptr, false},
            {723, "Intro", 0, [this] { app_.replace(makeIntro(app_)); }, true},
            {731, "Resolution", 0, [this] { app_.push(makeResolutionMenu(app_)); }, true},
            {735, "Quit", ui::kKeyEscape, [this] { app_.quit(); }, true},
        };
        int y = 40;
        for (const Item& it : items) {
            ui::Button b;
            b.rect = {panel_.x + 15, panel_.y + y, 256, 28};
            b.firstFrame = it.frame;
            b.caption = it.caption;
            b.hotkey = it.key;
            b.onClick = it.action;
            b.enabled = it.enabled; // not implemented yet in OpenDay
            buttons_.push_back(std::move(b));
            y += 30;
        }
    }

    void handle(const ui::InputEvent& e) override {
        for (ui::Button& b : buttons_)
            if (b.handle(e)) return;
    }

    void draw(render::IndexedSurface& s) override {
        clear(s, app_.ui().black);
        ui::drawFrame(s, app_.ui(), 710, panel_);
        for (const ui::Button& b : buttons_) b.draw(s, app_.ui());
        app_.ui().standard->draw(s, 4, 480 - 18, "OpenDay " OPENDAY_VERSION " - not affiliated with the original publisher",
                                 app_.ui().grey);
    }

private:
    ui::Rect panel_;
    std::vector<ui::Button> buttons_;
};

// ---------------------------------------------------------------------------

class ResolutionMenu : public Screen {
public:
    explicit ResolutionMenu(App& app) : Screen(app), panel_(centred(286, 228)) {
        for (int i = 0; i < 2; ++i) {
            ui::Button item;
            item.rect = {panel_.x + 15, panel_.y + 50 + 40 * i, 256, 28};
            item.firstFrame = i == 0 ? 778 : 783;
            item.caption = i == 0 ? "Low (640x480)" : "High (1024x768)";
            group_.items.push_back(item);
        }
        group_.check(app.settings().resolution);
        back_.rect = {panel_.x + 10, panel_.y + 180, 100, 24};
        back_.firstFrame = 799;
        back_.caption = "Back";
        back_.hotkey = ui::kKeyEscape;
        back_.onClick = [this] { app_.pop(); };
        save_.rect = {panel_.x + 176, panel_.y + 180, 100, 24};
        save_.firstFrame = 755;
        save_.caption = "Save";
        save_.hotkey = ui::kKeyEnter;
        save_.onClick = [this] {
            app_.settings().resolution = std::max(0, group_.checked());
            app_.pop();
        };
    }

    void handle(const ui::InputEvent& e) override {
        if (group_.handle(e) || back_.handle(e)) return;
        save_.handle(e);
    }

    void draw(render::IndexedSurface& s) override {
        clear(s, app_.ui().black);
        ui::drawFrame(s, app_.ui(), 777, panel_);
        group_.draw(s, app_.ui());
        back_.draw(s, app_.ui());
        save_.draw(s, app_.ui());
    }

private:
    ui::Rect panel_;
    ui::RadioGroup group_;
    ui::Button back_, save_;
};

// ---------------------------------------------------------------------------

// Skirmish, single player: panel 1046 (364 x 416).
class SkirmishMenu : public Screen {
public:
    explicit SkirmishMenu(App& app) : Screen(app), panel_(centred(364, 416)) {
        list_.rect = {panel_.x + 16, panel_.y + 50, 330, 134};
        list_.rowHeight = 17;
        for (const ScenarioEntry& e : app.data().skirmish) list_.rows.push_back(e.title + " - " + e.province);
        list_.onSelect = [this](int i) { description_.text = app_.data().skirmish[static_cast<std::size_t>(i)].description; };
        description_.rect = {panel_.x + 16, panel_.y + 218, 330, 118};
        description_.largeFont = true;
        if (!list_.rows.empty()) {
            list_.selected = 0;
            list_.onSelect(0);
        }
        back_.rect = {panel_.x + 12, panel_.y + 375, 100, 24};
        back_.firstFrame = 846;
        back_.caption = "Back";
        back_.hotkey = ui::kKeyEscape;
        back_.onClick = [this] { app_.pop(); };
        start_.rect = {panel_.x + 252, panel_.y + 375, 100, 24};
        start_.firstFrame = 842;
        start_.caption = "Continue";
        start_.hotkey = ui::kKeyEnter;
        start_.onClick = [this] {
            if (list_.selected < 0) return;
            app_.push(makeBriefing(app_, app_.data().skirmish[static_cast<std::size_t>(list_.selected)]));
        };
    }

    void handle(const ui::InputEvent& e) override {
        if (list_.handle(e) || description_.handle(e) || back_.handle(e)) return;
        start_.handle(e);
    }

    void draw(render::IndexedSurface& s) override {
        clear(s, app_.ui().black);
        ui::drawFrame(s, app_.ui(), 1046, panel_);
        list_.draw(s, app_.ui());
        description_.draw(s, app_.ui());
        back_.draw(s, app_.ui());
        start_.draw(s, app_.ui());
    }

private:
    ui::Rect panel_;
    ui::ListBox list_;
    ui::TextBox description_;
    ui::Button back_, start_;
};

// ---------------------------------------------------------------------------

// A full-screen loading picture (WAITING.SMK frame id - 1); runs `next`
// after the first frame was shown.
class WaitScreen : public Screen {
public:
    WaitScreen(App& app, int id, std::function<std::unique_ptr<Screen>()> next)
        : Screen(app), next_(std::move(next)) {
        std::ifstream f(app.data().gameDir / "INTERF" / "WAITING.SMK", std::ios::binary);
        if (!f) return;
        std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        try {
            formats::SmkVideo v(std::move(bytes));
            while (v.currentFrame() < id - 1 && v.nextFrame()) {
            }
            if (v.currentFrame() == id - 1) {
                picture_ = v.frame();
                w_ = v.width();
                h_ = v.height();
            }
        } catch (const formats::SmkError&) {
        }
    }

    void handle(const ui::InputEvent&) override {}
    void update(int) override {
        if (shown_ && next_) {
            auto make = std::move(next_);
            next_ = nullptr;
            app_.replace(make());
        }
    }
    void draw(render::IndexedSurface& s) override {
        clear(s, 0);
        for (int y = 0; y < h_ && y < s.height(); ++y)
            std::copy_n(picture_.begin() + static_cast<std::ptrdiff_t>(y) * w_, std::min(w_, s.width()), s.row(y));
        shown_ = true;
    }

private:
    std::function<std::unique_ptr<Screen>()> next_;
    std::vector<std::uint8_t> picture_;
    int w_ = 0, h_ = 0;
    bool shown_ = false;
};

} // namespace

std::unique_ptr<Screen> makeIntro(App& app) { return std::make_unique<IntroScreen>(app); }
std::unique_ptr<Screen> makeMainMenu(App& app) { return std::make_unique<MainMenu>(app); }
std::unique_ptr<Screen> makeResolutionMenu(App& app) { return std::make_unique<ResolutionMenu>(app); }
std::unique_ptr<Screen> makeSkirmishMenu(App& app) { return std::make_unique<SkirmishMenu>(app); }
std::unique_ptr<Screen> makeWait(App& app, int id, std::function<std::unique_ptr<Screen>()> next) {
    return std::make_unique<WaitScreen>(app, id, std::move(next));
}

} // namespace openday::app
