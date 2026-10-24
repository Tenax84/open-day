// The encyclopedia dialog (docs/mechanics/encyclopedia.md): the picture of
// the current entry, its text and statistics, ◀ / ▶ and the race and
// category lamps.
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>

#include "app/campaign.hpp"
#include "app/screens.hpp"
#include "info/encyclopedia.hpp"

namespace openday::app {
namespace {

using game::EncyclopediaCategory;
using game::EncyclopediaPage;

constexpr int kLabelX = 210, kValueX = 330, kMaxX = 470, kRight = 530;

// One line of text clipped to a rectangle (the font itself does not clip).
void drawClipped(render::IndexedSurface& s, const render::Font& f, const ui::Rect& clip, int x, int y, const std::string& text,
                 std::uint8_t color) {
    if (clip.w <= 0 || clip.h <= 0 || text.empty()) return;
    render::IndexedSurface scratch(clip.w, clip.h, 0);
    f.draw(scratch, x - clip.x, y - clip.y, text, 1);
    for (int yy = 0; yy < clip.h; ++yy) {
        const int ty = clip.y + yy;
        if (ty < 0 || ty >= s.height()) continue;
        for (int xx = 0; xx < clip.w; ++xx) {
            const int tx = clip.x + xx;
            if (tx >= 0 && tx < s.width() && scratch.at(xx, yy) == 1) s.row(ty)[tx] = color;
        }
    }
}

// The hot key of a lamp or arrow: the first character of its string.
int hotkey(App& app, int id, char fallback) {
    const std::string s = app.data().text(id);
    const unsigned char c = s.empty() ? static_cast<unsigned char>(fallback) : static_cast<unsigned char>(s[0]);
    return std::tolower(c);
}

class EncyclopediaScreen : public Screen {
public:
    EncyclopediaScreen(App& app, EncyclopediaRequest request)
        : Screen(app), request_(std::move(request)), data_(app.encyclopedia()),
          nav_(data_, [levels = request_.levels](int type) {
              const auto it = levels.find(type);
              return it == levels.end() ? 0 : it->second;
          }) {
        Screen* below = app.top();
        width_ = below ? below->width() : 640;
        height_ = below ? below->height() : 480;
        resolution_ = width_ >= 1024 ? 1 : 0;
        pictures_ = app.encyclopediaPictures();
        if (pictures_) {
            // The pictures' own palette, mapped to the game palette (encyclopedia.md, open question).
            for (std::size_t i = 0; i < remap_.size(); ++i) {
                const formats::Rgb c = pictures_->palette()[i];
                remap_[i] = nearestColor(app.data().palette, c.r, c.g, c.b);
            }
        }
        yellow_ = nearestColor(app.data().palette, 232, 200, 40);
        red_ = nearestColor(app.data().palette, 220, 0, 0);
        darkRed_ = nearestColor(app.data().palette, 110, 0, 0);
        grey_ = nearestColor(app.data().palette, 70, 70, 70);

        auto at = [](int x, int y, int w, int h) { return ui::Rect{kX + x, kY + y, w, h}; };
        buttons_.push_back(makeButton(at(22, 382, 53, 17), 383, "Back", ui::kKeyEscape, [this] { app_.pop(); }));
        buttons_.push_back(makeButton(at(22, 350, 19, 17), 387, "<", hotkey(app, 101, 'p'), [this] { nav_.previous(); }));
        buttons_.push_back(makeButton(at(56, 350, 19, 17), 391, ">", hotkey(app, 102, ' '), [this] { nav_.next(); }));
        race_.items.push_back(makeButton(at(6, 15, 19, 19), 395, "", hotkey(app, 118, 'h'), nullptr, true));
        race_.items.push_back(makeButton(at(6, 62, 19, 19), 395, "", hotkey(app, 119, 'a'), nullptr, true));
        race_.onChange = [this](int i) { nav_.setAlien(i == 1); };
        category_.items.push_back(makeButton(at(104, 366, 19, 19), 395, "", hotkey(app, 120, 'u'), nullptr, true));
        category_.items.push_back(makeButton(at(104, 390, 19, 19), 395, "", hotkey(app, 121, 'x'), nullptr, true));
        category_.items.push_back(makeButton(at(355, 366, 19, 19), 395, "", hotkey(app, 114, 'b'), nullptr, true));
        category_.items.push_back(makeButton(at(355, 390, 19, 19), 395, "", hotkey(app, 115, 'd'), nullptr, true));
        category_.onChange = [this](int i) { nav_.setCategory(static_cast<EncyclopediaCategory>(i)); };

        using K = EncyclopediaRequest::Kind;
        switch (request_.kind) {
        case K::Default: nav_.openDefault(); break;
        case K::Unit: nav_.openUnit(request_.type); break;
        case K::Automated: nav_.openAutomated(request_.type); break;
        case K::Building: nav_.openBuilding(request_.type); break;
        case K::Wall: nav_.openWall(request_.type); break;
        case K::Mine: nav_.openMine(request_.type); break;
        }
    }

    int width() const override { return width_; }
    int height() const override { return height_; }
    bool overlay() const override { return true; }

    void handle(const ui::InputEvent& e) override {
        for (ui::Button& b : buttons_)
            if (b.handle(e)) return;
        if (race_.handle(e)) return;
        category_.handle(e);
    }

    void draw(render::IndexedSurface& s) override {
        const ui::UiContext& ui = app_.ui(resolution_);
        // The lamps show the navigator's state (a step may change the page).
        race_.check(nav_.alien() ? 1 : 0);
        category_.check(static_cast<int>(nav_.category()));
        ui::drawFrame(s, ui, 382, {kX, kY, 540, 415});
        drawPicture(s);
        for (const ui::Button& b : buttons_) b.draw(s, ui);
        race_.draw(s, ui);
        category_.draw(s, ui);
        if (nav_.entry() < 0) return;
        switch (nav_.page()) {
        case EncyclopediaPage::Unit: drawUnit(s, ui); break;
        case EncyclopediaPage::Automated: drawAutomated(s, ui); break;
        case EncyclopediaPage::Building: drawBuilding(s, ui); break;
        case EncyclopediaPage::Wall: drawWall(s, ui); break;
        case EncyclopediaPage::Mine: drawMine(s, ui); break;
        }
    }

private:
    static constexpr int kX = 40, kY = 40; // fixed screen position (1)

    std::string text(int id, const char* fallback) const { return campaignText(app_, id, fallback); }

    int frameOfEntry() const {
        const auto i = static_cast<std::size_t>(std::max(0, nav_.entry()));
        switch (nav_.page()) {
        case EncyclopediaPage::Unit: {
            const game::EncyclopediaUnit* u = nav_.displayedUnit(nav_.entry());
            return u ? u->frame : -1;
        }
        case EncyclopediaPage::Automated: return data_.automated[i].frame;
        case EncyclopediaPage::Building: return data_.buildings[i].frame;
        case EncyclopediaPage::Wall: return data_.walls[i].frame;
        case EncyclopediaPage::Mine: return data_.mines[i].frame;
        }
        return -1;
    }

    void drawPicture(render::IndexedSurface& s) {
        const int frame = nav_.entry() < 0 ? -1 : frameOfEntry();
        if (!pictures_ || frame < 0 || frame >= pictures_->count()) return;
        const std::vector<std::uint8_t>& px = pictures_->frame(frame);
        const int w = pictures_->width(), h = pictures_->height();
        const int x0 = kX + 98, y0 = kY + 8;
        for (int y = 0; y < h && y0 + y < s.height(); ++y) {
            std::uint8_t* row = s.row(y0 + y);
            for (int x = 0; x < w && x0 + x < s.width(); ++x)
                row[x0 + x] = remap_[px[static_cast<std::size_t>(y * w + x)]];
        }
    }

    // --- Common page parts (5) --------------------------------------------

    void line(render::IndexedSurface& s, const ui::UiContext& ui, int x, int y, const std::string& t) {
        drawClipped(s, *ui.standard, {kX + kLabelX, kY, kRight - kLabelX, statBottom_}, kX + x, kY + y, t, ui.white);
    }

    void header(render::IndexedSurface& s, const ui::UiContext& ui, const std::string& name, const std::string& icon,
                const std::string& description, bool unitPage) {
        statBottom_ = unitPage ? 355 : 385;
        const render::Font& f = *ui.standard;
        drawClipped(s, f, {kX + 210, kY + 20, 320, 20}, kX + kRight - f.width(name), kY + 20, name, ui.white);
        // The description, cut off at the bottom of its box (no scrolling).
        const ui::Rect box{kX + 300, kY + 275, 230, statBottom_ - 275};
        int y = box.y;
        for (const std::string& l : f.wrap(description, box.w)) {
            if (y >= box.y + box.h) break;
            drawClipped(s, f, box, box.x, y, l, ui.white);
            y += f.lineHeight();
        }
        if (const formats::DbiSprite* sp = app_.data().sprites.get(game::SpriteSet::ListBox, icon + "999")) {
            formats::DbiSprite opaque = *sp; // drawn opaque
            std::fill(opaque.opaque.begin(), opaque.opaque.end(), 1);
            s.blit(opaque, kX + 492, kY + 37);
        }
    }

    // PREREQUISITE (6); nothing without a context.
    void prerequisite(render::IndexedSurface& s, const ui::UiContext& ui, int y, int p, bool buildingPage) {
        const game::PrerequisiteText t = game::prerequisiteText(data_, p, request_.context, buildingPage);
        using K = game::PrerequisiteText::Kind;
        if (t.kind == K::Hidden) return;
        line(s, ui, kLabelX, y, text(30, "PREREQUISITE"));
        const std::string value = t.kind == K::None       ? text(881, "None")
                                  : t.kind == K::Building ? t.name
                                                          : text(880, "Not available on this continent.");
        line(s, ui, kValueX, y, value);
    }

    // --- Unit page (5.1) ----------------------------------------------------

    void drawUnit(render::IndexedSurface& s, const ui::UiContext& ui) {
        const int type = nav_.entry();
        const game::UnitPageRows r = game::unitPageRows(data_, type, nav_.unitLevel(type));
        const game::EncyclopediaUnit* cur = r.cur ? r.cur : nav_.displayedUnit(type);
        if (!cur) return;
        const game::EncyclopediaUnit& max = r.max ? *r.max : *cur;
        const game::EncyclopediaUnit& next = r.next ? *r.next : *cur;
        header(s, ui, cur->name, cur->image, cur->description, true);
        auto num = [](int v) { return std::to_string(v); };
        static constexpr const char* kCategory[3] = {"GRAV", "GROUND", "WATER"};
        line(s, ui, kLabelX, 60, text(22, "TYPE"));
        line(s, ui, kValueX, 60, text(31 + std::clamp(cur->category, 0, 2), kCategory[std::clamp(cur->category, 0, 2)]));
        prerequisite(s, ui, 78, cur->prerequisite, false);
        line(s, ui, kLabelX, 96, text(37, "DEPLOY COST"));
        line(s, ui, kValueX, 96, num(cur->cost));
        line(s, ui, kValueX, 121, text(223, "ACTUAL"));
        line(s, ui, kMaxX, 121, text(225, "MAXIMUM"));
        line(s, ui, kLabelX, 139, text(23, "LEVEL"));
        line(s, ui, kValueX, 139, num(r.level + 1));
        line(s, ui, kMaxX, 139, num(r.levels));
        line(s, ui, kLabelX, 157, text(24, "ARMOR"));
        line(s, ui, kValueX, 157, num(cur->armor));
        line(s, ui, kMaxX, 157, num(max.armor));
        line(s, ui, kLabelX, 175, text(25, "ACTION POINTS"));
        line(s, ui, kValueX, 175, num(cur->actionPoints));
        line(s, ui, kMaxX, 175, num(max.actionPoints));
        line(s, ui, kLabelX, 200, text(26, "ARMAMENT"));
        line(s, ui, kValueX, 200, cur->weapon);
        line(s, ui, 240, 218, cur->isAA ? text(235, "DAMAGE AA") : text(27, "AVG DAMAGE"));
        auto damage = [](const game::EncyclopediaUnit& u) {
            return u.isAA ? game::damageRange(u.damageAA, 0) : game::damageRange(u.damageBase, u.damageRandom);
        };
        line(s, ui, kValueX, 218, damage(*cur));
        line(s, ui, kMaxX, 218, damage(max));
        line(s, ui, 240, 236, text(28, "ROF"));
        line(s, ui, kValueX, 236, num(game::rateOfFire(*cur)));
        line(s, ui, kMaxX, 236, num(game::rateOfFire(max)));
        line(s, ui, 240, 254, text(29, "RANGE"));
        line(s, ui, kValueX, 254, num(cur->range));
        line(s, ui, kMaxX, 254, num(max.range));

        gauge(s, 141, r.levels - 1, r.level, next.level);
        gauge(s, 159, max.armor, cur->armor, next.armor);
        gauge(s, 177, max.actionPoints, cur->actionPoints, next.actionPoints);
        gauge(s, 220, game::averageDamage(max), game::averageDamage(*cur), game::averageDamage(next));
        gauge(s, 238, game::rateOfFire(max), game::rateOfFire(*cur), game::rateOfFire(next));
        gauge(s, 256, max.range, cur->range, next.range);

        int x = 420;
        for (const int id : game::abilityIcons(*cur)) {
            abilityIcon(s, ui, id, kX + x, kY + 18);
            x -= 30;
        }
    }

    // A 78 x 14 gauge at (385, y): background, red from the left, dark red
    // from the right (5.1). The bitmaps come from the user's LIBDAY.EXE, or
    // OpenDay draws them.
    void gauge(render::IndexedSurface& s, int y, int m, int a, int b) {
        const int gx = kX + 385, gy = kY + y;
        const game::GaugeBars bars = game::gaugeBars(m, a, b);
        const formats::DbiSprite* back = app_.exeBitmap(252);
        if (back) {
            s.blit(*back, gx, gy);
        } else {
            fillBox(s, {gx, gy, 78, 14}, grey_);
            fillBox(s, {gx + 2, gy + 1, 74, 12}, yellow_);
        }
        auto bar = [&](const formats::DbiSprite* art, int x, int w, std::uint8_t color) {
            if (w <= 0) return;
            if (!art) return fillBox(s, {x, gy + 1, w, 12}, color);
            for (int yy = 0; yy < std::min(12, art->height); ++yy)
                for (int xx = 0; xx < std::min(w, art->width); ++xx)
                    if (x + xx >= 0 && x + xx < s.width() && gy + 1 + yy < s.height())
                        s.row(gy + 1 + yy)[x + xx] = art->pixels[static_cast<std::size_t>(yy * art->width + xx)];
        };
        bar(app_.exeBitmap(195), gx + 2, bars.red, red_);
        bar(app_.exeBitmap(251), gx + 76 - bars.dark, bars.dark, darkRed_);
    }

    // A 25 x 25 ability icon; OpenDay's substitute is a framed box with a
    // short label.
    void abilityIcon(render::IndexedSurface& s, const ui::UiContext& ui, int id, int x, int y) {
        drawAbilityIcon(app_, s, ui, id, x, y);
    }

    // --- Other pages (5.2 - 5.5) --------------------------------------------

    void drawAutomated(render::IndexedSurface& s, const ui::UiContext& ui) {
        const game::EncyclopediaAutomated& a = data_.automated[static_cast<std::size_t>(nav_.entry())];
        header(s, ui, a.name, a.image, a.description, false);
        prerequisite(s, ui, 80, a.prerequisite, false);
        line(s, ui, kLabelX, 98, text(37, "DEPLOY COST"));
        line(s, ui, kValueX, 98, std::to_string(a.cost));
        line(s, ui, kLabelX, 116, text(24, "ARMOR"));
        line(s, ui, kValueX, 116, std::to_string(a.armor));
    }

    void drawBuilding(render::IndexedSurface& s, const ui::UiContext& ui) {
        const game::EncyclopediaBuilding& b = data_.buildings[static_cast<std::size_t>(nav_.entry())];
        header(s, ui, b.name, b.image, b.description, false);
        line(s, ui, kLabelX, 80, text(40, "PRODUCTION"));
        line(s, ui, kValueX, 80, b.production);
        prerequisite(s, ui, 98, b.prerequisite, true);
        line(s, ui, kLabelX, 116, text(38, "COST"));
        line(s, ui, kValueX, 116, std::to_string(b.cost));
        line(s, ui, kLabelX, 134, text(39, "RECYCLE VALUE"));
        line(s, ui, kValueX, 134, std::to_string(b.recycle));
        line(s, ui, kLabelX, 152, text(24, "ARMOR"));
        line(s, ui, kValueX, 152, std::to_string(b.armor));
        line(s, ui, kLabelX, 170, text(41, "AREA"));
        char area[32];
        std::snprintf(area, sizeof area, "%d x %d", b.sizeX, b.sizeY);
        line(s, ui, kValueX, 170, area);
        static constexpr const char* kPosition[7] = {"SHALLOW WATER", "DEEP WATER", "RIVER SIDE", "PLAIN",
                                                     "MOUNTAINS",     "FOREST",     "CRACK"};
        const int pos = std::clamp(b.position, 0, 6);
        line(s, ui, kLabelX, 188, text(42, "POSITION"));
        line(s, ui, kValueX, 188, text(43 + pos, kPosition[pos]));
    }

    void drawWall(render::IndexedSurface& s, const ui::UiContext& ui) {
        const game::EncyclopediaWall& w = data_.walls[static_cast<std::size_t>(nav_.entry())];
        header(s, ui, w.name, w.image, w.description, false);
        static constexpr const char* kType[3] = {"Concrete", "Laser", "Force"};
        const int t = std::clamp(w.id, 0, 2);
        line(s, ui, kLabelX, 80, text(22, "TYPE"));
        line(s, ui, kValueX, 80, text(176 + t, kType[t]));
        line(s, ui, kLabelX, 98, text(38, "COST"));
        line(s, ui, kValueX, 98, std::to_string(w.cost));
        line(s, ui, kLabelX, 116, text(24, "ARMOR"));
        line(s, ui, kValueX, 116, std::to_string(w.armor));
        prerequisite(s, ui, 141, w.prerequisite, false);
    }

    void drawMine(render::IndexedSurface& s, const ui::UiContext& ui) {
        const game::EncyclopediaMine& m = data_.mines[static_cast<std::size_t>(nav_.entry())];
        header(s, ui, m.name, m.image, m.description, false);
        const int t = std::clamp(m.id, 0, 1);
        line(s, ui, kLabelX, 80, text(22, "TYPE"));
        line(s, ui, kValueX, 80, text(174 + t, t == 0 ? "Standard" : "Grav"));
        line(s, ui, kLabelX, 98, text(38, "COST"));
        line(s, ui, kValueX, 98, std::to_string(m.cost));
        line(s, ui, kLabelX, 116, text(27, "AVG DAMAGE"));
        line(s, ui, kValueX, 116, game::damageRange(m.damageBase, m.damageRandom));
        prerequisite(s, ui, 141, m.prerequisite, false);
    }

    EncyclopediaRequest request_;
    const game::EncyclopediaData& data_;
    game::EncyclopediaNavigator nav_;
    int width_ = 640, height_ = 480, resolution_ = 0;
    const render::SmkFrames* pictures_ = nullptr;
    std::array<std::uint8_t, 256> remap_{};
    std::uint8_t yellow_ = 0, red_ = 0, darkRed_ = 0, grey_ = 0;
    std::vector<ui::Button> buttons_;
    ui::RadioGroup race_, category_;
    int statBottom_ = 355;
};

} // namespace

void drawAbilityIcon(App& app, render::IndexedSurface& s, const ui::UiContext& ui, int id, int x, int y) {
    if (const formats::DbiSprite* art = app.exeBitmap(id)) return s.blit(*art, x, y);
    const std::uint8_t yellow = nearestColor(app.data().palette, 232, 200, 40);
    const char* label = id == 185 ? "AA" : id == 184 ? "MD" : id == 182 ? "FO" : id == 181 ? "ST" : id == 183 ? "IF" : "JA";
    fillBox(s, {x, y, 25, 25}, ui.black);
    frameBox(s, {x, y, 25, 25}, yellow);
    ui.standard->drawIn(s, x, y, 25, 25, label, yellow);
}

std::unique_ptr<Screen> makeEncyclopedia(App& app, EncyclopediaRequest request) {
    return std::make_unique<EncyclopediaScreen>(app, std::move(request));
}

void openEncyclopedia(App& app, EncyclopediaRequest request) { app.push(makeEncyclopedia(app, std::move(request))); }

} // namespace openday::app
