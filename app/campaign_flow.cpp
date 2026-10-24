// The campaign flow: new campaign menu, loading and saving, the in-game
// options of the strategic screens, and the screen for each campaign state
// (docs/mechanics/campaign.md 1, strategic-interface.md 6-8).
#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <sstream>

#include "app/campaign.hpp"
#include "app/setup.hpp"
#include "formats/pe_resources.hpp"

namespace openday::app {

// --- Helpers ------------------------------------------------------------------

std::string campaignText(App& app, int id, const char* fallback) {
    const std::string s = app.data().text(id);
    return s.empty() ? fallback : s;
}

void fillBox(render::IndexedSurface& s, const ui::Rect& r, std::uint8_t color) {
    for (int y = std::max(0, r.y); y < std::min(s.height(), r.y + r.h); ++y)
        for (int x = std::max(0, r.x); x < std::min(s.width(), r.x + r.w); ++x) s.row(y)[x] = color;
}

void frameBox(render::IndexedSurface& s, const ui::Rect& r, std::uint8_t color) {
    for (int x = r.x; x < r.x + r.w; ++x)
        for (const int y : {r.y, r.y + r.h - 1})
            if (x >= 0 && y >= 0 && x < s.width() && y < s.height()) s.row(y)[x] = color;
    for (int y = r.y; y < r.y + r.h; ++y)
        for (const int x : {r.x, r.x + r.w - 1})
            if (x >= 0 && y >= 0 && x < s.width() && y < s.height()) s.row(y)[x] = color;
}

ui::Button makeButton(ui::Rect r, int frame, const std::string& caption, int key, std::function<void()> action, bool toggle) {
    ui::Button b;
    b.rect = r;
    b.firstFrame = frame;
    b.caption = caption;
    b.hotkey = key;
    b.onClick = std::move(action);
    b.toggle = toggle;
    return b;
}

void drawNumber(render::IndexedSurface& s, const ui::UiContext& ui, const ui::Rect& r, int value) {
    fillBox(s, r, ui.black);
    ui.standard->drawIn(s, r.x, r.y + 1, r.w, r.h, std::to_string(std::clamp(value, 0, 99999)), ui.white);
}

game::CampaignResources campaignResources(GameData& data) {
    game::CampaignResources res;
    res.rules = &data.rules;
    GameData* d = &data;
    res.scenario = [d](const std::string& name) { return d->scenario(name); };
    res.colony = [d](int n) {
        const auto it = d->colonies.find(n);
        return it == d->colonies.end() ? fs::path() : it->second;
    };
    res.tempDir = fs::temp_directory_path() / "openday";
    return res;
}

bool campaignAvailable(const GameData& data) {
    return data.colonies.size() == 4 && !data.scenarios.empty() && data.scenarios.count("s1-1-1");
}

namespace {

constexpr int kSaveSlots = 10;

ui::Rect centred(int w, int h) { return {(640 - w) / 2, (480 - h) / 2, w, h}; }

fs::path savesDir() {
    if (const char* env = std::getenv("OPENDAY_SAVE_DIR")) return fs::path(env);
    return setup::userDir() / "saves";
}

fs::path saveFile(int slot) {
    char name[32];
    std::snprintf(name, sizeof name, "campaign.%02d", slot);
    return savesDir() / name;
}

std::string readFile(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return f ? std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>()) : std::string();
}

// --- New campaign (strategic-interface.md 6) ---------------------------------------

class NewCampaignMenu : public Screen {
public:
    explicit NewCampaignMenu(App& app) : Screen(app), panel_(centred(286, 228)) {
        name_.rect = {panel_.x + 15, panel_.y + 55, 252, 21};
        name_.maxLength = 16;
        name_.text = campaignText(app, 739, "Liberator");
        buttons_.push_back(makeButton({panel_.x + 17, panel_.y + 114, 19, 17}, 180, "<", ui::kKeyLeft, [this] {
            if (difficulty_ > 0) --difficulty_;
        }));
        buttons_.push_back(makeButton({panel_.x + 250, panel_.y + 114, 19, 17}, 176, ">", ui::kKeyRight, [this] {
            if (difficulty_ < 10) ++difficulty_;
        }));
        buttons_.push_back(makeButton({panel_.x + 10, panel_.y + 184, 100, 24}, 940, "Back", ui::kKeyEscape, [this] { app_.pop(); }));
        buttons_.push_back(makeButton({panel_.x + 170, panel_.y + 184, 100, 24}, 944, "Continue", ui::kKeyEnter, [this] { start(); }));
    }

    void handle(const ui::InputEvent& e) override {
        for (ui::Button& b : buttons_)
            if (b.handle(e)) return;
        name_.handle(e);
    }

    void draw(render::IndexedSurface& s) override {
        const ui::UiContext& ui = app_.ui();
        s.fill(ui.black);
        ui::drawFrame(s, ui, 939, panel_);
        name_.draw(s, ui);
        ui::drawFrame(s, ui, 1047, {panel_.x + 39, panel_.y + 114, 208, 17});
        ui::drawFrame(s, ui, 677, {panel_.x + 39 + difficulty_ * 208 / 11, panel_.y + 114, 19, 17});
        for (const ui::Button& b : buttons_) b.draw(s, ui);
    }

private:
    void start() {
        std::string name = name_.text;
        while (!name.empty() && name.back() == ' ') name.pop_back();
        while (!name.empty() && name.front() == ' ') name.erase(name.begin());
        if (name.empty()) return app_.sound("INVALIDE.WAV");
        auto ctx = std::make_shared<CampaignContext>();
        ctx->res = campaignResources(app_.data());
        try {
            ctx->campaign = game::Campaign::create(ctx->res, name, difficulty_, ctx->rng);
        } catch (const std::exception& ex) {
            std::fprintf(stderr, "cannot start the campaign: %s\n", ex.what());
            return app_.sound("INVALIDE.WAV");
        }
        App& app = app_;
        app_.reset(makeWait(app_, 2, [&app, ctx] { return campaignScreen(app, ctx); }));
    }

    ui::Rect panel_;
    ui::EditBox name_;
    std::vector<ui::Button> buttons_;
    int difficulty_ = 4;
};

// --- Load / save (interface.md 4, panels 739 / 750) -------------------------------

class SlotMenu : public Screen {
public:
    SlotMenu(App& app, CampaignPtr ctx) : Screen(app), ctx_(std::move(ctx)), panel_(centred(286, 419)) {
        saving_ = ctx_ != nullptr;
        for (int i = 0; i < kSaveSlots; ++i) {
            const std::string text = readFile(saveFile(i + 1));
            descriptions_.push_back(game::saveDescription(text));
        }
        buttons_.push_back(makeButton({panel_.x + 10, panel_.y + 383, 100, 24}, saving_ ? 751 : 740, "Back", ui::kKeyEscape,
                                      [this] { app_.pop(); }));
        buttons_.push_back(makeButton({panel_.x + 176, panel_.y + 383, 100, 24}, saving_ ? 755 : 744, saving_ ? "Save" : "Load",
                                      ui::kKeyEnter, [this] { act(); }));
    }

    void handle(const ui::InputEvent& e) override {
        for (ui::Button& b : buttons_)
            if (b.handle(e)) return;
        if (e.type == ui::InputEvent::Type::MouseDown && e.button == 1)
            for (int i = 0; i < kSaveSlots; ++i)
                if (slotRect(i).contains(e.x, e.y)) {
                    selected_ = i;
                    if (saving_ && descriptions_[static_cast<std::size_t>(i)].empty())
                        descriptions_[static_cast<std::size_t>(i)] = defaultDescription();
                }
        if (e.type == ui::InputEvent::Type::KeyDown && (e.key == ui::kKeyUp || e.key == ui::kKeyDown))
            selected_ = std::clamp(selected_ + (e.key == ui::kKeyDown ? 1 : -1), 0, kSaveSlots - 1);
    }

    void draw(render::IndexedSurface& s) override {
        const ui::UiContext& ui = app_.ui();
        s.fill(ui.black);
        ui::drawFrame(s, ui, saving_ ? 750 : 739, panel_);
        for (int i = 0; i < kSaveSlots; ++i) {
            const ui::Rect r = slotRect(i);
            fillBox(s, r, ui.black);
            if (i == selected_) frameBox(s, r, ui.white);
            ui.standard->drawIn(s, r.x + 4, r.y + 2, r.w - 8, r.h - 2, descriptions_[static_cast<std::size_t>(i)], ui.white,
                                render::Align::Left);
        }
        for (const ui::Button& b : buttons_) b.draw(s, ui);
        if (!message_.empty()) ui.standard->drawIn(s, 0, 460, 640, 18, message_, ui.white);
    }

private:
    ui::Rect slotRect(int i) const { return {panel_.x + 52, panel_.y + 40 + i * 32 - (i >= 6 ? 0 : 0), 217, 21}; }

    std::string defaultDescription() const {
        const game::Campaign& c = ctx_->campaign;
        return c.playerName + " - " + c.colony.name;
    }

    void act() {
        if (selected_ < 0) return;
        const fs::path file = saveFile(selected_ + 1);
        if (saving_) {
            try {
                fs::create_directories(file.parent_path());
                std::ofstream f(file, std::ios::binary);
                f << game::saveCampaign(ctx_->campaign, descriptions_[static_cast<std::size_t>(selected_)]);
                if (!f) throw std::runtime_error("write failed");
                app_.pop();
            } catch (const std::exception&) {
                message_ = campaignText(app_, 817, "Save failed");
                app_.sound("INVALIDE.WAV");
            }
            return;
        }
        const std::string text = readFile(file);
        if (text.empty()) return app_.sound("INVALIDE.WAV");
        auto ctx = std::make_shared<CampaignContext>();
        ctx->res = campaignResources(app_.data());
        try {
            ctx->campaign = game::loadCampaign(text, ctx->res);
        } catch (const std::exception& ex) {
            message_ = ex.what();
            app_.sound("INVALIDE.WAV");
            return;
        }
        ctx->continueShown = true;
        App& app = app_;
        app_.reset(makeWait(app_, 3, [&app, ctx] { return campaignScreen(app, ctx); }));
    }

    CampaignPtr ctx_;
    bool saving_ = false;
    ui::Rect panel_;
    std::vector<std::string> descriptions_;
    std::vector<ui::Button> buttons_;
    int selected_ = -1;
    std::string message_;
};

// --- In-game options of the strategic screens (interface.md 4) ---------------------

class CampaignOptions : public Screen {
public:
    CampaignOptions(App& app, CampaignPtr ctx, int resolution) : Screen(app), ctx_(std::move(ctx)), resolution_(resolution) {
        const int w = width(), h = height();
        p_ = {(w - 136 - 403) / 2, (h - 32 - 371) / 2, 403, 371};
        buttons_.push_back(makeButton({p_.x + 336, p_.y + 340, 53, 17}, 653, "Back", ui::kKeyEscape, [this] { app_.pop(); }));
        buttons_.push_back(makeButton({p_.x + 197, p_.y + 340, 53, 17}, 649, "Quit", 'q', [this] { app_.reset(makeMainMenu(app_)); }));
        buttons_.push_back(makeButton({p_.x + 132, p_.y + 340, 53, 17}, 661, "Save", 's', [this] {
            app_.push(makeSaveMenu(app_, ctx_));
        }));
        ui::Button restart = makeButton({p_.x + 10, p_.y + 340, 110, 17}, 689, "Restart", 0, nullptr);
        restart.enabled = false;
        buttons_.push_back(restart);
        auto group = [&](ui::RadioGroup& g, int y, int n, int checked, std::function<void(int)> change) {
            for (int i = 0; i < n; ++i) g.items.push_back(makeButton({p_.x + 36, p_.y + y + 21 * i, 19, 19}, 665, "", 0, nullptr, true));
            g.check(checked);
            g.onChange = std::move(change);
        };
        group(sound_, 71, 2, app.settings().sound ? 0 : 1, [this](int i) { app_.settings().sound = i == 0; });
        group(anim_, 158, 2, app.settings().moveAnimations ? 0 : 1, [this](int i) { app_.settings().moveAnimations = i == 0; });
        group(scroll_, 252, 3, app.settings().scrollSpeed, [this](int i) { app_.settings().scrollSpeed = i; });
    }

    int width() const override { return resolution_ == 1 ? 1024 : 640; }
    int height() const override { return resolution_ == 1 ? 768 : 480; }

    void handle(const ui::InputEvent& e) override {
        for (ui::Button& b : buttons_)
            if (b.handle(e)) return;
        if (sound_.handle(e) || anim_.handle(e)) return;
        scroll_.handle(e);
    }

    void draw(render::IndexedSurface& s) override {
        const ui::UiContext& ui = app_.ui(resolution_);
        s.fill(ui.black);
        ui::drawFrame(s, ui, 648, p_);
        for (const ui::Button& b : buttons_) b.draw(s, ui);
        sound_.draw(s, ui);
        anim_.draw(s, ui);
        scroll_.draw(s, ui);
    }

private:
    CampaignPtr ctx_;
    int resolution_ = 0;
    ui::Rect p_;
    std::vector<ui::Button> buttons_;
    ui::RadioGroup sound_, anim_, scroll_;
};

// --- Battles --------------------------------------------------------------------------

std::unique_ptr<Screen> campaignBattle(App& app, CampaignPtr ctx) {
    game::Campaign& c = ctx->campaign;
    game::CampaignBattle b;
    try {
        b = c.battle(ctx->res, ctx->rng);
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "cannot prepare the battle: %s\n", ex.what());
        return makeMainMenu(app);
    }
    BattleConfig config;
    config.entry.title = b.title;
    config.entry.province = b.provinceName;
    config.entry.description = b.description;
    config.entry.database = b.database;
    config.prepare = b.prepare;
    config.options = b.options;
    config.aftermath = b.aftermath;
    config.techLine = !b.city;
    config.onFinished = [&app, ctx](const BattleResult& r) {
        game::Campaign& cc = ctx->campaign;
        game::BattleOutcome o;
        o.success = r.success;
        o.techFound = r.techFound;
        o.unitsDestroyed = r.unitsDestroyed;
        o.unitsLost = r.unitsLost;
        o.buildingsDestroyed = r.buildingsDestroyed;
        o.buildingsLost = r.buildingsLost;
        o.finalState = r.finalState;
        const bool ends = cc.endsCampaign(r.success);
        cc.battleFinished(app.data().rules, o);
        if (ends) {
            // campaign.md 7: the final video, then the main menu.
            app.reset(makeVideo(app, r.success ? "FH2END" : "FH2LOSE",
                                [&app] { app.reset(makeWait(app, 1, [&app] { return makeMainMenu(app); })); }));
            return;
        }
        app.reset(makeWait(app, 1, [&app, ctx] { return campaignScreen(app, ctx); }));
    };
    return makeBattle(app, std::move(config));
}

} // namespace

// --- The screen of each state ------------------------------------------------------------

std::unique_ptr<Screen> campaignScreen(App& app, CampaignPtr ctx) {
    game::Campaign& c = ctx->campaign;
    using S = game::CampaignState;
    switch (c.state) {
    case S::FirstColony:
    case S::NewColony: {
        // The colony's intro, then loading picture 7 (base camp attacked) or 8-10.
        const int wait = c.state == S::FirstColony ? 7 : 6 + c.colonyNumber;
        const std::string video = c.colony.intro.empty() ? "INTRO" + std::to_string(c.colonyNumber) : c.colony.intro;
        return makeVideo(app, video, [&app, ctx, wait] {
            app.reset(makeWait(app, wait, [&app, ctx] {
                ctx->campaign.introDone();
                ctx->continueShown = false;
                return campaignScreen(app, ctx);
            }));
        });
    }
    case S::FirstCityAttack:
    case S::Tactic:
    case S::CounterAttack:
        return campaignBattle(app, ctx);
    case S::PreCounterAttack:
        return makeVideo(app, "COUNTERA", [&app, ctx] {
            app.reset(makeWait(app, 6, [&app, ctx] {
                ctx->campaign.counterAttackVideoDone();
                return campaignScreen(app, ctx);
            }));
        });
    case S::StrategicResults:
        return makeContinentScreen(app, ctx, ContinentMode::Win);
    case S::Strategic:
        return makeCityScreen(app, ctx);
    case S::Won:
    case S::Lost:
    default:
        return makeMainMenu(app);
    }
}

void campaignContinue(App& app, CampaignPtr ctx) { app.reset(campaignScreen(app, std::move(ctx))); }

std::unique_ptr<Screen> makeNewCampaignMenu(App& app) { return std::make_unique<NewCampaignMenu>(app); }
std::unique_ptr<Screen> makeLoadMenu(App& app) { return std::make_unique<SlotMenu>(app, nullptr); }
std::unique_ptr<Screen> makeSaveMenu(App& app, CampaignPtr ctx) { return std::make_unique<SlotMenu>(app, std::move(ctx)); }
std::unique_ptr<Screen> makeCampaignOptions(App& app, CampaignPtr ctx, int resolution) {
    return std::make_unique<CampaignOptions>(app, std::move(ctx), resolution);
}

} // namespace openday::app
