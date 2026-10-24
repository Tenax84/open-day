// The map screens (docs/mechanics/tactical-interface.md 7,
// strategic-interface.md 4.2): a top-down picture of the whole map with
// object dots, a player selector and a list of types whose objects blink
// when selected. The screen covers the viewport and the side panel; the
// owning view stays visible below it (its message strip).
#include "app/map_screen.hpp"

#include <algorithm>
#include <cmath>
#include <map>

#include "app/campaign.hpp"
#include "app/screens.hpp"
#include "battle/battle_state.hpp"
#include "data/sqlite_db.hpp"

namespace openday::app {
namespace {

// Minimap colour of an object (tactical-interface.md 2.6): players 1-4 by
// colour, the AI by race; neutral tech buildings and ruins have rows 8 and 9.
game::Rgb objectRgb(const game::GlobalRules& rules, int type, int owner, int race, int specialRow = -1) {
    static constexpr int kRaceRow[4] = {0, 5, 7, 6};
    const int row = specialRow >= 0 ? specialRow : owner >= 1 && owner <= 4 ? owner : kRaceRow[std::clamp(race, 0, 3)];
    const auto it = rules.colors.find({type, row});
    return it == rules.colors.end() ? game::Rgb{255, 255, 255} : it->second;
}

game::Rgb fixedRgb(const game::GlobalRules& rules, int type) {
    const auto it = rules.colors.find({type, 0});
    return it == rules.colors.end() ? game::Rgb{200, 200, 200} : it->second;
}

class MapScreen : public Screen {
public:
    MapScreen(App& app, MapScreenConfig c) : Screen(app), c_(std::move(c)) {
        const bool large = c_.resolution == 1;
        width_ = large ? 1024 : 640;
        height_ = large ? 768 : 480;
        viewW_ = (width_ - 131) / 56 * 56;
        viewH_ = (height_ - 24) / 28 * 28;
        // The map square, centred horizontally; the picture centred in it.
        const int side = std::min(viewW_, viewH_);
        square_ = {(viewW_ - side) / 2, 0, side, side};
        pic_ = c_.picture.width > 0 ? std::min(c_.picture.width, side) : side * 396 / 448;
        ox_ = square_.x + (square_.w - pic_) / 2;
        oy_ = square_.y + (square_.h - pic_) / 2;
        scale_ = static_cast<double>(pic_) / std::max(1, c_.mapSize);
        block_ = static_cast<int>(std::ceil(scale_));
        base_.resize(pic_, pic_, app.ui().black);
        if (c_.picture.width > 0)
            for (int y = 0; y < std::min(pic_, c_.picture.height); ++y)
                std::copy_n(c_.picture.pixels.begin() + static_cast<std::ptrdiff_t>(y) * c_.picture.width, pic_, base_.row(y));
        if (c_.staticMarkers) c_.staticMarkers(base_, scale_, block_);

        const int px = viewW_;
        auto at = [px](int x, int y, int w, int h) { return ui::Rect{px + x, y, w, h}; };
        ui::Rect closeRect = c_.closeRect;
        closeRect.x += px;
        buttons_.push_back(makeButton(closeRect, c_.closeFrame, c_.closeCaption, ui::kKeyEscape, [this] { close(); }));
        buttons_.push_back(makeButton(at(5, 0, 44, 43), c_.emblemFrame, "", ui::kKeyF1, [this] {
            if (c_.encyclopedia) c_.encyclopedia(selectedType());
        }));
        buttons_.push_back(makeButton(at(8, 431, 53, 17), 13, "Up", 0, [this] { list_.scroll(-1); }));
        buttons_.push_back(makeButton(at(75, 431, 53, 17), 17, "Down", 0, [this] { list_.scroll(1); }));
        static constexpr int kItemFrame[5] = {46, 52, 58, 64, 70};
        for (std::size_t i = 0; i < c_.items.size() && i < 5; ++i)
            players_.items.push_back(makeButton(at(10 + 19 * static_cast<int>(i), 73, 19, 19),
                                                kItemFrame[std::clamp(c_.items[i].color, 0, 4)], "", 0, nullptr, true));
        players_.check(std::clamp(c_.initialItem, 0, static_cast<int>(players_.items.size()) - 1));
        players_.onChange = [this](int) { fillList(); };
        list_.rect = at(12, 96, 116, 330);
        list_.rowHeight = 17;
        list_.onSelect = [this](int) { phase_ = 0; };
        fillList();
    }

    int width() const override { return width_; }
    int height() const override { return height_; }
    bool overlay() const override { return true; }
    bool updatesBelow() const override { return c_.updatesBelow; }
    bool paletteCycling() const override { return true; }

    void handle(const ui::InputEvent& e) override {
        for (ui::Button& b : buttons_)
            if (b.handle(e)) return;
        if (players_.handle(e) || list_.handle(e)) return;
        // A click on the picture centres the view there and closes (7.1).
        if (e.type != ui::InputEvent::Type::MouseDown || e.button != 1) return;
        if (c_.clickable && !c_.clickable()) return;
        if (const auto t = game::overviewTile(e.x - ox_, e.y - oy_, pic_, c_.mapSize)) {
            if (c_.centre) c_.centre(*t);
            close();
        }
    }

    void update(int ms) override {
        timer_ += ms;
        for (; timer_ >= c_.blinkMs; timer_ -= c_.blinkMs) phase_ = (phase_ + 1) & 3;
    }

    void draw(render::IndexedSurface& s) override {
        const ui::UiContext& ui = app_.ui(c_.resolution);
        fillBox(s, {0, 0, viewW_, viewH_}, ui.black);
        for (int y = 0; y < pic_; ++y)
            if (oy_ + y >= 0 && oy_ + y < s.height()) std::copy_n(base_.row(y), pic_, s.row(oy_ + y) + ox_);
        drawDots(s);
        if (c_.markers) c_.markers(s, ox_, oy_, scale_, phase_);
        drawViewport(s, ui);
        // Side panel.
        ui::drawFrame(s, ui, c_.panelFrame, {viewW_, 0, width_ - viewW_, height_});
        for (const ui::Button& b : buttons_) b.draw(s, ui);
        drawNumber(s, ui, {viewW_ + 64, 26, 40, 14}, c_.resource ? c_.resource() : 0);
        const ui::Rect name{viewW_ + 6, 53, 121, 15};
        fillBox(s, name, ui.black);
        if (const MapScreenItem* item = selectedItem())
            ui.standard->drawIn(s, name.x + 2, name.y, name.w - 4, name.h, item->name, ui.white, render::Align::Left);
        players_.draw(s, ui);
        fillBox(s, list_.rect, ui.black);
        list_.draw(s, ui);
    }

private:
    const MapScreenItem* selectedItem() const {
        const int i = players_.checked();
        return i >= 0 && i < static_cast<int>(c_.items.size()) ? &c_.items[static_cast<std::size_t>(i)] : nullptr;
    }

    int selectedType() const {
        return list_.selected >= 0 && list_.selected < static_cast<int>(rows_.size()) ? rows_[static_cast<std::size_t>(list_.selected)].type
                                                                                       : -1;
    }

    void fillList() {
        rows_ = c_.rows && selectedItem() ? c_.rows(selectedItem()->owner) : std::vector<game::OverviewRow>{};
        list_.rows.clear();
        for (const game::OverviewRow& r : rows_)
            list_.rows.push_back("  " + (c_.typeName ? c_.typeName(r.type) : std::to_string(r.type)) + " x" + std::to_string(r.count));
        list_.selected = -1;
        list_.top = 0;
    }

    std::uint8_t color(game::Rgb c) {
        const int key = (c.r << 16) | (c.g << 8) | c.b;
        const auto it = colors_.find(key);
        if (it != colors_.end()) return it->second;
        return colors_[key] = nearestColor(app_.data().palette, c.r, c.g, c.b);
    }

    void drawDots(render::IndexedSurface& s) {
        if (!c_.dots) return;
        std::vector<MapScreenDot> dots;
        c_.dots(dots);
        const int type = selectedType();
        const MapScreenItem* item = selectedItem();
        for (const MapScreenDot& d : dots) {
            const bool blink = type >= 0 && item && d.type == type && d.owner == item->owner;
            const std::uint8_t c = color(blink ? game::blinkColor(d.color, phase_) : d.color);
            const int side = std::max(1, block_ + 1 + d.extra);
            fillBox(s, {ox_ + static_cast<int>(d.tile.x * scale_), oy_ + static_cast<int>(d.tile.y * scale_), side, side}, c);
        }
    }

    // The owning view's visible part, as on the minimap (1-px black and white).
    void drawViewport(render::IndexedSurface& s, const ui::UiContext& ui) {
        if (!c_.viewport) return;
        game::Point tl;
        int span = 0;
        c_.viewport(tl, span);
        ui::Rect v{ox_ + static_cast<int>(tl.x * scale_), oy_ + static_cast<int>(tl.y * scale_), static_cast<int>(span * scale_),
                   static_cast<int>(span * scale_)};
        v.x = std::clamp(v.x, ox_, ox_ + pic_ - 2);
        v.y = std::clamp(v.y, oy_, oy_ + pic_ - 2);
        v.w = std::min(v.w, ox_ + pic_ - v.x);
        v.h = std::min(v.h, oy_ + pic_ - v.y);
        frameBox(s, v, ui.black);
        frameBox(s, {v.x + 1, v.y + 1, v.w - 2, v.h - 2}, ui.white);
    }

    void close() {
        auto done = c_.onClose;
        app_.pop();
        if (done) done();
    }

    MapScreenConfig c_;
    int width_ = 640, height_ = 480, viewW_ = 504, viewH_ = 448;
    ui::Rect square_;
    int pic_ = 396, ox_ = 0, oy_ = 0, block_ = 1;
    double scale_ = 1;
    render::IndexedSurface base_;
    std::vector<ui::Button> buttons_;
    ui::RadioGroup players_;
    ui::ListBox list_;
    std::vector<game::OverviewRow> rows_;
    std::map<int, std::uint8_t> colors_;
    int timer_ = 0, phase_ = 0;
};

// --- Markers of the tactical map (7.2) -------------------------------------------------

// Draws a keyed bitmap scaled to w x h (nearest neighbour) at (x, y).
void blitScaled(render::IndexedSurface& s, const formats::DbiSprite& sp, int x, int y, int w, int h) {
    for (int yy = 0; yy < h; ++yy)
        for (int xx = 0; xx < w; ++xx) {
            const int sx = xx * sp.width / w, sy = yy * sp.height / h;
            const std::size_t i = static_cast<std::size_t>(sy * sp.width + sx);
            if (!sp.opaque[i]) continue;
            const int tx = x + xx, ty = y + yy;
            if (tx >= 0 && ty >= 0 && tx < s.width() && ty < s.height()) s.row(ty)[tx] = sp.pixels[i];
        }
}

void blitAt(render::IndexedSurface& s, const formats::DbiSprite& sp, int x, int y) { s.blit(sp, x, y); }

class TacticalMarkers {
public:
    TacticalMarkers(App& app, const game::BattleState& st, int localPlayer) : app_(app), st_(st), local_(localPlayer) {
        const formats::Palette& p = app.data().palette;
        red_ = nearestColor(p, 200, 0, 0);
        orange_ = nearestColor(p, 255, 140, 0);
        yellow_ = nearestColor(p, 255, 230, 60);
        black_ = nearestColor(p, 0, 0, 0);
        static constexpr std::uint8_t kFlag[5][3] = {{200, 0, 0}, {40, 80, 255}, {240, 220, 0}, {170, 40, 200}, {0, 190, 40}};
        for (int i = 0; i < 5; ++i) flag_[i] = nearestColor(p, kFlag[i][0], kFlag[i][1], kFlag[i][2]);
        for (const game::BattleBuilding& b : st.buildings())
            if (b.alive && b.type->techReward > 0) tech_.push_back(b.pos);
    }

    // Deployment zone with flags, objective zone with markers (drawn once).
    void drawStatic(render::IndexedSurface& pic, double s, int b) {
        const std::vector<game::Point>& zone = st_.deployZone[static_cast<std::size_t>(std::clamp(local_, 0, 4))];
        shade(pic, zone, s, b);
        for (const game::TileRect& r : game::groupTiles(zone)) {
            const game::Point c = r.centre();
            const int x = std::max(0, static_cast<int>(c.x * s) - 10), y = std::max(0, static_cast<int>(c.y * s) - 24);
            if (const formats::DbiSprite* f = local_ >= 1 && local_ <= 4 ? app_.exeBitmap(239 + local_, true) : nullptr)
                blitAt(pic, *f, x, y);
            else if (local_ >= 1 && local_ <= 4)
                drawFlag(pic, x, y, flag_[local_]);
        }
        shade(pic, st_.objective.zone, s, b);
        for (const game::TileRect& r : game::groupTiles(st_.objective.zone)) {
            const game::Point c = r.centre();
            const int x = static_cast<int>(c.x * s) - 6, y = static_cast<int>(c.y * s) - 19;
            if (const formats::DbiSprite* o = app_.exeBitmap(152, true)) blitAt(pic, *o, x, y);
            else drawObjective(pic, x, y);
        }
    }

    // The light bulbs over the tech buildings, animated with the phase.
    void drawBulbs(render::IndexedSurface& s, int ox, int oy, double sc, int phase) {
        for (const game::TileRect& r : game::groupTiles(tech_)) {
            const game::Point c = r.centre();
            const int x = ox + static_cast<int>(c.x * sc) - 2, y = oy + static_cast<int>(c.y * sc) - 8;
            if (const formats::DbiSprite* bulb = app_.exeBitmap(305 + (phase & 3), true)) blitAt(s, *bulb, x, y);
            else drawBulb(s, x, y, phase);
        }
    }

private:
    void shade(render::IndexedSurface& pic, const std::vector<game::Point>& tiles, double s, int b) {
        const formats::DbiSprite* sp = app_.exeBitmap(213, true);
        for (const game::Point t : tiles) {
            const int x = static_cast<int>(t.x * s), y = static_cast<int>(t.y * s);
            if (sp) {
                blitScaled(pic, *sp, x, y, b, b);
                continue;
            }
            for (int yy = 0; yy < b; ++yy) // OpenDay: a 50 % red stipple
                for (int xx = 0; xx < b; ++xx)
                    if ((x + xx + y + yy) % 2 == 0 && x + xx < pic.width() && y + yy < pic.height()) pic.row(y + yy)[x + xx] = red_;
        }
    }

    void put(render::IndexedSurface& s, int x, int y, std::uint8_t c) {
        if (x >= 0 && y >= 0 && x < s.width() && y < s.height()) s.row(y)[x] = c;
    }

    // Substitutes: a 16 x 25 flag, an 8 x 20 marker, a 15 x 22 bulb.
    void drawFlag(render::IndexedSurface& s, int x, int y, std::uint8_t c) {
        for (int yy = 0; yy < 25; ++yy) put(s, x + 1, y + yy, black_);
        for (int yy = 0; yy < 10; ++yy)
            for (int xx = 2; xx < 15 - yy / 2; ++xx) put(s, x + xx, y + yy, c);
    }

    void drawObjective(render::IndexedSurface& s, int x, int y) {
        for (int yy = 0; yy < 20; ++yy)
            for (int xx = 0; xx < 8; ++xx) {
                const bool head = yy < 8 && std::abs(xx - 3) + std::abs(yy - 4) <= 4;
                if (head) put(s, x + xx, y + yy, orange_);
                else if (xx == 3 || xx == 4) put(s, x + xx, y + yy, black_);
            }
    }

    void drawBulb(render::IndexedSurface& s, int x, int y, int phase) {
        const std::uint8_t c = phase % 2 == 0 ? yellow_ : orange_;
        for (int yy = 0; yy < 22; ++yy)
            for (int xx = 0; xx < 15; ++xx) {
                const int dx = xx - 7, dy = yy - 7;
                if (dx * dx + dy * dy <= 42) put(s, x + xx, y + yy, c);
                else if (yy >= 14 && yy < 20 && xx >= 4 && xx <= 10) put(s, x + xx, y + yy, black_);
            }
    }

    App& app_;
    const game::BattleState& st_;
    int local_;
    std::vector<game::Point> tech_;
    std::uint8_t red_ = 0, orange_ = 0, yellow_ = 0, black_ = 0;
    std::uint8_t flag_[5]{};
};

} // namespace

std::unique_ptr<Screen> makeMapScreen(App& app, MapScreenConfig config) { return std::make_unique<MapScreen>(app, std::move(config)); }

formats::BmpImage loadOverviewPicture(const fs::path& scenario, int resolution) {
    formats::BmpImage out;
    try {
        const data::SqliteDb db(scenario);
        const char* column = resolution == 1 ? "mapScreen1024" : "mapScreen640";
        db.forEach(std::string("SELECT ") + column + " FROM \"Scenario Info\"", [&](const data::Row& r) {
            const std::vector<std::uint8_t> bmp = r.blob(0);
            if (!bmp.empty()) out = formats::readBmpImage(bmp);
        });
    } catch (const std::exception&) {
        out = {};
    }
    return out;
}

std::unique_ptr<Screen> makeTacticalMap(App& app, TacticalMapConfig t) {
    const game::BattleState* st = t.state;
    const game::GlobalRules& rules = app.data().rules;
    MapScreenConfig c;
    c.resolution = t.resolution;
    c.panelFrame = 41;
    c.emblemFrame = 88;
    c.closeFrame = t.first ? 700 : 84;
    c.closeCaption = t.first ? "Continue" : "Back";
    c.closeRect = {10, 458, 53, 17};
    c.blinkMs = 175;
    c.updatesBelow = true;
    c.clickable = t.clickable;
    c.picture = loadOverviewPicture(t.scenario, t.resolution);
    c.mapSize = st->size();
    // Players in player-list order, then the aliens (none in a death match).
    for (const int p : st->players)
        if (p != 0) c.items.push_back({p, p, t.playerName ? t.playerName(p) : "Player"});
    if (st->computerPlayer) c.items.push_back({0, 0, t.playerName ? t.playerName(0) : "Alien"});
    for (std::size_t i = 0; i < c.items.size(); ++i)
        if (c.items[i].owner == t.localPlayer) c.initialItem = static_cast<int>(i);
    const int local = t.localPlayer;
    c.resource = [st, local] { return st->deployBudget[static_cast<std::size_t>(std::clamp(local, 0, 4))]; };
    c.rows = [st](int owner) {
        std::vector<int> types;
        for (const game::BattleUnit& u : st->units())
            if (u.alive && u.owner == owner && !u.type->isConvoy) types.push_back(u.type->typeId);
        return game::overviewRows(types);
    };
    c.typeName = [st](int type) {
        const game::UnitType* u = st->unitType(type, 0);
        if (!u) u = st->unitType(type, -1);
        return u ? u->name : std::to_string(type);
    };
    c.dots = [st, &rules](std::vector<MapScreenDot>& out) {
        for (const game::BattleBuilding& b : st->buildings())
            if (b.alive) {
                const int special = b.type->techReward > 0 && b.owner == 0 ? 8 : b.type->category == 1 ? 9 : -1;
                const game::Rgb rgb = objectRgb(rules, 0, b.owner, b.type->race, special);
                for (int dy = 0; dy < b.type->sizeY; ++dy)
                    for (int dx = 0; dx < b.type->sizeX; ++dx) out.push_back({{b.pos.x + dx, b.pos.y + dy}, 1, rgb, b.owner, -1});
            }
        for (const game::BattleRoad& r : st->roads())
            if (r.alive) out.push_back({r.pos, 0, fixedRgb(rules, 2), r.owner, -1});
        for (const game::BattleWall& w : st->walls())
            if (w.alive) out.push_back({w.pos, 0, fixedRgb(rules, 3), w.owner, -1});
        for (const game::BattleUnit& u : st->units())
            if (u.alive) out.push_back({u.pos, -1, objectRgb(rules, 1, u.owner, u.type->race), u.owner, u.type->typeId});
    };
    auto markers = std::make_shared<TacticalMarkers>(app, *st, local);
    c.staticMarkers = [markers](render::IndexedSurface& pic, double s, int b) { markers->drawStatic(pic, s, b); };
    c.markers = [markers](render::IndexedSurface& s, int ox, int oy, double sc, int phase) { markers->drawBulbs(s, ox, oy, sc, phase); };
    c.viewport = t.viewport;
    c.centre = t.centre;
    c.onClose = t.onClose;
    // Emblem: the unit page of the selected row's type (no context).
    std::map<int, int> levels = st->techLevels[static_cast<std::size_t>(std::clamp(local, 0, 4))];
    App* a = &app;
    c.encyclopedia = [a, levels](int type) {
        EncyclopediaRequest r;
        r.levels = levels;
        if (type >= 0) {
            r.kind = EncyclopediaRequest::Kind::Unit;
            r.type = type;
        }
        openEncyclopedia(*a, std::move(r));
    };
    return makeMapScreen(app, std::move(c));
}

std::unique_ptr<Screen> makeCityMap(App& app, CityMapConfig m) {
    const std::shared_ptr<CampaignContext> ctx = m.ctx;
    const game::GlobalRules& rules = app.data().rules;
    const game::City& city = ctx->campaign.city;
    MapScreenConfig c;
    c.resolution = m.resolution;
    c.panelFrame = 578;
    c.emblemFrame = 627;
    c.closeFrame = 623;
    c.closeCaption = "Continue";
    c.closeRect = {10, 457, 110, 17};
    c.blinkMs = 250;
    fs::path db = app.data().scenario(city.scenario);
    if (db.empty() && ctx->res.scenario) db = ctx->res.scenario(city.scenario);
    c.picture = loadOverviewPicture(db, m.resolution);
    c.mapSize = city.size();
    // One item per player, no alien item (single player: the campaign player).
    c.items.push_back({game::kCampaignPlayer, game::kCampaignPlayer, ctx->campaign.playerName});
    c.resource = [ctx] { return ctx->campaign.player.credits; };
    c.rows = [ctx](int owner) {
        std::vector<int> types;
        for (const game::CityBuilding& b : ctx->campaign.city.buildings)
            if (b.owner == owner) types.push_back(b.type);
        return game::overviewRows(types);
    };
    c.typeName = [&rules](int type) {
        const auto it = rules.buildings.find(type);
        return it == rules.buildings.end() ? std::to_string(type) : it->second.name;
    };
    c.dots = [ctx, &rules](std::vector<MapScreenDot>& out) {
        const game::City& cy = ctx->campaign.city;
        for (const game::CityBuilding& b : cy.buildings) {
            const auto t = rules.buildings.find(b.type);
            const int race = t == rules.buildings.end() ? 0 : t->second.race;
            const game::Rgb rgb = objectRgb(rules, 0, b.owner, race);
            for (int dy = 0; dy < b.h; ++dy)
                for (int dx = 0; dx < b.w; ++dx) out.push_back({{b.pos.x + dx, b.pos.y + dy}, 1, rgb, b.owner, b.type});
        }
        for (const game::CityRoad& r : cy.roads) out.push_back({r.pos, 0, fixedRgb(rules, 2), r.owner, -1});
        for (const game::CityWall& w : cy.walls) out.push_back({w.pos, 0, fixedRgb(rules, 3), w.owner, -1});
    };
    c.viewport = m.viewport;
    c.centre = m.centre;
    App* a = &app;
    c.encyclopedia = [a](int type) {
        EncyclopediaRequest r;
        if (type >= 0) {
            r.kind = EncyclopediaRequest::Kind::Building;
            r.type = type;
        }
        openEncyclopedia(*a, std::move(r));
    };
    return makeMapScreen(app, std::move(c));
}

} // namespace openday::app
