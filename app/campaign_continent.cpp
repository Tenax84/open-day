// The continent screen with the colony map, the win view after a taken
// province, and the intelligence dialog (docs/mechanics/strategic-interface.md 1-3).
#include <algorithm>
#include <cstdio>

#include "app/campaign.hpp"
#include "app/view_layout.hpp"
#include "formats/bmp_palette.hpp"
#include "formats/pe_resources.hpp"

namespace openday::app {
namespace {

// Splits a description at "##".
std::vector<std::string> fields(const std::string& d) {
    std::vector<std::string> out;
    for (std::size_t pos = 0;;) {
        const std::size_t sep = d.find("##", pos);
        out.push_back(d.substr(pos, sep == std::string::npos ? std::string::npos : sep - pos));
        if (sep == std::string::npos) break;
        pos = sep + 2;
    }
    return out;
}

// The decoded colony animation, kept in the context.
const render::SmkFrames* colonyMap(App& app, CampaignContext& ctx) {
    const game::Colony& col = ctx.campaign.colony;
    if (ctx.colonyMap && ctx.mapColony == col.number) return ctx.colonyMap.get();
    ctx.colonyMap.reset();
    try {
        const fs::path p = app.data().file("COLANMS/" + game::toUpper(col.animation) + ".SMK");
        ctx.colonyMap = std::make_shared<render::SmkFrames>(render::SmkFrames::load(p));
        ctx.mapColony = col.number;
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "colony map: %s\n", ex.what());
    }
    return ctx.colonyMap.get();
}

class ContinentScreen : public Screen {
public:
    ContinentScreen(App& app, CampaignPtr ctx, ContinentMode mode) : Screen(app), ctx_(std::move(ctx)), mode_(mode) {
        large_ = app.settings().resolution == 1;
        map_ = colonyMap(app, *ctx_);
        if (mode_ == ContinentMode::Win) {
            hint_ = campaignText(app, 883, "You are victorious. Hit the continue button.");
            selected_ = ctx_->campaign.battleProvince;
            blink_ = 10;
            app.sound("PROVINCE.WAV");
        } else if (mode_ == ContinentMode::Normal) {
            hint_ = campaignText(app, 884, "Choose the province you wish to attack.");
        } else {
            hint_ = campaignText(app, 882, "Click the continue button.");
        }
        setLayout(ViewLayout::standard(large_));
        buildOwnedLayer();
    }

    int width() const override { return width_; }
    int height() const override { return height_; }
    bool resizable() const override { return true; }
    void resize(int w, int h) override { setLayout(ViewLayout(w, h, large_)); }

    void handle(const ui::InputEvent& e) override {
        for (ui::Button& b : buttons_)
            if (b.handle(e)) return;
        if (mode_ == ContinentMode::Win || e.type != ui::InputEvent::Type::MouseDown) return;
        if (e.x >= viewW_ || e.y >= viewH_) return;
        const int p = provinceAt(e.x - ox_, e.y - oy_);
        if (p <= 0) return;
        selected_ = p;
        if (attack_ >= 0) buttons_[static_cast<std::size_t>(attack_)].enabled = ctx_->campaign.attackable(p);
        app_.sound("SELPROV.WAV");
    }

    void update(int ms) override {
        if (!map_ || map_->count() == 0) return;
        clock_ += ms * 1000;
        const int step = std::max(1, map_->frameMicros());
        while (clock_ >= step) {
            clock_ -= step;
            ++tick_;
            // Blinking: every 4th frame (strategic-interface.md 2.5).
            if (blink_ > 0 && tick_ % 4 == 0) --blink_;
        }
    }

    void draw(render::IndexedSurface& s) override {
        const ui::UiContext& ui = app_.ui(large_ ? 1 : 0);
        s.fill(ui.black);
        drawMap(s);
        drawSidePanel(app_, s, 192, layout_);
        const game::Campaign& c = ctx_->campaign;
        drawNumber(s, ui, {viewW_ + 63, 26, 38, 14}, c.cityBudget(app_.data().rules));
        drawNumber(s, ui, {viewW_ + 63, 56, 38, 14}, c.player.credits);
        drawNumber(s, ui, {viewW_ + 63, 86, 38, 14}, c.player.tech);
        drawText(s, ui);
        drawMessageStrip(app_, s, layout_);
        for (const ui::Button& b : buttons_) b.draw(s, ui);
    }

private:
    // The map centred in the view; the controls on the panel and the strip.
    void setLayout(const ViewLayout& l) {
        layout_ = l;
        width_ = l.width;
        height_ = l.height;
        viewW_ = l.viewW;
        viewH_ = l.viewH;
        if (map_) {
            ox_ = (viewW_ - map_->width()) / 2;
            oy_ = (viewH_ - map_->height()) / 2;
        }
        const int px = viewW_;
        auto at = [px](int x, int y, int w, int h) { return ui::Rect{px + x, y, w, h}; };
        buttons_.clear();
        attack_ = -1;
        if (mode_ == ContinentMode::Win) {
            buttons_.push_back(makeButton(at(11, 454, 110, 17), 21, "Continue", ui::kKeyEnter, [this] { closeWin(); }));
        } else {
            buttons_.push_back(makeButton(at(11, 387, 110, 17), 198, "Intelligence", 'i', [this] {
                if (selected_ > 0) app_.push(makeIntelDialog(app_, ctx_, selected_));
            }));
            if (mode_ == ContinentMode::Normal) {
                attack_ = static_cast<int>(buttons_.size());
                buttons_.push_back(makeButton(at(11, 407, 110, 17), 202, "Attack", 'a', [this] { attack(); }));
                buttons_.back().enabled = selected_ > 0 && ctx_->campaign.attackable(selected_);
                buttons_.push_back(makeButton(at(11, 456, 110, 17), 210, "Back", ui::kKeyEscape, [this] { app_.pop(); }));
            } else {
                buttons_.push_back(makeButton(at(11, 456, 110, 17), 21, "Continue", ui::kKeyEscape, [this] { app_.pop(); }));
            }
        }
        // The emblem: the encyclopedia's default page with the colony's
        // building list as the context (encyclopedia.md 3).
        buttons_.push_back(makeButton(at(5, 0, 44, 43), 214, "", ui::kKeyF1, [this] {
            EncyclopediaRequest r;
            r.levels = ctx_->campaign.player.techLevels;
            r.context = ctx_->campaign.colony.buildingsAvailable;
            openEncyclopedia(app_, std::move(r));
        }));
        buttons_.push_back(makeButton({viewW_ - 60, viewH_ + 8, 53, 17}, 693, "Menu", ui::kKeyF2, [this] {
            app_.push(makeCampaignOptions(app_, ctx_, large_ ? 1 : 0));
        }));
    }

    int background() const {
        const game::Colony& col = ctx_->campaign.colony;
        const int frames = std::max(1, col.animationFrames);
        return col.animationFirst + tick_ % frames;
    }
    int selectionFrame(int p) const {
        const game::Colony& col = ctx_->campaign.colony;
        return col.animationFirst + col.animationFrames - 2 + 2 * p;
    }

    // The owned layer: the green frame of every captured province.
    void buildOwnedLayer() {
        if (!map_) return;
        owned_.assign(static_cast<std::size_t>(map_->width() * map_->height()), 0);
        for (const game::Province& p : ctx_->campaign.colony.provinces) {
            if (!ctx_->campaign.captured(p.number)) continue;
            const int f = selectionFrame(p.number) + 1;
            if (f < 0 || f >= map_->count()) continue;
            const std::vector<std::uint8_t>& px = map_->frame(f);
            for (std::size_t i = 0; i < px.size(); ++i)
                if (px[i] != 0) owned_[i] = px[i];
        }
    }

    // Province under a picture position (masks of the selection frames).
    int provinceAt(int x, int y) const {
        if (!map_ || x < 0 || y < 0 || x >= map_->width() || y >= map_->height()) return 0;
        for (const game::Province& p : ctx_->campaign.colony.provinces) {
            if (ctx_->campaign.captured(p.number)) continue;
            const int f = selectionFrame(p.number);
            if (f < 0 || f >= map_->count()) continue;
            if (map_->frame(f)[static_cast<std::size_t>(y * map_->width() + x)] != 0) return p.number;
        }
        return 0;
    }

    void drawMap(render::IndexedSurface& s) {
        if (!map_ || map_->count() == 0) return;
        const int w = map_->width();
        const std::vector<std::uint8_t>& bg = map_->frame(std::min(background(), map_->count() - 1));
        const std::vector<std::uint8_t>* sel = nullptr;
        if (selected_ > 0) {
            // The win view alternates green and blue while blinking.
            const int f = selectionFrame(selected_) + (blink_ > 0 && blink_ % 2 == 1 ? 1 : 0);
            if (f >= 0 && f < map_->count()) sel = &map_->frame(f);
        }
        for (int y = 0; y < viewH_; ++y) {
            const int py = y - oy_;
            if (py < 0 || py >= map_->height()) continue;
            std::uint8_t* row = s.row(y);
            for (int x = 0; x < viewW_; ++x) {
                const int pxl = x - ox_;
                if (pxl < 0 || pxl >= w) continue;
                const std::size_t i = static_cast<std::size_t>(py * w + pxl);
                std::uint8_t c = bg[i];
                if (owned_[i]) c = owned_[i];
                if (sel && (*sel)[i]) c = (*sel)[i];
                row[x] = c;
            }
        }
    }

    void drawText(render::IndexedSurface& s, const ui::UiContext& ui) {
        const ui::Rect box{viewW_ + 10, 150, 118, 230};
        const render::Font& f = *ui.standard;
        if (selected_ <= 0 || mode_ == ContinentMode::Win) {
            f.drawWrapped(s, box.x, box.y, box.w, box.h, hint_, ui.white);
            return;
        }
        fillBox(s, box, 0);
        const game::Province* p = ctx_->campaign.colony.province(selected_);
        if (!p) return;
        std::string t = p->name + "\n";
        if (ctx_->campaign.attackable(selected_)) {
            const std::vector<std::string> d = fields(p->description);
            t += d[0];
            if (d.size() > 1) t += "\n" + d[1];
            t += "\n\n" + formats::formatMessage(campaignText(app_, 865, "Credits: %1!d!"),
                                                {std::to_string(ctx_->campaign.provinceCredits(selected_))});
        } else {
            t += campaignText(app_, 802, "Province is too far.");
        }
        f.drawWrapped(s, box.x, box.y, box.w, box.h, t, ui.white);
    }

    void attack() {
        if (selected_ <= 0 || !ctx_->campaign.attack(selected_)) return;
        CampaignPtr ctx = ctx_;
        App& app = app_;
        app_.reset(makeWait(app_, 12, [&app, ctx] { return campaignScreen(app, ctx); }));
    }

    void closeWin() {
        CampaignPtr ctx = ctx_;
        App& app = app_;
        app_.reset(makeWait(app_, 11, [&app, ctx] {
            if (ctx->campaign.resultsDone(ctx->res, ctx->rng)) ctx->continueShown = false;
            return campaignScreen(app, ctx);
        }));
    }

    CampaignPtr ctx_;
    ContinentMode mode_;
    bool large_ = false;
    ViewLayout layout_;
    int width_ = 640, height_ = 480, viewW_ = 504, viewH_ = 448;
    const render::SmkFrames* map_ = nullptr;
    int ox_ = 0, oy_ = 0;
    std::vector<std::uint8_t> owned_;
    std::vector<ui::Button> buttons_;
    int attack_ = -1;
    std::string hint_;
    int selected_ = 0;
    int blink_ = 0;
    long long clock_ = 0;
    int tick_ = 0;
};

// --- Intelligence (strategic-interface.md 3) ------------------------------------------

class IntelDialog : public Screen {
public:
    IntelDialog(App& app, CampaignPtr ctx, int province) : Screen(app), ctx_(std::move(ctx)), province_(province) {
        large_ = app.settings().resolution == 1;
        back_ = makeButton({40 + 364, 40 + 250, 110, 17}, 558, "Back", ui::kKeyEscape, [this] { app_.pop(); });
        const game::Campaign& c = ctx_->campaign;
        uplink_ = c.ownsSatellite();
        if (const game::Province* p = c.colony.province(province)) {
            const int v = std::clamp(c.provinces[static_cast<std::size_t>(province - 1)].variant, 1, 4);
            const std::vector<std::uint8_t>& bmp = p->mapScreen[static_cast<std::size_t>(v - 1)];
            try {
                if (!bmp.empty()) picture_ = formats::readBmpImage(bmp);
            } catch (const std::exception&) {
            }
        }
    }

    int width() const override { return large_ ? 1024 : 640; }
    int height() const override { return large_ ? 768 : 480; }

    void handle(const ui::InputEvent& e) override { back_.handle(e); }

    void draw(render::IndexedSurface& s) override {
        const ui::UiContext& ui = app_.ui(large_ ? 1 : 0);
        s.fill(ui.black);
        ui::drawFrame(s, ui, 557, {40, 40, 513, 311});
        const game::Province* p = ctx_->campaign.colony.province(province_);
        const render::Font& f = *ui.standard;
        if (uplink_) {
            fillBox(s, {50, 50, 200, 200}, 0);
            const int w = picture_.width;
            const int x0 = 50 + (200 - w) / 2, y0 = 50 + (200 - w) / 2;
            for (int y = 0; y < picture_.height; ++y)
                std::copy_n(picture_.pixels.begin() + static_cast<std::ptrdiff_t>(y) * w, w, s.row(y0 + y) + x0);
        }
        if (p) {
            f.drawIn(s, 260, 60, 273, f.lineHeight(), p->title, ui.white);
            f.draw(s, 260, 90, p->name, ui.white);
            if (!uplink_) {
                f.drawWrapped(s, 60, 150, 473, 171,
                              campaignText(app_, 803, "You must build a Satellite Uplink to see more information."), ui.white);
            } else {
                const std::vector<std::string> d = fields(p->description);
                std::string t = d.size() > 1 ? d[1] : std::string();
                if (d.size() > 2) t += "\n" + d[2];
                int y = 120;
                auto block = [&](const std::string& text) {
                    const std::vector<std::string> lines = f.wrap(text, 273);
                    for (const std::string& line : lines) {
                        if (y + f.lineHeight() > 330) break;
                        f.draw(s, 260, y, line, ui.white);
                        y += f.lineHeight();
                    }
                    y += 20;
                };
                block(t);
                block(p->objective);
                f.draw(s, 260, y, std::to_string(p->tech), ui.white);
            }
        }
        back_.draw(s, ui);
    }

private:
    CampaignPtr ctx_;
    int province_;
    bool large_ = false;
    bool uplink_ = false;
    formats::BmpImage picture_;
    ui::Button back_;
};

} // namespace

std::unique_ptr<Screen> makeContinentScreen(App& app, CampaignPtr ctx, ContinentMode mode) {
    return std::make_unique<ContinentScreen>(app, std::move(ctx), mode);
}

std::unique_ptr<Screen> makeIntelDialog(App& app, CampaignPtr ctx, int province) {
    return std::make_unique<IntelDialog>(app, std::move(ctx), province);
}

} // namespace openday::app
