// The city screen of the campaign: building, recycling and moving
// structures on the player's city map (docs/mechanics/city-building.md 9),
// plus the army screen entry and a small message box.
#include <algorithm>
#include <cmath>
#include <optional>
#include <set>

#include "app/campaign.hpp"
#include "formats/pe_resources.hpp"
#include "map/map_objects.hpp"

namespace openday::app {
namespace {

constexpr int kPlayer = game::kCampaignPlayer;
constexpr int kListRows = 4, kRowH = 40;

// --- Message box (frame 477: 254 x 144; YES 478, NO 482, OK 486) -----------------------

class MessageBox : public Screen {
public:
    MessageBox(App& app, std::string text, int resolution, bool yesNo, std::function<void()> onYes)
        : Screen(app), text_(std::move(text)), resolution_(resolution), onYes_(std::move(onYes)) {
        box_ = {(width() - 254) / 2, (height() - 144) / 2, 254, 144};
        if (yesNo) {
            buttons_.push_back(makeButton({box_.x + 40, box_.y + 118, 53, 17}, 478, "Yes", 'y', [this] { finish(true); }));
            buttons_.push_back(makeButton({box_.x + 161, box_.y + 118, 53, 17}, 482, "No", 'n', [this] { finish(false); }));
            buttons_.back().hotkey = ui::kKeyEscape;
        } else {
            buttons_.push_back(makeButton({box_.x + 100, box_.y + 118, 53, 17}, 486, "OK", ui::kKeyEnter, [this] { finish(true); }));
        }
    }
    int width() const override { return resolution_ == 1 ? 1024 : 640; }
    int height() const override { return resolution_ == 1 ? 768 : 480; }
    void handle(const ui::InputEvent& e) override {
        for (ui::Button& b : buttons_)
            if (b.handle(e)) return;
    }
    void draw(render::IndexedSurface& s) override {
        const ui::UiContext& ui = app_.ui(resolution_);
        s.fill(ui.black);
        ui::drawFrame(s, ui, 477, box_);
        ui.standard->drawWrapped(s, box_.x + 12, box_.y + 12, box_.w - 24, 96, text_, ui.white);
        for (const ui::Button& b : buttons_) b.draw(s, ui);
    }

private:
    void finish(bool yes) {
        auto action = onYes_;
        app_.pop();
        if (yes && action) action();
    }
    std::string text_;
    int resolution_;
    std::function<void()> onYes_;
    ui::Rect box_;
    std::vector<ui::Button> buttons_;
};

// --- City screen ---------------------------------------------------------------------------

class CityScreen : public Screen {
public:
    CityScreen(App& app, CampaignPtr ctx) : Screen(app), ctx_(std::move(ctx)), colors_(app.data().palette) {
        large_ = app.settings().resolution == 1;
        width_ = large_ ? 1024 : 640;
        height_ = large_ ? 768 : 480;
        viewW_ = (width_ - 131) / 56 * 56;
        viewH_ = (height_ - 24) / 28 * 28;
        viewport_.resize(viewW_, viewH_);
        rebuildScene();
        buildMinimap();
        buildControls();
        // Start at the player's Headquarters.
        for (const game::CityBuilding& b : city().buildings)
            if (b.owner == kPlayer && b.type == game::kHeadquarters) centerOn({b.pos.x + 2, b.pos.y + 2});
        app.startMusic();
    }

    int width() const override { return width_; }
    int height() const override { return height_; }
    bool paletteCycling() const override { return true; }

    void handle(const ui::InputEvent& e) override {
        if (e.type == ui::InputEvent::Type::MouseMove) {
            mouseX_ = e.x;
            mouseY_ = e.y;
        }
        for (ui::Button& b : buttons_)
            if (b.handle(e)) return;
        if (tabs_.handle(e)) return;
        if (recycle_.handle(e) || move_.handle(e)) return;
        if (e.type == ui::InputEvent::Type::KeyDown) {
            switch (e.key) {
            case ui::kKeyLeft: return scrollStep(-1, 0);
            case ui::kKeyRight: return scrollStep(1, 0);
            case ui::kKeyUp: return scrollStep(0, -1);
            case ui::kKeyDown: return scrollStep(0, 1);
            case ui::kKeyEscape:
                segmentStart_.reset();
                picked_.reset();
                return;
            default: return;
            }
        }
        const ui::Rect list{viewW_ + 9, 247, 118, kListRows * kRowH};
        if (e.type == ui::InputEvent::Type::Wheel && list.contains(e.x, e.y)) return scrollList(-e.key);
        if (e.type != ui::InputEvent::Type::MouseDown) return;
        if (list.contains(e.x, e.y) && e.button == 1) {
            const int row = top_ + (e.y - list.y) / kRowH;
            if (row < static_cast<int>(options_.size())) selectEntry(row);
            return;
        }
        const ui::Rect mm = minimapRect();
        if (mm.contains(e.x, e.y) && e.button == 1) {
            const double s = 112.0 / city().size();
            centerOn({static_cast<int>((e.x - mm.x) / s), static_cast<int>((e.y - mm.y) / s)});
            return;
        }
        if (e.x >= viewW_ || e.y >= viewH_) return;
        const game::Point tile = tileAt(e.x, e.y);
        if (e.button == 3) {
            segmentStart_.reset();
            picked_.reset();
            return;
        }
        if (e.button == 1 && city().inside(tile)) mapClick(tile);
    }

    void update(int ms) override {
        if (!introChecked_) {
            introChecked_ = true;
            // Arrival on colonies 2-4: the continent screen first, in continue mode.
            const game::Campaign& c = ctx_->campaign;
            int captured = 0;
            for (const game::ProvinceState& p : c.provinces) captured += p.captured ? 1 : 0;
            if (c.colonyNumber > 1 && captured == 1 && !ctx_->continueShown) {
                ctx_->continueShown = true;
                app_.push(makeContinentScreen(app_, ctx_, ContinentMode::Continue));
            }
        }
        if (messageMs_ > 0) messageMs_ -= ms;
        edgeScroll(ms);
    }

    void draw(render::IndexedSurface& s) override {
        const ui::UiContext& ui = app_.ui(large_ ? 1 : 0);
        s.fill(ui.black);
        drawMap();
        for (int y = 0; y < viewH_; ++y) std::copy_n(viewport_.row(y), viewW_, s.row(y));
        ui::drawFrame(s, ui, 100, {viewW_, 0, width_ - viewW_, height_});
        drawNumber(s, ui, {viewW_ + 64, 26, 40, 14}, ctx_->campaign.player.credits);
        drawMinimap(s, ui);
        drawList(s, ui);
        ui::drawFrame(s, ui, 498, {0, viewH_, viewW_, height_ - viewH_});
        const ui::Rect msg{64, viewH_ + 5, viewW_ - 135 - 64, 20};
        fillBox(s, msg, ui.black);
        if (messageMs_ > 0) ui.standard->drawIn(s, msg.x + 2, msg.y, msg.w - 4, msg.h, message_, ui.white, render::Align::Left);
        recycle_.checked = mode_ == Mode::Recycle;
        move_.checked = mode_ == Mode::Move;
        for (const ui::Button& b : buttons_) b.draw(s, ui);
        tabs_.draw(s, ui);
        recycle_.draw(s, ui);
        move_.draw(s, ui);
    }

private:
    enum class Mode { Build, Recycle, Move };

    game::City& city() { return ctx_->campaign.city; }
    const game::City& city() const { return ctx_->campaign.city; }
    const game::GlobalRules& rules() const { return app_.data().rules; }

    // --- Controls ------------------------------------------------------------

    void buildControls() {
        const int px = viewW_;
        auto at = [px](int x, int y, int w, int h) { return ui::Rect{px + x, y, w, h}; };
        ui::Button emblem = makeButton(at(5, 0, 44, 43), 143, "", ui::kKeyF1, nullptr);
        emblem.enabled = false; // no encyclopedia yet
        buttons_.push_back(emblem);
        ui::Button map = makeButton(at(11, 171, 110, 17), 105, "Map", ui::kKeyF4, nullptr);
        map.enabled = false; // the city overview map is not implemented
        buttons_.push_back(map);
        buttons_.push_back(makeButton(at(9, 407, 53, 17), 121, "Up", 0, [this] { scrollList(-1); }));
        buttons_.push_back(makeButton(at(74, 407, 53, 17), 125, "Down", 0, [this] { scrollList(1); }));
        buttons_.push_back(makeButton(at(11, 455, 53, 17), 135, "Campaign", 'c', [this] {
            app_.push(makeContinentScreen(app_, ctx_, ContinentMode::Normal));
        }));
        buttons_.push_back(makeButton(at(70, 455, 53, 17), 139, "Army", 'a', [this] { openCampaignArmy(app_, ctx_); }));
        buttons_.push_back(makeButton({viewW_ - 60, viewH_ + 8, 53, 17}, 693, "Menu", ui::kKeyF2, [this] {
            app_.push(makeCampaignOptions(app_, ctx_, large_ ? 1 : 0));
        }));
        tabs_.items.push_back(makeButton(at(12, 198, 19, 19), 109, "", 'b', nullptr, true));
        tabs_.items.push_back(makeButton(at(12, 222, 19, 19), 115, "", 'd', nullptr, true));
        tabs_.onChange = [this](int i) {
            buildingsTab_ = i == 0;
            rebuildList();
        };
        tabs_.check(1); // Defenses first
        recycle_ = makeButton(at(11, 435, 53, 17), 147, "Recycle", 'r', [this] {
            mode_ = mode_ == Mode::Recycle ? Mode::Build : Mode::Recycle;
            picked_.reset();
            segmentStart_.reset();
        }, true);
        move_ = makeButton(at(70, 435, 53, 17), 129, "Move", 'm', [this] {
            mode_ = mode_ == Mode::Move ? Mode::Build : Mode::Move;
            picked_.reset();
            segmentStart_.reset();
        }, true);
        rebuildList();
    }

    void rebuildList() {
        const std::optional<game::BuildOption> keep =
            selected_ >= 0 && selected_ < static_cast<int>(options_.size()) ? std::optional(options_[static_cast<std::size_t>(selected_)])
                                                                             : std::nullopt;
        const std::vector<int>& available = ctx_->campaign.colony.buildingsAvailable;
        options_ = buildingsTab_ ? game::buildingOptions(city(), rules(), available, kPlayer)
                                 : game::defenseOptions(city(), rules(), available, kPlayer);
        selected_ = -1;
        if (keep)
            for (std::size_t i = 0; i < options_.size(); ++i)
                if (options_[i].kind == keep->kind && options_[i].type == keep->type) selected_ = static_cast<int>(i);
        top_ = std::clamp(top_, 0, std::max(0, static_cast<int>(options_.size()) - kListRows));
        if (selected_ < 0) segmentStart_.reset();
    }

    void selectEntry(int row) {
        selected_ = row;
        mode_ = Mode::Build;
        segmentStart_.reset();
        picked_.reset();
        const game::StructureKind k = options_[static_cast<std::size_t>(row)].kind;
        if (k == game::StructureKind::Road || k == game::StructureKind::Wall)
            showMessage(campaignText(app_, 407, "Click on starting point"), 20000);
        else
            messageMs_ = 0;
    }

    void scrollList(int d) {
        top_ = std::clamp(top_ + d, 0, std::max(0, static_cast<int>(options_.size()) - kListRows));
    }

    void showMessage(const std::string& m, int ms) {
        message_ = m;
        messageMs_ = ms;
    }

    void error(int code) {
        showMessage(campaignText(app_, 384 + code, "Can't build there"), 5000);
        app_.sound("INVALIDE.WAV");
    }

    // --- Map -----------------------------------------------------------------

    void rebuildScene() {
        scene_ = std::make_unique<game::MapScene>(city().terrain, game::mapObjectSprites(city().objects(), rules(), city().terrain));
        buildMinimap();
    }

    game::Point tileAt(int x, int y) const {
        return game::iso::tileAt(static_cast<int>(camX_) + x, static_cast<int>(camY_) + y, city().size());
    }

    void centerOn(game::Point tile) {
        const game::Point o = game::iso::tileOrigin(tile.x, tile.y, city().size());
        camX_ = static_cast<float>(o.x + 27 - viewW_ / 2);
        camY_ = static_cast<float>(o.y + 42 - viewH_ / 2);
        clampCamera();
    }

    void clampCamera() {
        const game::Rect b = game::iso::mapBounds(city().size());
        camX_ = std::clamp(camX_, static_cast<float>(-viewW_ / 2), static_cast<float>(b.w - viewW_ / 2));
        camY_ = std::clamp(camY_, static_cast<float>(-viewH_ / 2), static_cast<float>(b.h - viewH_ / 2));
    }

    void scrollStep(int dx, int dy) {
        camX_ += static_cast<float>(dx * 56);
        camY_ += static_cast<float>(dy * 28);
        clampCamera();
    }

    void edgeScroll(int ms) {
        if (mouseX_ < 0 || app_.top() != this) return;
        const int dx = mouseX_ < 5 ? -1 : mouseX_ >= width_ - 5 ? 1 : 0;
        const int dy = mouseY_ < 5 ? -1 : mouseY_ >= height_ - 5 ? 1 : 0;
        if (!dx && !dy) {
            scrollAcc_ = 0;
            return;
        }
        static constexpr int kInterval[3] = {100, 50, 0};
        scrollAcc_ += ms;
        if (scrollAcc_ >= kInterval[std::clamp(app_.settings().scrollSpeed, 0, 2)]) {
            scrollAcc_ = 0;
            scrollStep(dx, dy);
        }
    }

    const formats::DbiSprite* symbol(const std::string& name) {
        return app_.data().sprites.get(game::SpriteSet::Symbols, name);
    }

    void marker(std::vector<game::Overlay>& out, game::Point t, const std::string& name, bool top = false) {
        const game::Point o = game::iso::tileOrigin(t.x, t.y, city().size());
        if (const formats::DbiSprite* sp = symbol(name)) out.push_back({top ? -1 : t.x, top ? -1 : t.y, o.x, o.y, sp});
    }

    // Map sprites of a structure for the cursor, tinted on a checkerboard.
    void cursorSprites(std::vector<game::Overlay>& out, game::StructureKind kind, int type, game::Point at, bool valid) {
        std::vector<std::pair<game::Point, std::pair<game::SpriteSet, std::string>>> parts;
        switch (kind) {
        case game::StructureKind::Building:
        case game::StructureKind::Tower: {
            const auto t = rules().buildings.find(type);
            if (t == rules().buildings.end()) return;
            for (int dy = 0; dy < t->second.sizeY; ++dy)
                for (int dx = 0; dx < t->second.sizeX; ++dx)
                    parts.push_back({{at.x + dx, at.y + dy},
                                     {game::SpriteSet::UnitsBuildings,
                                      t->second.image + std::to_string(kPlayer) + "0" + std::to_string(dx) + std::to_string(dy)}});
            break;
        }
        case game::StructureKind::Road:
            if (const auto t = rules().roads.find(type); t != rules().roads.end())
                parts.push_back({at, {game::SpriteSet::Roads, t->second.image + "G000"}});
            break;
        case game::StructureKind::Wall:
            if (const auto t = rules().walls.find(type); t != rules().walls.end())
                parts.push_back({at, {game::SpriteSet::Walls, t->second.image + "000"}});
            break;
        case game::StructureKind::Mine:
            if (const auto t = rules().mines.find(type); t != rules().mines.end())
                parts.push_back({at, {game::SpriteSet::Mines, t->second.image + "000"}});
            break;
        }
        for (const auto& [tile, sprite] : parts) {
            const formats::DbiSprite* sp = app_.data().sprites.get(sprite.first, sprite.second);
            if (!sp) continue;
            formats::DbiSprite tinted = *sp;
            for (int y = 0; y < tinted.height; ++y)
                for (int x = 0; x < tinted.width; ++x) {
                    const std::size_t i = static_cast<std::size_t>(y * tinted.width + x);
                    if (tinted.opaque[i] && (x + y) % 2 == 0) tinted.pixels[i] = valid ? 250 : 249;
                }
            tinted_.push_back(std::move(tinted));
            const game::Point o = game::iso::tileOrigin(tile.x, tile.y, city().size());
            out.push_back({-1, -1, o.x, o.y, nullptr});
        }
    }

    bool ownStructureAt(game::Point t) const {
        const game::City& c = city();
        const int b = c.buildingAt(t), w = c.wallAt(t), r = c.roadAt(t), m = c.mineAt(t);
        return (b >= 0 && c.buildings[static_cast<std::size_t>(b)].owner == kPlayer) ||
               (w >= 0 && c.walls[static_cast<std::size_t>(w)].owner == kPlayer) ||
               (r >= 0 && c.roads[static_cast<std::size_t>(r)].owner == kPlayer) ||
               (m >= 0 && c.mines[static_cast<std::size_t>(m)].owner == kPlayer);
    }

    // An own structure of the selected type on the tile (recycled by a click).
    bool selectedTypeAt(game::Point t) const {
        if (selected_ < 0) return false;
        const game::BuildOption& o = options_[static_cast<std::size_t>(selected_)];
        const game::City& c = city();
        switch (o.kind) {
        case game::StructureKind::Building:
        case game::StructureKind::Tower: {
            const int b = c.buildingAt(t);
            return b >= 0 && c.buildings[static_cast<std::size_t>(b)].owner == kPlayer && c.buildings[static_cast<std::size_t>(b)].type == o.type;
        }
        case game::StructureKind::Road: {
            const int r = c.roadAt(t);
            return r >= 0 && c.roads[static_cast<std::size_t>(r)].owner == kPlayer && c.roads[static_cast<std::size_t>(r)].type == o.type;
        }
        case game::StructureKind::Wall: {
            const int w = c.wallAt(t);
            return w >= 0 && c.walls[static_cast<std::size_t>(w)].owner == kPlayer && c.walls[static_cast<std::size_t>(w)].type == o.type;
        }
        case game::StructureKind::Mine: {
            const int m = c.mineAt(t);
            return m >= 0 && c.mines[static_cast<std::size_t>(m)].owner == kPlayer && c.mines[static_cast<std::size_t>(m)].type == o.type;
        }
        }
        return false;
    }

    void drawMap() {
        std::vector<game::Overlay> markers;
        tinted_.clear();
        tinted_.reserve(64);
        // Bridge sites without a road (city-building.md 4.3).
        for (const game::Point p : city().bridgeSites())
            if (city().roadAt(p) < 0)
                marker(markers, p,
                       game::areaOk(city(), rules(), kPlayer, game::StructureKind::Road, 1, p) ? "BRIDGE" : "BRIDG001");
        const game::Point hover = mouseX_ >= 0 && mouseX_ < viewW_ && mouseY_ >= 0 && mouseY_ < viewH_ ? tileAt(mouseX_, mouseY_)
                                                                                                    : game::Point{-1, -1};
        const bool inside = city().inside(hover);
        std::vector<game::Overlay> cursor;
        if (mode_ == Mode::Recycle) {
            if (inside) marker(cursor, hover, ownStructureAt(hover) ? "CRECY2" : "CRECY", true);
        } else if (mode_ == Mode::Move) {
            if (!picked_) {
                if (inside) marker(cursor, hover, ownStructureAt(hover) ? "CMOVE" : "CINVA", true);
            } else if (inside) {
                pickedCursor(cursor, hover);
            }
        } else if (selected_ >= 0) {
            const game::BuildOption& o = options_[static_cast<std::size_t>(selected_)];
            const bool segment = o.kind == game::StructureKind::Road || o.kind == game::StructureKind::Wall;
            if (segment && segmentStart_) {
                // The reachable band along the start's row and column.
                static constexpr game::Point kDirs[4] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
                marker(markers, *segmentStart_, "ROUTE");
                for (const game::Point d : kDirs)
                    for (game::Point p{segmentStart_->x + d.x, segmentStart_->y + d.y};
                         game::areaOk(city(), rules(), kPlayer, o.kind, o.type, p); p = {p.x + d.x, p.y + d.y})
                        marker(markers, p, "ROUTE");
            }
            if (inside) {
                if (selectedTypeAt(hover)) marker(cursor, hover, "CRECY2", true);
                else {
                    bool valid = game::checkPlacement(city(), rules(), kPlayer, o.kind, o.type, hover) == game::kBuildOk;
                    if (segment && segmentStart_ && (hover.x == segmentStart_->x || hover.y == segmentStart_->y))
                        valid = game::areaOk(city(), rules(), kPlayer, o.kind, o.type, hover);
                    cursorSprites(cursor, o.kind, o.type, hover, valid);
                }
            }
        }
        // Tinted sprites point into tinted_, which no longer grows.
        std::size_t t = 0;
        for (game::Overlay& ov : cursor)
            if (!ov.sprite && t < tinted_.size()) ov.sprite = &tinted_[t++];
        markers.insert(markers.end(), cursor.begin(), cursor.end());
        game::SceneExtras extras;
        extras.overlays = std::move(markers);
        viewport_.fill(0);
        scene_->draw(viewport_, {static_cast<int>(camX_), static_cast<int>(camY_), viewW_, viewH_}, app_.data().sprites, true, &extras);
    }

    void pickedCursor(std::vector<game::Overlay>& out, game::Point at) {
        const game::City& c = city();
        const game::Point from = *picked_;
        game::MovedObjects moved;
        const int b = c.buildingAt(from);
        if (b >= 0 && c.buildings[static_cast<std::size_t>(b)].owner == kPlayer) moved.building = b;
        if (const int w = c.wallAt(from); w >= 0 && c.walls[static_cast<std::size_t>(w)].owner == kPlayer) moved.wall = w;
        if (const int r = c.roadAt(from); r >= 0 && c.roads[static_cast<std::size_t>(r)].owner == kPlayer) moved.road = r;
        if (const int m = c.mineAt(from); m >= 0 && c.mines[static_cast<std::size_t>(m)].owner == kPlayer) moved.mine = m;
        auto show = [&](game::StructureKind k, int type) {
            cursorSprites(out, k, type, at, game::checkPlacement(c, rules(), kPlayer, k, type, at, moved) == game::kBuildOk);
        };
        if (moved.building >= 0) {
            const int type = c.buildings[static_cast<std::size_t>(moved.building)].type;
            const auto t = rules().buildings.find(type);
            show(t != rules().buildings.end() && game::isTower(t->second) ? game::StructureKind::Tower : game::StructureKind::Building, type);
        }
        if (moved.wall >= 0) show(game::StructureKind::Wall, c.walls[static_cast<std::size_t>(moved.wall)].type);
        if (moved.road >= 0) show(game::StructureKind::Road, c.roads[static_cast<std::size_t>(moved.road)].type);
        if (moved.mine >= 0) show(game::StructureKind::Mine, c.mines[static_cast<std::size_t>(moved.mine)].type);
    }

    // --- Actions ---------------------------------------------------------------

    void mapClick(game::Point tile) {
        if (mode_ == Mode::Recycle) return recycle(tile);
        if (mode_ == Mode::Move) return moveClick(tile);
        if (selected_ < 0) return;
        const game::BuildOption o = options_[static_cast<std::size_t>(selected_)];
        if (selectedTypeAt(tile)) return recycle(tile);
        game::Campaign& c = ctx_->campaign;
        const bool segment = o.kind == game::StructureKind::Road || o.kind == game::StructureKind::Wall;
        if (segment && segmentStart_) {
            if (!pieceAffordable(o)) return;
            const game::BuildResult r =
                game::buildSegment(city(), rules(), kPlayer, c.player.credits, c.built, o.kind, o.type, *segmentStart_, tile);
            segmentStart_.reset();
            if (r.pieces == 0) return error(r.code);
            if (r.pieces == 1) showMessage(formats::formatMessage(campaignText(app_, 403, "%1 has been built"), {r.name}), 20000);
            else
                showMessage(formats::formatMessage(campaignText(app_, 406, "%1 has been built for %2!d! credits"),
                                                   {r.name, std::to_string(r.credits)}),
                            20000);
            return changed();
        }
        const int code = game::checkPlacement(city(), rules(), kPlayer, o.kind, o.type, tile);
        if (code != game::kBuildOk) return error(code);
        if (segment) {
            segmentStart_ = tile;
            showMessage(campaignText(app_, 408, "Click on ending point of segment"), 20000);
            return;
        }
        if (!pieceAffordable(o)) return;
        const game::BuildResult r = game::buildStructure(city(), rules(), kPlayer, c.player.credits, c.built, o.kind, o.type, tile);
        if (r.code != game::kBuildOk) return error(r.code);
        if (o.kind == game::StructureKind::Building || o.kind == game::StructureKind::Tower) {
            builtHere_.insert({tile.x, tile.y});
            app_.sound("BUILD.WAV");
        }
        showMessage(formats::formatMessage(campaignText(app_, 403, "%1 has been built"), {r.name}), 20000);
        changed();
    }

    // The credit pre-check (city-building.md 9.2): one piece must be affordable.
    bool pieceAffordable(const game::BuildOption& o) {
        int cost = 0;
        switch (o.kind) {
        case game::StructureKind::Building:
        case game::StructureKind::Tower: cost = rules().buildings.at(o.type).cost; break;
        case game::StructureKind::Road: cost = rules().roads.at(o.type).cost; break;
        case game::StructureKind::Wall: cost = rules().walls.at(o.type).cost; break;
        case game::StructureKind::Mine: cost = rules().mines.at(o.type).cost; break;
        }
        if (ctx_->campaign.player.credits >= cost) return true;
        showMessage(campaignText(app_, 402, "Need more strategic resources"), 5000);
        app_.sound("INVALIDE.WAV");
        return false;
    }

    void recycle(game::Point tile) {
        if (!ownStructureAt(tile)) {
            showMessage(campaignText(app_, 400, "Can't recycle other's structure"), 5000);
            app_.sound("INVALIDE.WAV");
            return;
        }
        const int b = city().buildingAt(tile);
        if (b >= 0 && city().buildings[static_cast<std::size_t>(b)].type == game::kHeadquarters) return error(game::kBuildRecycleHq);
        // Confirmation unless built during this visit (city-building.md 6).
        bool fresh = builtHere_.count({tile.x, tile.y}) > 0;
        if (b >= 0) {
            const game::CityBuilding& bd = city().buildings[static_cast<std::size_t>(b)];
            fresh = ctx_->campaign.built.buildings.count(bd.id) > 0;
        }
        if (!fresh && !confirmed_) {
            app_.push(std::make_unique<MessageBox>(
                app_, campaignText(app_, 401, "You might not get all your money back if you recycle this. Do you want to continue anyway?"),
                large_ ? 1 : 0, true, [this, tile] {
                    confirmed_ = true;
                    doRecycle(tile);
                }));
            return;
        }
        doRecycle(tile);
    }

    void doRecycle(game::Point tile) {
        game::Campaign& c = ctx_->campaign;
        const int b = city().buildingAt(tile);
        const game::BuildResult r = game::recycleAt(city(), rules(), kPlayer, c.player.credits, c.built, tile);
        if (r.code != game::kBuildOk) return error(r.code);
        if (b >= 0) app_.sound("CASHIN.WAV");
        showMessage(formats::formatMessage(campaignText(app_, 404, "%1 has been recycled"), {r.name}), 20000);
        changed();
    }

    void moveClick(game::Point tile) {
        if (!picked_) {
            if (!ownStructureAt(tile)) {
                showMessage(campaignText(app_, 400, "Can't recycle other's structure"), 5000);
                app_.sound("INVALIDE.WAV");
                return;
            }
            const int b = city().buildingAt(tile);
            if (b >= 0 && city().buildings[static_cast<std::size_t>(b)].type == game::kHeadquarters) return error(game::kBuildMoveHq);
            picked_ = b >= 0 && city().buildings[static_cast<std::size_t>(b)].owner == kPlayer ? city().buildings[static_cast<std::size_t>(b)].pos
                                                                                                : tile;
            return;
        }
        const game::BuildResult r = game::moveAt(city(), rules(), kPlayer, ctx_->campaign.built, *picked_, tile);
        if (r.code != game::kBuildOk) return error(r.code);
        picked_.reset();
        showMessage(formats::formatMessage(campaignText(app_, 405, "%1 has been moved"), {r.name}), 20000);
        changed();
    }

    void changed() {
        rebuildScene();
        rebuildList();
    }

    // --- Panel pieces ------------------------------------------------------------

    ui::Rect minimapRect() const { return {viewW_ + 12, 49, 112, 112}; }

    std::uint8_t color(int r, int g, int b) {
        const int key = (r << 16) | (g << 8) | b;
        const auto it = colorCache_.find(key);
        if (it != colorCache_.end()) return it->second;
        return colorCache_[key] = nearestColor(colors_, r, g, b);
    }

    void buildMinimap() {
        const game::TerrainMap& t = city().terrain;
        const int n = t.size;
        minimap_.assign(112 * 112, 0);
        for (int y = 0; y < 112; ++y)
            for (int x = 0; x < 112; ++x) {
                const game::TerrainType* tt = rules().terrainType(t.at(x * n / 112, y * n / 112));
                const game::Rgb c = tt ? tt->color : game::Rgb{};
                minimap_[static_cast<std::size_t>(y * 112 + x)] = color(c.r / 4 * 4, c.g / 4 * 4, c.b / 4 * 4);
            }
    }

    void drawMinimap(render::IndexedSurface& s, const ui::UiContext& ui) {
        const ui::Rect r = minimapRect();
        for (int y = 0; y < 112; ++y) std::copy_n(minimap_.begin() + y * 112, 112, s.row(r.y + y) + r.x);
        const int n = city().size();
        const double sc = 112.0 / n;
        const int b = std::max(1, static_cast<int>(std::ceil(sc)));
        auto square = [&](game::Point p, std::uint8_t c) {
            fillBox(s, {r.x + static_cast<int>(p.x * sc), r.y + static_cast<int>(p.y * sc), b, b}, c);
        };
        auto colorOf = [&](int type, int row) {
            const auto it = rules().colors.find({type, row});
            const game::Rgb c = it == rules().colors.end() ? game::Rgb{255, 255, 255} : it->second;
            return color(c.r, c.g, c.b);
        };
        for (const game::CityRoad& rd : city().roads) square(rd.pos, colorOf(2, 0));
        for (const game::CityWall& w : city().walls) square(w.pos, colorOf(3, 0));
        for (const game::CityBuilding& bd : city().buildings)
            for (int dy = 0; dy < bd.h; ++dy)
                for (int dx = 0; dx < bd.w; ++dx) square({bd.pos.x + dx, bd.pos.y + dy}, colorOf(0, std::clamp(bd.owner, 0, 4)));
        const game::Point tl = tileAt(0, 0);
        const int span = std::max(viewW_ / 54, viewH_ / 28);
        ui::Rect v{r.x + static_cast<int>(tl.x * sc), r.y + static_cast<int>(tl.y * sc), static_cast<int>(span * sc),
                   static_cast<int>(span * sc)};
        v.x = std::clamp(v.x, r.x, r.x + r.w - 2);
        v.y = std::clamp(v.y, r.y, r.y + r.h - 2);
        v.w = std::min(v.w, r.x + r.w - v.x);
        v.h = std::min(v.h, r.y + r.h - v.y);
        frameBox(s, v, ui.black);
        frameBox(s, {v.x + 1, v.y + 1, v.w - 2, v.h - 2}, ui.white);
    }

    void drawList(render::IndexedSurface& s, const ui::UiContext& ui) {
        const render::Font& f = *ui.standard;
        const ui::Rect area{viewW_ + 9, 247, 118, kListRows * kRowH};
        fillBox(s, area, ui.black);
        for (int i = 0; i < kListRows; ++i) {
            const int row = top_ + i;
            if (row >= static_cast<int>(options_.size())) break;
            const game::BuildOption& o = options_[static_cast<std::size_t>(row)];
            std::string icon, name;
            int cost = 0;
            switch (o.kind) {
            case game::StructureKind::Building:
            case game::StructureKind::Tower: {
                const game::BuildingType& t = rules().buildings.at(o.type);
                icon = t.image;
                name = t.name;
                cost = t.cost;
                break;
            }
            case game::StructureKind::Road: {
                const game::RoadType& t = rules().roads.at(o.type);
                icon = t.image;
                name = t.name;
                cost = t.cost;
                break;
            }
            case game::StructureKind::Wall: {
                const game::WallType& t = rules().walls.at(o.type);
                icon = t.image;
                name = t.name;
                cost = t.cost;
                break;
            }
            case game::StructureKind::Mine: {
                const game::MineType& t = rules().mines.at(o.type);
                icon = t.image;
                name = t.name;
                cost = t.cost;
                break;
            }
            }
            const ui::Rect r{area.x, area.y + i * kRowH, 118, kRowH};
            if (const formats::DbiSprite* sp = app_.data().sprites.get(game::SpriteSet::ListBox, game::toUpper(icon) + "999"))
                s.blit(*sp, r.x + 2, r.y + 2);
            f.drawIn(s, r.x + 50, r.y + 2, r.w - 51, f.lineHeight(), name, ui.white, render::Align::Left);
            f.drawIn(s, r.x + 50, r.y + 24, r.w - 51, f.lineHeight(), std::to_string(cost), ui.white, render::Align::Left);
            if (row == selected_) frameBox(s, {r.x + 1, r.y + 1, r.w - 2, r.h - 2}, 255);
        }
    }

    CampaignPtr ctx_;
    formats::Palette colors_;
    std::map<int, std::uint8_t> colorCache_;
    bool large_ = false;
    int width_ = 640, height_ = 480, viewW_ = 504, viewH_ = 448;
    std::unique_ptr<game::MapScene> scene_;
    render::IndexedSurface viewport_;
    float camX_ = 0, camY_ = 0;
    int mouseX_ = -1, mouseY_ = -1, scrollAcc_ = 0;
    std::vector<ui::Button> buttons_;
    ui::RadioGroup tabs_;
    ui::Button recycle_, move_;
    std::vector<game::BuildOption> options_;
    int selected_ = -1, top_ = 0;
    bool buildingsTab_ = false;
    Mode mode_ = Mode::Build;
    std::optional<game::Point> segmentStart_, picked_;
    std::string message_;
    int messageMs_ = 0;
    std::vector<formats::DbiSprite> tinted_;
    std::vector<std::uint8_t> minimap_;
    std::set<std::pair<int, int>> builtHere_;
    bool confirmed_ = false;
    bool introChecked_ = false;
};

} // namespace

std::unique_ptr<Screen> makeCityScreen(App& app, CampaignPtr ctx) { return std::make_unique<CityScreen>(app, std::move(ctx)); }

void openCampaignArmy(App& app, CampaignPtr ctx) {
    const int resolution = app.settings().resolution == 1 ? 1 : 0;
    auto open = [&app, ctx, resolution] {
        game::Campaign& c = ctx->campaign;
        ArmyConfig a;
        a.variant = ArmyConfig::Variant::Strategic;
        a.units = c.deployableTypes(app.data().rules);
        a.automated = c.automatedTypes(app.data().rules);
        for (const int t : a.units) c.player.techLevels.try_emplace(t, 0); // newly available types start at level 0
        a.levels = &c.player.techLevels;
        a.techPoints = &c.player.tech;
        a.canUpgrade = c.ownsLaboratory();
        a.resolution = resolution;
        app.push(makeArmy(app, std::move(a)));
    };
    if (!ctx->campaign.ownsLaboratory()) {
        app.push(std::make_unique<MessageBox>(
            app, campaignText(app, 850, "You don't own any laboratory center. You will not be able to upgrade your units."),
            resolution, false, open));
        return;
    }
    open();
}

} // namespace openday::app
