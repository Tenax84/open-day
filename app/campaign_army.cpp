// The army screen: unit types and automated units with their tech levels,
// and upgrades with tech points (docs/mechanics/army-tech.md 4).
#include <algorithm>

#include "app/screens.hpp"
#include "formats/pe_resources.hpp"

namespace openday::app {
namespace {

constexpr int kUnitColumns = 4, kUnitRows = 6, kAutoColumns = 2, kAutoRows = 2;
constexpr int kCellW = 135, kCellH = 43, kAutoPitch = 51;

std::string text(App& app, int id, const char* fallback) {
    const std::string s = app.data().text(id);
    return s.empty() ? fallback : s;
}

void fillRect(render::IndexedSurface& s, const ui::Rect& r, std::uint8_t color) {
    for (int y = std::max(0, r.y); y < std::min(s.height(), r.y + r.h); ++y)
        for (int x = std::max(0, r.x); x < std::min(s.width(), r.x + r.w); ++x) s.row(y)[x] = color;
}

void frameRect(render::IndexedSurface& s, const ui::Rect& r, std::uint8_t color) {
    for (int x = r.x; x < r.x + r.w; ++x)
        for (const int y : {r.y, r.y + r.h - 1})
            if (x >= 0 && y >= 0 && x < s.width() && y < s.height()) s.row(y)[x] = color;
    for (int y = r.y; y < r.y + r.h; ++y)
        for (const int x : {r.x, r.x + r.w - 1})
            if (x >= 0 && y >= 0 && x < s.width() && y < s.height()) s.row(y)[x] = color;
}

// Draws a sprite; with `stipple` every second pixel (checkerboard) is index 0.
void blitIcon(render::IndexedSurface& s, const formats::DbiSprite& sp, int x0, int y0, bool stipple) {
    for (int y = 0; y < sp.height; ++y) {
        const int ty = y0 + y;
        if (ty < 0 || ty >= s.height()) continue;
        for (int x = 0; x < sp.width; ++x) {
            const int tx = x0 + x;
            if (tx < 0 || tx >= s.width()) continue;
            const std::size_t i = static_cast<std::size_t>(y * sp.width + x);
            if (!sp.opaque[i]) continue;
            s.row(ty)[tx] = stipple && ((x + y) % 2 == 1) ? 0 : sp.pixels[i];
        }
    }
}

ui::Button button(ui::Rect r, int frame, const std::string& caption, int key, std::function<void()> action) {
    ui::Button b;
    b.rect = r;
    b.firstFrame = frame;
    b.caption = caption;
    b.hotkey = key;
    b.onClick = std::move(action);
    return b;
}

class ArmyScreen : public Screen {
public:
    ArmyScreen(App& app, ArmyConfig config) : Screen(app), c_(std::move(config)) {
        large_ = c_.resolution == 1;
        const int w = width(), h = height();
        origin_ = {(w - 588) / 2, h < 768 ? 0 : (h - 451) / 2};
        red_ = nearestColor(app.data().palette, 255, 0, 0);
        upgrades_ = c_.variant != ArmyConfig::Variant::Deployment && c_.canUpgrade;
        auto at = [&](int x, int y, int bw, int bh) { return ui::Rect{origin_.x + x, origin_.y + y, bw, bh}; };
        if (upgrades_) buttons_.push_back(button(at(471, 400, 110, 17), 162, "Upgrade", 't', [this] { upgrade(); }));
        if (c_.variant == ArmyConfig::Variant::Skirmish)
            buttons_.push_back(button(at(471, 428, 110, 17), 158, "Continue", ui::kKeyEnter, [this] { close(); }));
        else
            buttons_.push_back(button(at(471, 428, 110, 17), 168, "Back", ui::kKeyEscape, [this] { close(); }));
        ui::Button ency = button(at(496, 309, 60, 55), 172, "", ui::kKeyF1, nullptr);
        ency.enabled = false; // no encyclopedia yet
        buttons_.push_back(ency);
        auto arrows = [&](int x, int y, bool units) {
            buttons_.push_back(button(at(x, y, 19, 17), 180, "<", 0, [this, units] { step(units, -1); }));
            buttons_.push_back(button(at(x + 22, y, 19, 17), 188, "^", 0, [this, units] { scroll(units, -1); }));
            buttons_.push_back(button(at(x + 44, y, 19, 17), 184, "v", 0, [this, units] { scroll(units, 1); }));
            buttons_.push_back(button(at(x + 66, y, 19, 17), 176, ">", 0, [this, units] { step(units, 1); }));
        };
        arrows(247, 280, true);
        arrows(185, 426, false);
        unitSel_ = c_.units.empty() ? -1 : 0;
        autoSel_ = c_.automated.empty() ? -1 : 0;
        updateUpgradeButton();
    }

    int width() const override { return large_ ? 1024 : 640; }
    int height() const override { return large_ ? 768 : 480; }

    void handle(const ui::InputEvent& e) override {
        for (ui::Button& b : buttons_)
            if (b.handle(e)) return;
        if (e.type == ui::InputEvent::Type::KeyDown) {
            switch (e.key) {
            case ui::kKeyLeft: step(activeUnits_, -1); return;
            case ui::kKeyRight: step(activeUnits_, 1); return;
            case ui::kKeyUp: scroll(activeUnits_, -1); return;
            case ui::kKeyDown: scroll(activeUnits_, 1); return;
            default: return;
            }
        }
        if (e.type != ui::InputEvent::Type::MouseDown || e.button != 1) return;
        const ui::Rect units{origin_.x + 8, origin_.y + 10, kUnitColumns * kCellW, kUnitRows * kCellH};
        if (units.contains(e.x, e.y)) {
            activeUnits_ = true;
            const int i = (unitTop_ + (e.y - units.y) / kCellH) * kUnitColumns + (e.x - units.x) / kCellW;
            if (i < static_cast<int>(c_.units.size())) unitSel_ = i;
            updateUpgradeButton();
            return;
        }
        const ui::Rect autos{origin_.x + 10, origin_.y + 315, kAutoColumns * kCellW, kAutoRows * kAutoPitch};
        if (autos.contains(e.x, e.y)) {
            activeUnits_ = false;
            const int i = (autoTop_ + (e.y - autos.y) / kAutoPitch) * kAutoColumns + (e.x - autos.x) / kCellW;
            if (i < static_cast<int>(c_.automated.size())) autoSel_ = i;
        }
    }

    void draw(render::IndexedSurface& s) override {
        const ui::UiContext& ui = app_.ui(c_.resolution);
        s.fill(ui.black);
        ui::drawFrame(s, ui, 157, {origin_.x, origin_.y, 588, 451});
        drawUnits(s, ui);
        drawAutomated(s, ui);
        if (upgrades_) drawInfo(s, ui);
        const ui::Rect ind{origin_.x + 474, origin_.y + 371, 84, 17};
        fillRect(s, ind, ui.black);
        ui.standard->drawIn(s, ind.x, ind.y + 1, ind.w, ind.h, std::to_string(std::clamp(available(), 0, 99999)), ui.white);
        for (const ui::Button& b : buttons_) b.draw(s, ui);
    }

private:
    int level(int type) const {
        if (!c_.levels) return 0;
        const auto it = c_.levels->find(type);
        return it == c_.levels->end() ? 0 : it->second;
    }
    const game::UnitType* row(int type, int lvl) const {
        const auto it = app_.data().rules.units.find({type, lvl});
        return it == app_.data().rules.units.end() ? nullptr : &it->second;
    }
    int available() const { return c_.techPoints ? *c_.techPoints : 0; }

    void step(bool units, int d) {
        activeUnits_ = units;
        int& sel = units ? unitSel_ : autoSel_;
        int& top = units ? unitTop_ : autoTop_;
        const int count = static_cast<int>(units ? c_.units.size() : c_.automated.size());
        const int cols = units ? kUnitColumns : kAutoColumns, rows = units ? kUnitRows : kAutoRows;
        if (count == 0) return;
        sel = std::clamp(sel + d, 0, count - 1);
        if (sel / cols < top) top = sel / cols;
        if (sel / cols >= top + rows) top = sel / cols - rows + 1;
        updateUpgradeButton();
    }

    void scroll(bool units, int d) {
        activeUnits_ = units;
        int& top = units ? unitTop_ : autoTop_;
        const int count = static_cast<int>(units ? c_.units.size() : c_.automated.size());
        const int cols = units ? kUnitColumns : kAutoColumns, rows = units ? kUnitRows : kAutoRows;
        const int maxTop = std::max(0, (count + cols - 1) / cols - rows);
        top = std::clamp(top + d, 0, maxTop);
    }

    void updateUpgradeButton() {
        if (!upgrades_ || buttons_.empty()) return;
        const game::UnitType* r = unitSel_ >= 0 ? row(c_.units[static_cast<std::size_t>(unitSel_)], level(c_.units[static_cast<std::size_t>(unitSel_)])) : nullptr;
        buttons_[0].enabled = r && r->costToNextTL > 0;
    }

    void upgrade() {
        if (unitSel_ < 0 || !c_.levels || !c_.techPoints) return;
        const int type = c_.units[static_cast<std::size_t>(unitSel_)];
        const int lvl = level(type);
        const game::UnitType* cur = row(type, lvl);
        if (!cur || cur->costToNextTL <= 0 || !row(type, lvl + 1)) return;
        if (available() < cur->costToNextTL) {
            message_ = text(app_, 849, "Not enough technical resources to upgrade this unit");
            app_.sound("INVALIDE.WAV");
            return;
        }
        *c_.techPoints -= cur->costToNextTL;
        (*c_.levels)[type] = lvl + 1;
        message_.clear();
        app_.sound("CASHOUT.WAV");
        updateUpgradeButton();
    }

    void close() {
        const int type = unitSel_ >= 0 ? c_.units[static_cast<std::size_t>(unitSel_)] : -1;
        auto done = c_.onClose;
        app_.pop();
        if (done) done(type);
    }

    void drawUnits(render::IndexedSurface& s, const ui::UiContext& ui) {
        const render::Font& f = *ui.standard;
        const int x0 = origin_.x + 8, y0 = origin_.y + 10;
        fillRect(s, {x0, y0, kUnitColumns * kCellW, kUnitRows * kCellH}, 0);
        for (int r = 0; r < kUnitRows; ++r)
            for (int col = 0; col < kUnitColumns; ++col) {
                const int i = (unitTop_ + r) * kUnitColumns + col;
                if (i >= static_cast<int>(c_.units.size())) break;
                const int type = c_.units[static_cast<std::size_t>(i)];
                const game::UnitType* cur = row(type, level(type));
                if (!cur) continue;
                const int x = x0 + col * kCellW, y = y0 + r * kCellH;
                const bool max = cur->costToNextTL < 1;
                if (const formats::DbiSprite* icon = app_.data().sprites.get(game::SpriteSet::ListBox, cur->image + "999"))
                    blitIcon(s, *icon, x, y + 2, max);
                const int lh = f.lineHeight();
                f.drawIn(s, x + 40, y, kCellW - 40, lh, cur->name, ui.white, render::Align::Left);
                if (max) {
                    f.drawIn(s, x + 40, y + lh, kCellW - 40, lh, text(app_, 848, "Maximum level"), ui.white, render::Align::Left);
                } else {
                    f.drawIn(s, x + 40, y + lh, kCellW - 40, lh, text(app_, 851, "Next tech:") + " " + std::to_string(cur->techLevel + 2),
                             ui.white, render::Align::Left);
                    f.drawIn(s, x + 40, y + 2 * lh, kCellW - 40, lh, text(app_, 852, "Cost:") + " " + std::to_string(cur->costToNextTL),
                             ui.white, render::Align::Left);
                }
                if (i == unitSel_) frameRect(s, {x, y, kCellW, kCellH - 1}, 255);
            }
    }

    void drawAutomated(render::IndexedSurface& s, const ui::UiContext& ui) {
        const render::Font& f = *ui.standard;
        const int x0 = origin_.x + 10, y0 = origin_.y + 315;
        fillRect(s, {x0, y0, kAutoColumns * kCellW, kAutoRows * kAutoPitch}, 0);
        for (int r = 0; r < kAutoRows; ++r)
            for (int col = 0; col < kAutoColumns; ++col) {
                const int i = (autoTop_ + r) * kAutoColumns + col;
                if (i >= static_cast<int>(c_.automated.size())) break;
                const auto it = app_.data().rules.automated.find(c_.automated[static_cast<std::size_t>(i)]);
                if (it == app_.data().rules.automated.end()) continue;
                const int x = x0 + col * kCellW, y = y0 + r * kAutoPitch;
                if (const formats::DbiSprite* icon = app_.data().sprites.get(game::SpriteSet::ListBox, it->second.image + "999"))
                    blitIcon(s, *icon, x, y + 2, true);
                f.drawIn(s, x + 40, y, kCellW - 40, f.lineHeight(), it->second.name, ui.white, render::Align::Left);
                f.drawIn(s, x + 40, y + f.lineHeight(), kCellW - 40, f.lineHeight(), text(app_, 848, "Maximum level"), ui.white,
                         render::Align::Left);
                if (i == autoSel_) frameRect(s, {x, y, kCellW, kCellH - 1}, 255);
            }
    }

    // The next level's statistics; values the next upgrade changes in red (4.5).
    void drawInfo(render::IndexedSurface& s, const ui::UiContext& ui) {
        const render::Font& f = *ui.standard;
        const ui::Rect box{origin_.x + 300, origin_.y + 315, 159, 124};
        fillRect(s, box, 0);
        int y = box.y;
        auto line = [&](const std::string& t, std::uint8_t color) {
            f.drawIn(s, box.x, y, box.w, f.lineHeight(), t, color, render::Align::Left);
            y += f.lineHeight();
        };
        if (!message_.empty()) {
            f.drawWrapped(s, box.x, box.y, box.w, box.h, message_, ui.white);
            return;
        }
        if (!activeUnits_ && autoSel_ >= 0) {
            const auto it = app_.data().rules.automated.find(c_.automated[static_cast<std::size_t>(autoSel_)]);
            if (it == app_.data().rules.automated.end()) return;
            line(it->second.name, ui.white);
            line(text(app_, 848, "Maximum level"), ui.white);
            return;
        }
        if (unitSel_ < 0) return;
        const int type = c_.units[static_cast<std::size_t>(unitSel_)];
        const game::UnitType* cur = row(type, level(type));
        if (!cur) return;
        const game::UnitType* next = row(type, level(type) + 1);
        if (!next || cur->costToNextTL <= 0) next = cur;
        line(cur->name, ui.white);
        if (cur->costToNextTL <= 0) line(text(app_, 848, "Maximum level"), ui.white);
        auto stat = [&](int label, const char* fallback, int now, int then, bool changed) {
            f.drawIn(s, box.x, y, 110, f.lineHeight(), text(app_, label, fallback), ui.white, render::Align::Left);
            f.drawIn(s, box.x + 110, y, box.w - 110, f.lineHeight(), std::to_string(then), changed ? red_ : ui.white,
                     render::Align::Left);
            (void)now;
            y += f.lineHeight();
        };
        stat(24, "ARMOR", cur->armor, next->armor, cur->armor != next->armor);
        stat(25, "ACTION POINTS", cur->actionPoints, next->actionPoints, cur->actionPoints != next->actionPoints);
        const int avgCur = cur->damageBase + cur->damageRandom / 2, avgNext = next->damageBase + next->damageRandom / 2;
        stat(27, "AVG DAMAGE", avgCur, avgNext, avgCur != avgNext);
        if (cur->isAA) stat(235, "DAMAGE AA", cur->damageAA, next->damageAA, cur->damageAA != next->damageAA);
        stat(29, "RANGE", cur->range, next->range, cur->range != next->range);
        const int rofCur = cur->shotAp > 0 ? cur->actionPoints / cur->shotAp : 0;
        const int rofNext = next->shotAp > 0 ? next->actionPoints / next->shotAp : 0;
        stat(28, "ROF", rofCur, rofNext, cur->shotAp != next->shotAp);
    }

    ArmyConfig c_;
    bool large_ = false;
    bool upgrades_ = true;
    ui::Rect origin_;
    std::uint8_t red_ = 249;
    std::vector<ui::Button> buttons_;
    int unitSel_ = -1, autoSel_ = -1, unitTop_ = 0, autoTop_ = 0;
    bool activeUnits_ = true;
    std::string message_;
};

} // namespace

std::unique_ptr<Screen> makeArmy(App& app, ArmyConfig config) { return std::make_unique<ArmyScreen>(app, std::move(config)); }

} // namespace openday::app
