// The battle screens: deployment, your turn, waiting for the other side,
// the objectives dialog, the in-game options and the battle report
// (docs/mechanics/tactical-interface.md).
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <set>

#include "anim/battle_session.hpp"
#include "app/map_screen.hpp"
#include "app/screens.hpp"
#include "battle/automated.hpp"
#include "battle/combat.hpp"
#include "battle/objectives.hpp"
#include "formats/bmp_palette.hpp"
#include "formats/pe_resources.hpp"
#include "map/map_objects.hpp"

namespace openday::app {
namespace {

constexpr int kHuman = 1; // the local player's slot in single-player games

std::string text(App& app, int id, const char* fallback) {
    const std::string s = app.data().text(id);
    return s.empty() ? fallback : s;
}

std::string fmt(App& app, int id, const char* fallback, const std::vector<std::string>& args) {
    return formats::formatMessage(text(app, id, fallback), args);
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

ui::Button button(ui::Rect r, int frame, const std::string& caption, int key, std::function<void()> action,
                  bool toggle = false) {
    ui::Button b;
    b.rect = r;
    b.firstFrame = frame;
    b.caption = caption;
    b.hotkey = key;
    b.onClick = std::move(action);
    b.toggle = toggle;
    return b;
}

// Deployment-zone marker number for a neighbor mask (map-objects.md).
int deployMarker(int mask) {
    static constexpr int kNumber[16] = {0, 13, 15, 14, 10, 1, 7, 4, 12, 3, 9, 6, 11, 2, 8, 5};
    return kNumber[mask & 15];
}

std::string twoDigits(int n) { return (n < 10 ? "0" : "") + std::to_string(n); }

// Palette index nearest to a color, with a cache.
class ColorMatcher {
public:
    explicit ColorMatcher(const formats::Palette& p) : palette_(p) {}
    std::uint8_t operator()(int r, int g, int b) {
        const int key = (r << 16) | (g << 8) | b;
        const auto it = cache_.find(key);
        if (it != cache_.end()) return it->second;
        return cache_[key] = nearestColor(palette_, r, g, b);
    }

private:
    formats::Palette palette_;
    std::map<int, std::uint8_t> cache_;
};

// ---------------------------------------------------------------------------

class AftermathScreen : public Screen {
public:
    AftermathScreen(App& app, const BattleConfig& config, std::array<game::BattleSession::Stats, game::kPlayerSlots> stats,
                    std::vector<int> players, std::shared_ptr<const game::BattleState> finalState)
        : Screen(app), config_(config), stats_(stats), players_(std::move(players)), final_(std::move(finalState)) {
        const game::BattleState& s = *final_;
        result_.success = s.success;
        result_.loser = s.loser;
        result_.techFound = s.techFound();
        result_.unitsLost = stats_[kHuman].unitsLost;
        result_.buildingsLost = stats_[kHuman].buildingsLost;
        result_.unitsDestroyed = stats_[0].unitsLost;
        result_.buildingsDestroyed = stats_[0].buildingsLost;
        result_.finalState = final_.get();
        continue_ = button({45 + 300, 0 + 419, 110, 17}, 366, "Continue", ui::kKeyEnter, [this] {
            if (config_.onFinished) {
                const auto done = config_.onFinished; // the callback may replace this screen
                done(result_);
            } else {
                app_.reset(makeMainMenu(app_));
            }
        });
    }
    void handle(const ui::InputEvent& e) override { continue_.handle(e); }
    void draw(render::IndexedSurface& s) override {
        const ui::UiContext& ui = app_.ui();
        s.fill(ui.black);
        const ui::Rect dlg{45, 0, 448, 480};
        ui::drawFrame(s, ui, 365, dlg);
        const ui::Rect area{dlg.x + 20, dlg.y + 20, dlg.w - 40, dlg.h - 50};
        const render::Font& f = *ui.standard;
        f.drawIn(s, area.x, area.y, area.w, f.lineHeight(), text(app_, 513, "Result"), ui.white);
        const bool won = result_.loser != 5 ? result_.loser != kHuman : result_.success;
        const std::string result = won ? (config_.aftermath.empty() ? text(app_, 519, "Victory.") : config_.aftermath)
                                       : text(app_, 520, "Defeat.");
        int y = area.y + 3 * f.lineHeight();
        for (const std::string& line : f.wrap(result, area.w)) {
            if (y > dlg.y + 170) break;
            f.drawIn(s, area.x, y, area.w, f.lineHeight(), line, ui.white);
            y += f.lineHeight();
        }
        // Statistics: destroyed = the other side's losses (single player).
        const int header = dlg.y + 185;
        const int cols[4] = {25, 115, 250, 360};
        const char* heads[4] = {"PLAYER", "DESTROYED", "LOST", "RANK"};
        const int ids[4] = {180, 512, 515, 514};
        for (int c = 0; c < 4; ++c) f.draw(s, area.x + cols[c], header, text(app_, ids[c], heads[c]), ui.white);
        int row = header + 2 * f.lineHeight();
        for (std::size_t i = 0; i < players_.size(); ++i) {
            const int p = players_[i];
            if (p == 0) continue;
            int destroyedUnits = 0, destroyedBuildings = 0;
            for (int q = 0; q < game::kPlayerSlots; ++q)
                if (q != p) {
                    destroyedUnits += stats_[static_cast<std::size_t>(q)].unitsLost;
                    destroyedBuildings += stats_[static_cast<std::size_t>(q)].buildingsLost;
                }
            const auto& mine = stats_[static_cast<std::size_t>(p)];
            f.draw(s, area.x + cols[0], row, text(app_, 739, "Player"), ui.white);
            f.draw(s, area.x + cols[1], row, fmt(app_, 516, "Units: %1", {std::to_string(destroyedUnits)}), ui.white);
            f.draw(s, area.x + cols[1], row + f.lineHeight(),
                   fmt(app_, 517, "Buildings: %1", {std::to_string(destroyedBuildings)}), ui.white);
            f.draw(s, area.x + cols[2], row, fmt(app_, 516, "Units: %1", {std::to_string(mine.unitsLost)}), ui.white);
            f.draw(s, area.x + cols[2], row + f.lineHeight(),
                   fmt(app_, 517, "Buildings: %1", {std::to_string(mine.buildingsLost)}), ui.white);
            f.draw(s, area.x + cols[3], row, "1", ui.white);
            row += f.lineHeight() * 5 / 2;
        }
        // The tech line (campaign province battles, on a win).
        if (config_.techLine && won)
            f.draw(s, area.x + 25, dlg.y + 418 - 30,
                   fmt(app_, 518, "New technologies found : %1", {std::to_string(result_.techFound)}), ui.white);
        continue_.draw(s, ui);
    }

private:
    BattleConfig config_;
    std::array<game::BattleSession::Stats, game::kPlayerSlots> stats_;
    std::vector<int> players_;
    std::shared_ptr<const game::BattleState> final_;
    BattleResult result_;
    ui::Button continue_;
};

// ---------------------------------------------------------------------------

class BattleScreen : public Screen {
public:
    BattleScreen(App& app, BattleConfig config, std::unique_ptr<game::BattleSession> session)
        : Screen(app), config_(std::move(config)), entry_(config_.entry), session_(std::move(session)),
          colors_(app.data().palette) {
        large_ = app.settings().resolution == 1;
        width_ = large_ ? 1024 : 640;
        height_ = large_ ? 768 : 480;
        viewW_ = (width_ - 131) / 56 * 56;
        viewH_ = (height_ - 24) / 28 * 28;
        viewport_.resize(viewW_, viewH_);
        session_->animations().animator().playSound = [this](const render::Animation& a) {
            app_.audio().soundEnabled = app_.settings().sound;
            app_.audio().play(a.audio);
        };
        // Automated-unit clips centre the view on their tile (automated-units.md 8.3).
        session_->animations().setView(width_, viewW_, viewH_);
        session_->animations().onCenter = [this](game::Point tile) { centerOn(tile); };
        buildMinimapTerrain();
        centerOnHuman();
        buildControls();
        app.startMusic();
        if (std::getenv("OPENDAY_LOG"))
            session_->log = [](const game::Event& e) { std::fprintf(stderr, "event %zu\n", e.index()); };
        if (!session_->state().deploying) session_->begin();
        if (session_->state().deploying) firstMap_ = true; // the tactical map first (opened by update)
        else openGoal();
    }

    int width() const override { return width_; }
    int height() const override { return height_; }
    bool paletteCycling() const override { return true; }

    // --- Input ---------------------------------------------------------------

    void handle(const ui::InputEvent& e) override {
        if (e.type == ui::InputEvent::Type::MouseMove) {
            mouseX_ = e.x;
            mouseY_ = e.y;
        }
        if (e.type != ui::InputEvent::Type::KeyDown) infoTile_.reset(); // any mouse event ends the info card
        if (dialog_ != Dialog::None) return handleDialog(e);
        const Mode mode = this->mode();
        if (e.type == ui::InputEvent::Type::KeyDown && handleScrollKey(e.key)) return;
        // Cheat keys: Tab, then W or L (tactical-interface.md 3.7).
        if (e.type == ui::InputEvent::Type::KeyDown && app_.settings().cheats && mode == Mode::Tactic) {
            if (e.key == ui::kKeyTab) {
                tab_ = true;
                return;
            }
            if (tab_ && (e.key == 'w' || e.key == 'l')) {
                tab_ = false;
                session_->send(game::CheatRequest{kHuman, e.key == 'w'});
                return;
            }
            tab_ = false;
        }
        for (ui::Button& b : buttons(mode))
            if (b.handle(e)) return;
        if (mode == Mode::Deploy) {
            if (deployList_.handle(e)) return;
        }
        if (mode == Mode::Automated) {
            if (autoList_.handle(e)) return;
        }
        // Minimap click: centre the view.
        const ui::Rect mm = minimapRect();
        if (e.type == ui::InputEvent::Type::MouseDown && e.button == 1 && mm.contains(e.x, e.y)) {
            const int n = session_->scene().terrain().size;
            const double s = 112.0 / n;
            centerOn({static_cast<int>((e.x - mm.x) / s), static_cast<int>((e.y - mm.y) / s)});
            return;
        }
        if (e.type == ui::InputEvent::Type::KeyDown && e.key == ui::kKeyEscape) fire_ = false;
        if (e.x >= viewW_ || e.y >= viewH_ || e.type != ui::InputEvent::Type::MouseDown) return;
        const game::Point tile = tileAt(e.x, e.y);
        if (!session_->state().inside(tile)) return;
        if (e.button == 3) {
            infoTile_ = tile;
            return;
        }
        if (e.button != 1 || session_->animations().busy()) return;
        if (mode == Mode::Deploy) deployClick(tile);
        else if (mode == Mode::Tactic) tacticClick(tile);
        else if (mode == Mode::Automated) automatedClick(tile);
    }

    void update(int ms) override {
        if (firstMap_ && app_.top() == this) {
            firstMap_ = false;
            openMap(true);
        }
        clockMs_ += ms;
        if (messageMs_ > 0) messageMs_ -= ms;
        tickAcc_ += ms;
        const int tickMs = 1000 / game::kTicksPerSecond;
        for (; tickAcc_ >= tickMs; tickAcc_ -= tickMs) session_->animations().tick();
        edgeScroll(ms);
        const Mode mode = this->mode();
        if (mode != lastMode_) {
            if (mode == Mode::Tactic && lastMode_ != Mode::Automated) {
                fire_ = false;
                done_.clear();
                std::erase_if(selection_, [&](int u) { return !ownUnit(u); });
                clockMs_ = 0;
            } else if (mode == Mode::Wait && lastMode_ != Mode::Deploy) {
                clockMs_ = 0;
            }
            if (lastMode_ == Mode::Wait && mode == Mode::Tactic) showMessage(fmt(app_, 897, "Turn: %1", {playerName(kHuman)}), 2000);
            lastMode_ = mode;
        }
        if (session_->state().techHeld.size() > techHeld_) app_.sound("TECH.WAV");
        techHeld_ = session_->state().techHeld.size();
        if (session_->displayedOver() && !session_->animations().busy() && app_.top() == this) {
            const game::BattleState& s = session_->state();
            app_.replace(std::make_unique<AftermathScreen>(app_, config_, session_->stats(), s.players,
                                                           std::make_shared<game::BattleState>(session_->server().state())));
        }
    }

    // --- Drawing -------------------------------------------------------------

    void draw(render::IndexedSurface& s) override {
        const ui::UiContext& ui = app_.ui(large_ ? 1 : 0);
        s.fill(ui.black);
        drawMap();
        for (int y = 0; y < viewH_; ++y) std::copy_n(viewport_.row(y), viewW_, s.row(y));
        const Mode mode = this->mode();
        const int panelFrame = mode == Mode::Deploy ? 0 : mode == Mode::Tactic ? 226 : mode == Mode::Automated ? 507 : 310;
        ui::drawFrame(s, ui, panelFrame, {viewW_, 0, width_ - viewW_, height_});
        drawStrip(s, ui);
        for (const ui::Button& b : buttons(mode)) b.draw(s, ui);
        drawResource(s, ui);
        if (infoTile_) drawInfoCard(s, ui, *infoTile_);
        else drawMinimap(s, ui);
        if (mode == Mode::Tactic) {
            drawUnitInfo(s, ui);
            drawTurnCounter(s, ui);
        } else if (mode == Mode::Wait) {
            drawPlayerList(s, ui);
        } else if (mode == Mode::Automated) {
            drawAutomatedList(s, ui);
        } else {
            drawDeployList(s, ui);
        }
        if (dialog_ == Dialog::Goal) drawGoal(s, ui);
        if (dialog_ == Dialog::Menu) drawMenu(s, ui);
    }

private:
    enum class Mode { Deploy, Tactic, Wait, Automated };
    enum class Dialog { None, Goal, Menu };

    Mode mode() const {
        if (session_->state().deploying) return Mode::Deploy;
        if (session_->displayedPlayer() == kHuman && !session_->animations().busy() && !session_->state().over)
            return reinforcing_ ? Mode::Deploy : automated_ ? Mode::Automated : Mode::Tactic;
        return Mode::Wait;
    }

    game::Point tileAt(int x, int y) const {
        return game::iso::tileAt(static_cast<int>(camX_) + x, static_cast<int>(camY_) + y,
                                 session_->scene().terrain().size);
    }

    bool ownUnit(int u) const {
        const game::BattleUnit& unit = session_->state().unit(u);
        return unit.alive && unit.owner == kHuman && !unit.type->isConvoy;
    }

    std::string playerName(int slot) const {
        return slot == 0 ? text(app_, 65, "Alien") : text(app_, 739, "Player");
    }

    void showMessage(const std::string& m, int ms) {
        message_ = m;
        messageMs_ = ms;
    }

    // --- Camera --------------------------------------------------------------

    void centerOn(game::Point tile) {
        const game::Point o = game::iso::tileOrigin(tile.x, tile.y, session_->scene().terrain().size);
        camX_ = static_cast<float>(o.x + 27 - viewW_ / 2);
        camY_ = static_cast<float>(o.y + 42 - viewH_ / 2);
        clampCamera();
    }

    void clampCamera() {
        const game::Rect b = game::iso::mapBounds(session_->scene().terrain().size);
        camX_ = std::clamp(camX_, static_cast<float>(-viewW_ / 2), static_cast<float>(b.w - viewW_ / 2));
        camY_ = std::clamp(camY_, static_cast<float>(-viewH_ / 2), static_cast<float>(b.h - viewH_ / 2));
    }

    void centerOnHuman() {
        const game::BattleState& s = session_->state();
        long sx = 0, sy = 0, n = 0;
        for (const game::BattleUnit& u : s.units())
            if (u.alive && u.owner == kHuman) {
                sx += u.pos.x;
                sy += u.pos.y;
                ++n;
            }
        for (const game::Point p : s.deployZone[kHuman]) {
            sx += p.x;
            sy += p.y;
            ++n;
        }
        if (n > 0) centerOn({static_cast<int>(sx / n), static_cast<int>(sy / n)});
        else centerOn({s.size() / 2, s.size() / 2});
    }

    void scrollStep(int dx, int dy) {
        camX_ += static_cast<float>(dx * 56);
        camY_ += static_cast<float>(dy * 28);
        clampCamera();
    }

    bool handleScrollKey(int key) {
        switch (key) {
        case ui::kKeyLeft: scrollStep(-1, 0); return true;
        case ui::kKeyRight: scrollStep(1, 0); return true;
        case ui::kKeyUp: scrollStep(0, -1); return true;
        case ui::kKeyDown: scrollStep(0, 1); return true;
        case ui::kKeyHome: scrollStep(-1, -1); return true;
        case ui::kKeyPageUp: scrollStep(1, -1); return true;
        case ui::kKeyEnd: scrollStep(-1, 1); return true;
        case ui::kKeyPageDown: scrollStep(1, 1); return true;
        default: return false;
        }
    }

    // Edge scrolling: within 5 px of the screen edges (3.6).
    void edgeScroll(int ms) {
        if (dialog_ != Dialog::None || mouseX_ < 0 || app_.top() != this) return;
        const int dx = mouseX_ < 5 ? -1 : mouseX_ >= width_ - 5 ? 1 : 0;
        const int dy = mouseY_ < 5 ? -1 : mouseY_ >= height_ - 5 ? 1 : 0;
        if (!dx && !dy) {
            scrollAcc_ = 0;
            return;
        }
        static constexpr int kInterval[3] = {100, 50, 0};
        const int interval = kInterval[std::clamp(app_.settings().scrollSpeed, 0, 2)];
        scrollAcc_ += ms;
        if (scrollAcc_ >= interval) {
            scrollAcc_ = 0;
            scrollStep(dx, dy);
        }
    }

    // --- Controls ------------------------------------------------------------

    void buildControls() {
        const int px = viewW_;
        auto at = [px](int x, int y, int w, int h) { return ui::Rect{px + x, y, w, h}; };
        const int stripRight = viewW_;
        const int stripY = viewH_;
        // Common: menu and chat on the message strip.
        auto strip = [&](std::vector<ui::Button>& v) {
            ui::Button chat = button({stripRight - 120, stripY + 8, 53, 17}, 499, "Chat", ui::kKeyF3, nullptr);
            chat.enabled = false; // fewer than two players
            v.push_back(chat);
            v.push_back(button({stripRight - 60, stripY + 8, 53, 17}, 693, "Menu", ui::kKeyF2, [this] { openMenu(); }));
        };

        // Deployment view (5.1).
        deployButtons_ = {
            button(at(5, 0, 44, 43), 25, "", ui::kKeyF1, [this] { openEncyclopediaHere(); }),
            button(at(9, 172, 53, 17), 5, "Map", ui::kKeyF4, [this] { openMap(false); }),
            button(at(68, 172, 53, 17), 9, "Goal", ui::kKeyF5, [this] { openGoal(); }),
            button(at(9, 406, 53, 17), 13, "Up", 0, [this] { deployList_.scroll(-1); }),
            button(at(66, 406, 53, 17), 17, "Down", 0, [this] { deployList_.scroll(1); }),
            button(at(9, 430, 110, 17), 29, "Army", 'a', [this] { openArmy(); }),
            button(at(9, 456, 110, 17), 21, "Continue", ui::kKeyEnter, [this] { finishDeployment(); }),
        };
        strip(deployButtons_);
        deployList_.rect = at(9, 206, 120, 5 * 39);
        deployList_.rowHeight = 39;
        buildDeployList();

        // Tactical view (2.1).
        tacticButtons_ = {
            button(at(5, 0, 44, 43), 294, "", ui::kKeyF1, [this] { openEncyclopediaHere(); }),
            button(at(10, 169, 53, 17), 231, "Map", ui::kKeyF4, [this] { openMap(false); }),
            button(at(71, 169, 53, 17), 235, "Goal", ui::kKeyF5, [this] { openGoal(); }),
            button(at(10, 252, 53, 17), 241, "Group", 'g', [this] { toggleGroup(); }, true),
            button(at(71, 252, 53, 17), 298, "Center", ui::kKeyF6, [this] { centerSelection(); }),
            button(at(12, 273, 110, 17), 247, "Move all", 'm', [this] { moveAll(); }),
            button(at(15, 295, 19, 19), 251, "", 'o', [this] { app_.settings().overwatch = !app_.settings().overwatch; }, true),
            button(at(12, 325, 110, 17), 258, "Fire", 'f', [this] { fire_ = !fire_ && !selection_.empty(); }, true),
            button(at(15, 344, 19, 19), 264, "", 'x', [this] { app_.settings().autofire = !app_.settings().autofire; }, true),
            button(at(12, 374, 19, 17), 270, "<", 'p', [this] { cycle(-1); }),
            button(at(41, 374, 53, 17), 278, "Guard", 0, [this] { guard(); }),
            button(at(103, 374, 19, 17), 274, ">", ' ', [this] { cycle(1); }),
            button(at(12, 402, 110, 17), 286, "Automated", 'a', [this] { openAutomated(); }),
            button(at(12, 428, 110, 17), 282, "Reinforcement", 'r', [this] { startReinforcement(); }),
            button(at(11, 456, 53, 17), 290, "End turn", ui::kKeyEnter, [this] { endTurn(); }),
        };
        strip(tacticButtons_);

        // Automated units view (6.2).
        autoButtons_ = {
            button(at(5, 0, 44, 43), 516, "", ui::kKeyF1, [this] { openEncyclopediaHere(); }),
            button(at(10, 169, 53, 17), 520, "Map", ui::kKeyF4, [this] { openMap(false); }),
            button(at(71, 169, 53, 17), 524, "Goal", ui::kKeyF5, [this] { openGoal(); }),
            button(at(9, 404, 53, 17), 528, "Up", 0, [this] { autoList_.scroll(-1); }),
            button(at(70, 404, 53, 17), 532, "Down", 0, [this] { autoList_.scroll(1); }),
            button(at(12, 428, 110, 17), 512, "Automated", ui::kKeyEscape, [this] { closeAutomated(); }),
        };
        strip(autoButtons_);
        autoList_.rect = at(9, 205, 118, 5 * 39);
        autoList_.rowHeight = 39;
        autoList_.onSelect = [this](int row) { selectAutomated(row); };

        // Waiting view (4.2).
        waitButtons_ = {
            button(at(5, 0, 44, 43), 323, "", ui::kKeyF1, [this] { openEncyclopediaHere(); }),
            button(at(10, 169, 53, 17), 315, "Map", ui::kKeyF4, [this] { openMap(false); }),
            button(at(71, 169, 53, 17), 319, "Goal", ui::kKeyF5, [this] { openGoal(); }),
        };
        strip(waitButtons_);
    }

    std::vector<ui::Button>& buttons(Mode m) {
        if (m == Mode::Deploy) return deployButtons_;
        if (m == Mode::Automated) return autoButtons_;
        if (m == Mode::Tactic) {
            // Toggle states follow the settings and the screen state.
            tacticButtons_[3].checked = group_;
            tacticButtons_[6].checked = app_.settings().overwatch;
            tacticButtons_[7].checked = fire_;
            tacticButtons_[8].checked = app_.settings().autofire;
            return tacticButtons_;
        }
        return waitButtons_;
    }

    // --- Deployment ----------------------------------------------------------

    void buildDeployList() {
        const game::BattleState& s = session_->state();
        static constexpr int kMovementOrder[10] = {2, 1, 4, 6, 5, 9, 3, 7, 0, 8};
        auto rank = [](int movement) {
            for (int i = 0; i < 10; ++i)
                if (kMovementOrder[i] == movement) return i;
            return 10;
        };
        deployTypes_.clear();
        // Only the human side's own race can be bought *(OpenDay: the list in
        // the scenario names both races)*.
        for (const int id : s.buyable(kHuman))
            if (const game::UnitType* t = s.unitType(id, s.techLevel(kHuman, id)); t && t->race == 0)
                deployTypes_.push_back(t);
        std::stable_sort(deployTypes_.begin(), deployTypes_.end(), [&](const game::UnitType* a, const game::UnitType* b) {
            if (rank(a->movement) != rank(b->movement)) return rank(a->movement) < rank(b->movement);
            return a->cost < b->cost;
        });
        deployList_.rows.clear();
        for (const game::UnitType* t : deployTypes_) deployList_.rows.push_back(t->name);
        deployList_.selected = deployTypes_.empty() ? -1 : 0;
    }

    void deployClick(game::Point tile) {
        const game::BattleState& s = session_->state();
        if (!s.inDeployZone(kHuman, tile)) {
            showMessage(text(app_, 452, "Outside your deployment zone"), 5000);
            app_.sound("INVALIDE.WAV");
            return;
        }
        const int u = s.unitAt(tile);
        if (u >= 0) {
            if (!s.deploying || std::find(deployed_.begin(), deployed_.end(), u) == deployed_.end()) return;
            if (session_->send(game::UndeployRequest{kHuman, u}).empty()) {
                showMessage(fmt(app_, 450, "%1 taken back", {s.unit(u).type->name}), 5000);
                std::erase(deployed_, u);
            }
            return;
        }
        if (deployList_.selected < 0) return;
        const game::UnitType* type = deployTypes_[static_cast<std::size_t>(deployList_.selected)];
        if (type->cost > s.deployBudget[kHuman]) {
            showMessage(text(app_, 448, "Not enough credits"), 5000);
            app_.sound("INVALIDE.WAV");
            return;
        }
        const std::size_t before = s.units().size();
        if (!session_->send(game::DeployRequest{kHuman, {{type->typeId, type->techLevel, tile}}}).empty()) {
            showMessage(text(app_, 451, "Not a valid tile for this unit"), 5000);
            app_.sound("INVALIDE.WAV");
            return;
        }
        deployed_.push_back(static_cast<int>(before));
        showMessage(fmt(app_, 449, "%1 placed", {type->name}), 5000);
        app_.sound("DEPLOY.WAV");
    }

    void finishDeployment() {
        if (reinforcing_) {
            reinforcing_ = false;
            return;
        }
        session_->begin();
        selection_.clear();
    }

    void startReinforcement() {
        reinforcing_ = true;
        selection_.clear();
        preview_.clear();
        plannedTarget_.reset();
        fire_ = false;
        buildDeployList();
    }

    void openArmy() {
        ArmyConfig a;
        a.variant = ArmyConfig::Variant::Deployment;
        for (const game::UnitType* t : deployTypes_) a.units.push_back(t->typeId);
        armyLevels_ = session_->state().techLevels[kHuman];
        a.levels = &armyLevels_;
        a.canUpgrade = false;
        a.resolution = large_ ? 1 : 0;
        a.onClose = [this](int type) {
            for (std::size_t i = 0; i < deployTypes_.size(); ++i)
                if (deployTypes_[i]->typeId == type) {
                    deployList_.selected = static_cast<int>(i);
                    if (deployList_.selected < deployList_.top) deployList_.top = deployList_.selected;
                    if (deployList_.selected >= deployList_.top + deployList_.visibleRows())
                        deployList_.top = deployList_.selected - deployList_.visibleRows() + 1;
                }
        };
        app_.push(makeArmy(app_, std::move(a)));
    }

    // --- Tactical orders -----------------------------------------------------

    void select(int u) {
        if (group_) {
            if (std::find(selection_.begin(), selection_.end(), u) != selection_.end()) return;
            if (selection_.size() >= 64) {
                showMessage(text(app_, 580, "Selection is full"), 5000);
                app_.sound("INVALIDE.WAV");
                return;
            }
            selection_.push_back(u);
        } else {
            selection_ = {u};
        }
        fire_ = false;
        plannedTarget_.reset();
        app_.sound("SELECT.WAV");
        centerOn(session_->state().unit(u).pos);
        if (const auto it = destination_.find(u); it != destination_.end()) plan(it->second);
    }

    void plan(game::Point target) {
        const game::BattleState& s = session_->state();
        preview_ = game::planGroupMove(s, selection_, target);
        if (preview_.empty()) {
            showMessage(text(app_, 577, "No path to that tile"), 5000);
            plannedTarget_.reset();
            for (const int u : selection_) destination_.erase(u);
            return;
        }
        plannedTarget_ = target;
        for (const game::PlannedMove& m : preview_) destination_[m.unit] = m.target;
    }

    void tacticClick(game::Point tile) {
        const game::BattleState& s = session_->state();
        auto shoot = [&] {
            if (selection_.empty()) return;
            const std::string refused = session_->send(game::shootOrder(s, selection_, tile, app_.settings().autofire));
            if (!refused.empty()) {
                showMessage(refused, 5000);
                app_.sound("INVALIDE.WAV");
            }
            preview_.clear();
            plannedTarget_.reset();
        };
        if (fire_) return shoot();
        if (const int u = s.unitAt(tile); u >= 0) {
            if (ownUnit(u)) {
                if (selection_.size() == 1 && selection_.front() == u) return; // turning a unit is not implemented
                return select(u);
            }
            if (!selection_.empty() && game::isEnemyTarget(s, tile)) return shoot();
        }
        if (!selection_.empty()) {
            if (s.buildingAt(tile) >= 0 && game::isEnemyTarget(s, tile)) return shoot();
            if (const int m = s.mineAt(tile); m >= 0 && !s.mine(m).hidden[kHuman]) return shoot();
            if (s.wallAt(tile) >= 0) return shoot();
            // Two-click movement (3.3).
            if (plannedTarget_ && plannedTarget_->x == tile.x && plannedTarget_->y == tile.y) {
                game::MoveRequest r{s.currentPlayer, {}};
                for (const game::PlannedMove& m : preview_) {
                    game::UnitOrder o{m.unit, m.path};
                    o.path.resize(static_cast<std::size_t>(m.affordable));
                    if (!o.path.empty()) r.orders.push_back(std::move(o));
                }
                const std::string refused = session_->send(r);
                if (!refused.empty()) showMessage(refused, 5000);
                preview_.clear();
                plannedTarget_.reset();
                fire_ = false;
            } else {
                plan(tile);
            }
        }
    }

    void toggleGroup() {
        group_ = !group_;
        if (!group_ && selection_.size() > 1) selection_.resize(1);
    }

    void centerSelection() {
        if (!selection_.empty()) centerOn(session_->state().unit(selection_.front()).pos);
    }

    void moveAll() {
        const game::BattleState& s = session_->state();
        game::MoveRequest r{s.currentPlayer, {}};
        for (const auto& [u, target] : destination_) {
            if (!ownUnit(u) || s.unit(u).availableAp() <= 3) continue;
            if (s.unit(u).pos.x == target.x && s.unit(u).pos.y == target.y) continue;
            const game::PlannedMove m = game::planMove(s, u, target);
            if (m.affordable == 0) continue;
            game::UnitOrder o{u, m.path};
            o.path.resize(static_cast<std::size_t>(m.affordable));
            r.orders.push_back(std::move(o));
            if (r.orders.size() >= 64) break;
        }
        if (r.orders.empty()) {
            showMessage(text(app_, 579, "Nothing to move"), 5000);
            return;
        }
        session_->send(r);
        preview_.clear();
        plannedTarget_.reset();
    }

    // Unit cycling by id (3.8).
    void cycle(int direction) {
        const game::BattleState& s = session_->state();
        const int current = selection_.empty() ? -1 : selection_.front();
        if (current >= 0 && s.unit(current).availableAp() == 0) done_.insert(current);
        std::vector<int> candidates;
        for (int i = 0; i < static_cast<int>(s.units().size()); ++i)
            if (ownUnit(i) && !done_.count(i) && i != current) candidates.push_back(i);
        if (candidates.empty()) return;
        std::sort(candidates.begin(), candidates.end(), [&](int a, int b) { return s.unit(a).id < s.unit(b).id; });
        const int currentId = current >= 0 ? s.unit(current).id : (direction > 0 ? -1 : 1 << 30);
        int pick = -1;
        if (direction > 0) {
            for (const int c : candidates)
                if (s.unit(c).id > currentId) {
                    pick = c;
                    break;
                }
            if (pick < 0) pick = candidates.front();
        } else {
            for (auto it = candidates.rbegin(); it != candidates.rend(); ++it)
                if (s.unit(*it).id < currentId) {
                    pick = *it;
                    break;
                }
            if (pick < 0) pick = candidates.back();
        }
        const bool group = group_;
        group_ = false;
        select(pick);
        group_ = group;
    }

    void guard() {
        if (selection_.empty()) return;
        done_.insert(selection_.front());
        cycle(1);
    }

    void endTurn() {
        selection_.clear();
        preview_.clear();
        plannedTarget_.reset();
        fire_ = false;
        session_->send(game::EndTurnRequest{session_->state().currentPlayer});
    }

    // --- Automated units (tactical-interface.md 6.2, automated-units.md 3) --

    void openAutomated() {
        const game::BattleState& s = session_->state();
        autoTypes_.clear();
        for (const int id : s.usableAutomated(kHuman))
            if (const auto it = s.rules().automated.find(id); it != s.rules().automated.end()) autoTypes_.push_back(&it->second);
        if (autoTypes_.empty()) {
            showMessage(text(app_, 71, "You don't have any automated units available"), 5000);
            return;
        }
        fire_ = false;
        preview_.clear();
        plannedTarget_.reset();
        autoList_.rows.clear();
        for (const game::AutomatedType* t : autoTypes_) autoList_.rows.push_back(t->name);
        autoList_.top = 0;
        automated_ = true;
        selectAutomated(0); // opening the view selects the first row
    }

    void closeAutomated() {
        automated_ = false;
        autoPoints_.clear();
    }

    const game::AutomatedType* selectedAutomated() const {
        const int row = autoList_.selected;
        return row >= 0 && row < static_cast<int>(autoTypes_.size()) ? autoTypes_[static_cast<std::size_t>(row)] : nullptr;
    }

    void selectAutomated(int row) {
        autoList_.selected = row;
        autoPoints_.clear();
        const game::AutomatedType* t = selectedAutomated();
        if (!t) return;
        if (t->id != lastAutomated_) showMessage(t->explain, 10000);
        lastAutomated_ = t->id;
    }

    void automatedClick(game::Point tile) {
        const game::AutomatedType* t = selectedAutomated();
        if (!t) return;
        const game::BattleState& s = session_->state();
        auto refuse = [&](int id, const char* fallback) {
            showMessage(text(app_, id, fallback), 5000);
            app_.sound("INVALIDE.WAV");
        };
        if (s.deployBudget[kHuman] < t->cost) return refuse(448, "Need more tactical resource");
        const game::Point* source = autoPoints_.empty() ? nullptr : &autoPoints_.front();
        if (!game::automatedTargetOk(s, kHuman, *t, tile, true, source)) return refuse(320, "Invalid target");
        autoPoints_.push_back(tile);
        if (static_cast<int>(autoPoints_.size()) < game::automatedPoints(*t)) {
            showMessage(t->secondExplain.empty() ? "Click on destination" : t->secondExplain, 10000);
            return;
        }
        if (s.deployBudget[kHuman] < t->cost) {
            autoPoints_.clear();
            return refuse(448, "Need more tactical resource");
        }
        const game::AutomatedRequest request{kHuman, t->id, autoPoints_};
        closeAutomated();
        const std::string refused = session_->send(request);
        if (!refused.empty()) showMessage(refused, 5000);
    }

    void drawAutomatedList(render::IndexedSurface& s, const ui::UiContext& ui) {
        const render::Font& f = *ui.standard;
        const ui::Rect area = autoList_.rect;
        for (int i = 0; i < autoList_.visibleRows(); ++i) {
            const int row = autoList_.top + i;
            if (row >= static_cast<int>(autoTypes_.size())) break;
            const game::AutomatedType* t = autoTypes_[static_cast<std::size_t>(row)];
            const ui::Rect r{area.x, area.y + i * 39, 118, 39};
            // The list icon is assumed to be <facingsImageID>999 like the units' *(?)*.
            if (const formats::DbiSprite* icon = app_.data().sprites.get(game::SpriteSet::ListBox, t->image + "999"))
                s.blit(*icon, r.x + 2, r.y + 2);
            f.draw(s, r.x + 50, r.y + 2, t->name, ui.white);
            f.draw(s, r.x + 50, r.y + 24, std::to_string(t->cost), ui.white);
            if (row == autoList_.selected) frameRect(s, {r.x + 1, r.y + 1, r.w - 2, r.h - 2}, ui.white);
        }
    }

    // Markers of the automated-units view: bridge sites, the transport's
    // source, the target cursor.
    void automatedMarkers(std::vector<game::Overlay>& markers, game::Point hover) {
        const game::AutomatedType* t = selectedAutomated();
        if (!t) return;
        const game::BattleState& s = session_->state();
        if (t->kind() == game::AutomatedCategory::Build)
            for (const game::Point p : s.bridgeSites)
                if (s.roadAt(p) < 0) marker(markers, p, game::bridgePlacementOk(s, p) ? "BRIDGE" : "BRIDG001", 13);
        const std::string race = t->image.empty() ? "H" : t->image.substr(0, 1);
        if (!autoPoints_.empty()) marker(markers, autoPoints_.front(), race + "TRANSPS");
        if (!s.inside(hover)) return;
        const game::Point* source = autoPoints_.empty() ? nullptr : &autoPoints_.front();
        const bool ok = game::automatedTargetOk(s, kHuman, *t, hover, true, source);
        if (ok && source) marker(markers, hover, race + "TRANSPD");
        marker(markers, hover, ok ? t->cursor : "CINVA");
    }

    // --- Dialogs -------------------------------------------------------------

    void openGoal() {
        dialog_ = Dialog::Goal;
        app_.sound("OBJECTIF.WAV");
        dialogButtons_ = {button({40 + 308, 40 + 205, 110, 17}, 634, "Continue", ui::kKeyEscape, [this] { dialog_ = Dialog::None; }),
                          button({40 + 298, 40 + 230, 19, 19}, 638, "", 0, [this] { markers_ = !markers_; }, true)};
    }

    void openMenu() {
        dialog_ = Dialog::Menu;
        const ui::Rect p = menuRect();
        auto quit = [this] { app_.reset(makeMainMenu(app_)); };
        dialogButtons_ = {button({p.x + 336, p.y + 340, 53, 17}, 653, "Back", ui::kKeyEscape, [this] { dialog_ = Dialog::None; }),
                          button({p.x + 197, p.y + 340, 53, 17}, 649, "Quit", 'q', quit)};
        ui::Button save = button({p.x + 132, p.y + 340, 53, 17}, 661, "Save", 's', nullptr);
        save.enabled = false;
        ui::Button restart = button({p.x + 10, p.y + 340, 110, 17}, 689, "Restart", 0, nullptr);
        restart.enabled = false;
        dialogButtons_.push_back(save);
        dialogButtons_.push_back(restart);
        // Sounds on/off and scroll speed (interface.md, in-game options).
        soundGroup_ = {};
        for (int i = 0; i < 2; ++i) soundGroup_.items.push_back(button({p.x + 36, p.y + 71 + 21 * i, 19, 19}, 665, "", 0, nullptr, true));
        soundGroup_.check(app_.settings().sound ? 0 : 1);
        soundGroup_.onChange = [this](int i) { app_.settings().sound = i == 0; };
        animGroup_ = {};
        for (int i = 0; i < 2; ++i) animGroup_.items.push_back(button({p.x + 36, p.y + 158 + 21 * i, 19, 19}, 665, "", 0, nullptr, true));
        animGroup_.check(app_.settings().moveAnimations ? 0 : 1);
        animGroup_.onChange = [this](int i) { app_.settings().moveAnimations = i == 0; };
        scrollGroup_ = {};
        for (int i = 0; i < 3; ++i) scrollGroup_.items.push_back(button({p.x + 36, p.y + 252 + 21 * i, 19, 19}, 665, "", 0, nullptr, true));
        scrollGroup_.check(app_.settings().scrollSpeed);
        scrollGroup_.onChange = [this](int i) { app_.settings().scrollSpeed = i; };
    }

    ui::Rect menuRect() const { return {(viewW_ - 403) / 2, (viewH_ - 371) / 2, 403, 371}; }

    void handleDialog(const ui::InputEvent& e) {
        if (dialog_ == Dialog::Goal) dialogButtons_[1].checked = markers_;
        for (ui::Button& b : dialogButtons_)
            if (b.handle(e)) return;
        if (dialog_ == Dialog::Menu) {
            if (soundGroup_.handle(e) || animGroup_.handle(e)) return;
            scrollGroup_.handle(e);
        }
    }

    void drawGoal(render::IndexedSurface& s, const ui::UiContext& ui) {
        const ui::Rect dlg{40, 40, 448, 268};
        ui::drawFrame(s, ui, 633, dlg);
        const render::Font& f = *ui.standard;
        const ui::Rect area{dlg.x + 20, dlg.y + 20, dlg.w - 40, dlg.h - 50};
        f.drawIn(s, area.x, area.y, area.w, f.lineHeight(), entry_.title, ui.white);
        f.draw(s, area.x, area.y + 30, entry_.province, ui.white);
        // Description: part1, blank line, part2, line break, part3.
        std::string d = entry_.description;
        std::vector<std::string> parts;
        for (std::size_t pos = 0;;) {
            const std::size_t sep = d.find("##", pos);
            parts.push_back(d.substr(pos, sep == std::string::npos ? std::string::npos : sep - pos));
            if (sep == std::string::npos) break;
            pos = sep + 2;
        }
        std::string shown = parts[0];
        if (parts.size() > 1) shown += "\n\n" + parts[1];
        if (parts.size() > 2) shown += "\n" + parts[2];
        f.drawWrapped(s, area.x, area.y + 60, area.w - 120, area.h - 60, shown, ui.white);
        dialogButtons_[1].checked = markers_;
        for (const ui::Button& b : dialogButtons_) b.draw(s, ui);
    }

    void drawMenu(render::IndexedSurface& s, const ui::UiContext& ui) {
        ui::drawFrame(s, ui, 648, menuRect());
        for (const ui::Button& b : dialogButtons_) b.draw(s, ui);
        soundGroup_.draw(s, ui);
        animGroup_.draw(s, ui);
        scrollGroup_.draw(s, ui);
    }

    // --- Tactical map screen (tactical-interface.md 7) and encyclopedia --------

    void openMap(bool first) {
        TacticalMapConfig m;
        m.state = &session_->state();
        m.localPlayer = kHuman;
        m.first = first;
        m.resolution = large_ ? 1 : 0;
        m.scenario = entry_.database;
        m.playerName = [this](int slot) { return playerName(slot); };
        m.clickable = [this] { return mode() != Mode::Wait; }; // your turn or your deployment
        m.viewport = [this](game::Point& tl, int& span) {
            tl = tileAt(0, 0);
            span = std::max(viewW_ / 54, viewH_ / 28);
        };
        m.centre = [this](game::Point t) { centerOn(t); };
        if (first) m.onClose = [this] { openGoal(); };
        app_.push(makeTacticalMap(app_, std::move(m)));
    }

    // The emblem (encyclopedia.md 3): the lead selected unit's type in the
    // tactical view, the selected list type in the deployment view.
    void openEncyclopediaHere() {
        EncyclopediaRequest r;
        r.levels = session_->state().techLevels[kHuman];
        const Mode mode = this->mode();
        int type = -1;
        if (mode == Mode::Tactic && !selection_.empty()) type = session_->state().unit(selection_.front()).type->typeId;
        if (mode == Mode::Deploy && deployList_.selected >= 0 && deployList_.selected < static_cast<int>(deployTypes_.size()))
            type = deployTypes_[static_cast<std::size_t>(deployList_.selected)]->typeId;
        if (type >= 0) {
            r.kind = EncyclopediaRequest::Kind::Unit;
            r.type = type;
        }
        if (mode == Mode::Automated) {
            if (const game::AutomatedType* t = selectedAutomated()) {
                r.kind = EncyclopediaRequest::Kind::Automated;
                r.type = t->id;
            }
        }
        openEncyclopedia(app_, std::move(r));
    }

    // --- Map -----------------------------------------------------------------

    // Color value of a player slot (map-objects.md, "Color variant").
    int colors(int owner) const { return session_->animations().colors()(owner); }

    const formats::DbiSprite* symbol(const std::string& name) {
        return app_.data().sprites.get(game::SpriteSet::Symbols, name);
    }

    // A SYMBOLS.DBI marker on a tile, drawn as a feature of `layer`
    // (tactical-interface.md 2.10); kDrawOnTop draws it after all cells.
    void marker(std::vector<game::Overlay>& out, game::Point t, const std::string& name, int layer = kCursorLayer, int mode = 0) {
        const game::Point o = game::iso::tileOrigin(t.x, t.y, session_->scene().terrain().size);
        if (const formats::DbiSprite* sp = symbol(name)) out.push_back({t.x, t.y, o.x, o.y, sp, layer, mode});
    }
    static constexpr int kCursorLayer = 8;

    void drawMap() {
        const game::BattleState& s = session_->state();
        const Mode mode = this->mode();
        std::vector<game::Overlay> markers;
        if (markers_) {
            for (const game::Point z : s.objective.zone) marker(markers, z, "OBJT000", 2);
            const std::string obj = s.objective.kind == 0 || s.objective.kind == 4 ? "OBJT001" : "OBJT002";
            for (const int id : s.objective.ids) {
                for (const game::BattleUnit& u : s.units())
                    if (u.alive && u.id == id) marker(markers, u.pos, obj, 14, game::kDrawOnTop);
                for (const game::BattleBuilding& b : s.buildings())
                    if (b.alive && b.id == id) marker(markers, b.pos, obj, 14, game::kDrawOnTop);
            }
        }
        const game::Point hover = mouseX_ >= 0 && mouseX_ < viewW_ && mouseY_ < viewH_ ? tileAt(mouseX_, mouseY_) : game::Point{-1, -1};
        if (mode == Mode::Deploy) {
            for (const game::Point z : s.deployZone[kHuman]) {
                int mask = 0;
                if (s.inDeployZone(kHuman, {z.x - 1, z.y})) mask |= 8;
                if (s.inDeployZone(kHuman, {z.x + 1, z.y})) mask |= 4;
                if (s.inDeployZone(kHuman, {z.x, z.y - 1})) mask |= 2;
                if (s.inDeployZone(kHuman, {z.x, z.y + 1})) mask |= 1;
                // In the local player's own color (map-objects.md).
                marker(markers, z, "DEPL" + std::to_string(colors(kHuman)) + twoDigits(deployMarker(mask)), 3);
            }
            if (s.inside(hover) && s.inDeployZone(kHuman, hover)) {
                const int u = s.unitAt(hover);
                if (u < 0) marker(markers, hover, "CDEPL");
                else if (std::find(deployed_.begin(), deployed_.end(), u) != deployed_.end()) marker(markers, hover, "CRECA");
            }
        }
        if (mode == Mode::Automated) automatedMarkers(markers, hover);
        if (mode == Mode::Tactic) {
            for (const game::PlannedMove& m : preview_) {
                const std::vector<std::string> names = game::previewSprites(m);
                // The crossable-only markers stay visible over what they cross.
                for (std::size_t i = 0; i < names.size(); ++i)
                    marker(markers, m.path[i].pos, names[i], 7, names[i] == "PATH0002" || names[i] == "PATH0003" ? game::kDrawOnTop : 0);
            }
            if (s.inside(hover)) {
                const std::string cursor = tileCursor(hover);
                if (!cursor.empty()) marker(markers, hover, cursor);
            }
        }
        if (mode != Mode::Deploy)
            for (const int u : selection_)
                if (s.unit(u).alive)
                    marker(markers, s.unit(u).pos, "SU00" + std::to_string(colors(s.unit(u).owner)), 17, game::kDrawOnTop);
        for (const int b : s.techHeld)
            if (b >= 0 && b < static_cast<int>(s.buildings().size()) && s.building(b).alive)
                marker(markers, s.building(b).pos, "LILBULB", 16, game::kDrawOnTop);
        game::SceneExtras extras = session_->animations().extras();
        extras.overlays.insert(extras.overlays.end(), markers.begin(), markers.end());
        viewport_.fill(0);
        session_->scene().draw(viewport_, {static_cast<int>(camX_), static_cast<int>(camY_), viewW_, viewH_},
                               app_.data().sprites, true, &extras);
    }

    // Tile cursor sprite (3.4).
    std::string tileCursor(game::Point t) {
        const game::BattleState& s = session_->state();
        auto shotCursor = [&] {
            const game::ShotCheck c = game::checkShot(s, selection_.front(), t, true);
            if (c.ok()) return std::string("CTARG");
            if (c.error == game::ShotError::InvalidTarget) return std::string("CINVA");
            return std::string("CMVSH");
        };
        if (!selection_.empty() && fire_) return shotCursor();
        const int u = s.unitAt(t);
        if (u >= 0 && ownUnit(u)) return "CSELC";
        if (selection_.empty()) return "";
        if (u >= 0 && game::isEnemyTarget(s, t)) return shotCursor();
        if (s.buildingAt(t) >= 0 && game::isEnemyTarget(s, t)) return shotCursor();
        if (const int m = s.mineAt(t); m >= 0 && !s.mine(m).hidden[kHuman]) return shotCursor();
        if (s.wallAt(t) >= 0) return shotCursor();
        return game::findPath(s, selection_.front(), t).empty() ? "" : "CMOVE";
    }

    // --- Panel pieces --------------------------------------------------------

    ui::Rect minimapRect() const { return {viewW_ + 12, 49, 112, 112}; }

    void buildMinimapTerrain() {
        const game::TerrainMap& t = session_->scene().terrain();
        const int n = t.size;
        const double s = 112.0 / n;
        const int b = static_cast<int>(std::ceil(s));
        std::vector<game::Rgb> rgb(112 * 112);
        for (int y = 0; y < n; ++y)
            for (int x = 0; x < n; ++x) {
                const game::TerrainType* tt = app_.data().rules.terrainType(t.at(x, y));
                const game::Rgb c = tt ? tt->color : game::Rgb{};
                const int x0 = static_cast<int>(x * s), y0 = static_cast<int>(y * s);
                for (int yy = y0; yy < std::min(112, y0 + b + 1); ++yy)
                    for (int xx = x0; xx < std::min(112, x0 + b + 1); ++xx) rgb[static_cast<std::size_t>(yy * 112 + xx)] = c;
            }
        // One smoothing pass with the 8 neighbours (2.6).
        minimap_.assign(112 * 112, 0);
        for (int y = 0; y < 112; ++y)
            for (int x = 0; x < 112; ++x) {
                int r = 0, g = 0, bl = 0, count = 0;
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int xx = x + dx, yy = y + dy;
                        if (xx < 0 || yy < 0 || xx >= 112 || yy >= 112) continue;
                        const game::Rgb& c = rgb[static_cast<std::size_t>(yy * 112 + xx)];
                        r += c.r;
                        g += c.g;
                        bl += c.b;
                        ++count;
                    }
                minimap_[static_cast<std::size_t>(y * 112 + x)] = colors_(r / count / 4 * 4, g / count / 4 * 4, bl / count / 4 * 4);
            }
    }

    std::uint8_t objectColor(int type, int owner, int race) {
        // Players 1-4 by colour, the AI by race (2.6).
        static constexpr int kRaceRow[4] = {0, 5, 7, 6};
        const int row = owner >= 1 && owner <= 4 ? owner : kRaceRow[std::clamp(race, 0, 3)];
        const auto it = app_.data().rules.colors.find({type, row});
        const game::Rgb c = it == app_.data().rules.colors.end() ? game::Rgb{255, 255, 255} : it->second;
        return colors_(c.r, c.g, c.b);
    }

    void drawMinimap(render::IndexedSurface& s, const ui::UiContext& ui) {
        const ui::Rect r = minimapRect();
        for (int y = 0; y < 112; ++y) std::copy_n(minimap_.begin() + y * 112, 112, s.row(r.y + y) + r.x);
        const game::BattleState& st = session_->state();
        const int n = st.size();
        const double sc = 112.0 / n;
        const int b = static_cast<int>(std::ceil(sc));
        auto square = [&](game::Point p, int side, std::uint8_t color) {
            fillRect(s, {r.x + static_cast<int>(p.x * sc), r.y + static_cast<int>(p.y * sc), std::max(1, side), std::max(1, side)}, color);
        };
        for (const game::BattleBuilding& bd : st.buildings())
            if (bd.alive) {
                const int row = bd.type->techReward > 0 && bd.owner == 0 ? 8 : bd.type->category == 1 ? 9 : -1;
                std::uint8_t c = objectColor(0, bd.owner, bd.type->race);
                if (row >= 0) {
                    const auto it = app_.data().rules.colors.find({0, row});
                    if (it != app_.data().rules.colors.end()) c = colors_(it->second.r, it->second.g, it->second.b);
                }
                for (int dy = 0; dy < bd.type->sizeY; ++dy)
                    for (int dx = 0; dx < bd.type->sizeX; ++dx) square({bd.pos.x + dx, bd.pos.y + dy}, b + 2, c);
            }
        const auto road = app_.data().rules.colors.find({2, 0});
        const auto wall = app_.data().rules.colors.find({3, 0});
        if (road != app_.data().rules.colors.end())
            for (const game::BattleRoad& rd : st.roads())
                if (rd.alive) square(rd.pos, b + 1, colors_(road->second.r, road->second.g, road->second.b));
        if (wall != app_.data().rules.colors.end())
            for (const game::BattleWall& w : st.walls())
                if (w.alive) square(w.pos, b + 1, colors_(wall->second.r, wall->second.g, wall->second.b));
        for (const game::BattleUnit& u : st.units())
            if (u.alive) square(u.pos, b, objectColor(1, u.owner, u.type->race));
        // The visible part: from the viewport's top-left tile, a square of
        // max(view_w / 54, view_h / 28) tiles.
        const game::Point tl = tileAt(0, 0);
        const int span = std::max(viewW_ / 54, viewH_ / 28);
        ui::Rect v{r.x + static_cast<int>(tl.x * sc), r.y + static_cast<int>(tl.y * sc), static_cast<int>(span * sc),
                   static_cast<int>(span * sc)};
        v.x = std::clamp(v.x, r.x, r.x + r.w - 2);
        v.y = std::clamp(v.y, r.y, r.y + r.h - 2);
        v.w = std::min(v.w, r.x + r.w - v.x);
        v.h = std::min(v.h, r.y + r.h - v.y);
        frameRect(s, v, ui.black);
        frameRect(s, {v.x + 1, v.y + 1, v.w - 2, v.h - 2}, ui.white);
    }

    void drawInfoCard(render::IndexedSurface& s, const ui::UiContext& ui, game::Point t) {
        const ui::Rect r = minimapRect();
        fillRect(s, r, ui.black);
        const game::BattleState& st = session_->state();
        const render::Font& f = *ui.standard;
        const int h = f.lineHeight();
        auto icon = [&](const std::string& code) {
            if (const formats::DbiSprite* sp = app_.data().sprites.get(game::SpriteSet::ListBox, code + "999"))
                s.blit(*sp, r.x + 3, r.y + 3);
        };
        if (const int u = st.unitAt(t); u >= 0) {
            const game::BattleUnit& unit = st.unit(u);
            icon(unit.type->image);
            f.draw(s, r.x + 44, r.y + 3, unit.type->name, ui.white);
            f.draw(s, r.x + 44, r.y + 3 + h, fmt(app_, 640, "AR %1", {std::to_string(unit.armor)}), ui.white);
            f.draw(s, r.x + 44, r.y + 3 + 2 * h, fmt(app_, 641, "AP %1", {std::to_string(unit.availableAp())}), ui.white);
            f.draw(s, r.x + 3, r.y + 3 + 3 * h, fmt(app_, 642, "RANGE %1", {std::to_string(unit.type->range)}), ui.white);
            const bool ow = unit.availableAp() >= unit.type->shotAp && unit.type->hasWeapon;
            f.draw(s, r.x + 3, r.y + 3 + 4 * h, fmt(app_, 643, "Reaction fire: %1", {ow ? text(app_, 644, "YES") : text(app_, 645, "NO")}),
                   ui.white);
            f.draw(s, r.x + 3, r.y + 3 + 5 * h, playerName(unit.owner), ui.white);
        } else if (const int b = st.buildingAt(t); b >= 0) {
            const game::BattleBuilding& bd = st.building(b);
            icon(bd.type->image);
            f.draw(s, r.x + 44, r.y + 3, bd.type->name, ui.white);
            f.draw(s, r.x + 44, r.y + 3 + h, fmt(app_, 640, "AR %1", {std::to_string(bd.armor)}), ui.white);
            f.draw(s, r.x + 3, r.y + 3 + 3 * h, playerName(bd.owner), ui.white);
            if (bd.type->techReward > 0)
                f.draw(s, r.x + 3, r.y + 3 + 4 * h, fmt(app_, 646, "TECH %1", {std::to_string(bd.type->techReward)}), ui.white);
        } else if (const int w = st.wallAt(t); w >= 0) {
            f.draw(s, r.x + 44, r.y + 3 + h, fmt(app_, 640, "AR %1", {std::to_string(st.wall(w).armor)}), ui.white);
            f.draw(s, r.x + 3, r.y + 3 + 3 * h, playerName(st.wall(w).owner), ui.white);
        } else {
            drawMinimap(s, ui);
        }
    }

    void drawResource(render::IndexedSurface& s, const ui::UiContext& ui) {
        const ui::Rect r{viewW_ + 64, 26, 40, 14};
        fillRect(s, r, ui.black);
        // Deployment: the remaining budget; later the tactical resource (2.4).
        const int value = session_->state().deployBudget[kHuman];
        ui.standard->drawIn(s, r.x, r.y - 1, r.w, r.h + 2, std::to_string(std::clamp(value, 0, 99999)), ui.white);
    }

    void drawUnitInfo(render::IndexedSurface& s, const ui::UiContext& ui) {
        const ui::Rect r{viewW_ + 8, 200, 112, 38};
        fillRect(s, r, ui.black);
        const render::Font& f = *ui.standard;
        const game::BattleState& st = session_->state();
        if (selection_.empty()) {
            f.drawIn(s, r.x, r.y, r.w, r.h, text(app_, 84, "No selection"), ui.white);
            return;
        }
        if (selection_.size() > 1) {
            f.drawIn(s, r.x, r.y, r.w, r.h, fmt(app_, 83, "%1 units", {std::to_string(selection_.size())}), ui.white);
            return;
        }
        const game::BattleUnit& u = st.unit(selection_.front());
        int x = r.x;
        if (const formats::DbiSprite* icon = app_.data().sprites.get(game::SpriteSet::ListBox, u.type->image + "999")) {
            s.blit(*icon, r.x, r.y);
            x += icon->width + 5;
        }
        const int lh = f.lineHeight() - 2;
        f.draw(s, x, r.y, u.type->name, ui.white);
        f.draw(s, x, r.y + lh,
               fmt(app_, 673, "AP %1 / %2", {std::to_string(u.availableAp()), std::to_string(u.type->actionPoints)}), ui.white);
        f.draw(s, x, r.y + 2 * lh, fmt(app_, 672, "AR %1 / %2", {std::to_string(u.armor), std::to_string(u.type->armor)}),
               ui.white);
    }

    void drawTurnCounter(render::IndexedSurface& s, const ui::UiContext& ui) {
        const game::BattleState& st = session_->state();
        if (st.turnLimit <= 0) return;
        const ui::Rect r{viewW_ + 95, 456, 34, 19};
        ui.standard->drawIn(s, r.x, r.y, r.w, r.h, std::to_string(std::max(0, st.turnLimit - st.turn + 1)), ui.white,
                            render::Align::Right);
    }

    void drawPlayerList(render::IndexedSurface& s, const ui::UiContext& ui) {
        std::vector<int> rows;
        for (const int p : session_->state().players)
            if (p != 0) rows.push_back(p);
        if (session_->state().computerPlayer) rows.push_back(0);
        for (std::size_t i = 0; i < rows.size() && i < 5; ++i) {
            const int p = rows[i];
            ui::Button lamp;
            lamp.rect = {viewW_ + 6, 205 + 28 * static_cast<int>(i), 19, 19};
            lamp.firstFrame = 46 + 6 * std::clamp(p, 0, 4);
            lamp.toggle = true;
            lamp.checked = session_->displayedPlayer() == p;
            lamp.draw(s, ui);
            ui.standard->drawIn(s, lamp.rect.x + 25, lamp.rect.y + 1, 96, 16, playerName(p), ui.white, render::Align::Left);
        }
    }

    void drawDeployList(render::IndexedSurface& s, const ui::UiContext& ui) {
        const render::Font& f = *ui.standard;
        const ui::Rect area = deployList_.rect;
        fillRect(s, area, ui.black);
        for (int i = 0; i < deployList_.visibleRows(); ++i) {
            const int row = deployList_.top + i;
            if (row >= static_cast<int>(deployTypes_.size())) break;
            const game::UnitType* t = deployTypes_[static_cast<std::size_t>(row)];
            const ui::Rect r{area.x, area.y + i * 39, 120, 39};
            if (const formats::DbiSprite* icon = app_.data().sprites.get(game::SpriteSet::ListBox, t->image + "999"))
                s.blit(*icon, r.x + 2, r.y + 1);
            f.draw(s, r.x + 50, r.y + 2, t->name, ui.white);
            f.draw(s, r.x + 50, r.y + 21, std::to_string(t->cost), ui.white);
            if (row == deployList_.selected) frameRect(s, {r.x + 1, r.y + 1, r.w - 2, r.h - 2}, ui.white);
        }
    }

    void drawStrip(render::IndexedSurface& s, const ui::UiContext& ui) {
        const ui::Rect strip{0, viewH_, viewW_, height_ - viewH_};
        ui::drawFrame(s, ui, 498, strip);
        const render::Font& f = *ui.standard;
        const int secs = clockMs_ / 1000;
        char clock[16];
        std::snprintf(clock, sizeof clock, "%02d:%02d", std::min(99, secs / 60), secs % 60);
        f.drawIn(s, strip.x + 8, strip.y + 6, 40, 19, clock, ui.white, render::Align::Left);
        const ui::Rect msg{strip.x + 64, strip.y + 5, viewW_ - 135 - 64, 20};
        fillRect(s, msg, ui.black);
        if (messageMs_ > 0) f.drawIn(s, msg.x + 2, msg.y, msg.w - 4, msg.h, message_, ui.white, render::Align::Left);
    }

    // --- State ---------------------------------------------------------------

    BattleConfig config_;
    ScenarioEntry entry_;
    std::unique_ptr<game::BattleSession> session_;
    ColorMatcher colors_;
    bool large_ = false;
    int width_ = 640, height_ = 480, viewW_ = 504, viewH_ = 448;
    render::IndexedSurface viewport_;
    float camX_ = 0, camY_ = 0;
    int mouseX_ = -1, mouseY_ = -1;
    int scrollAcc_ = 0, tickAcc_ = 0;
    int clockMs_ = 0;
    std::string message_;
    int messageMs_ = 0;
    Mode lastMode_ = Mode::Deploy;
    Dialog dialog_ = Dialog::None;
    bool markers_ = true;
    bool reinforcing_ = false;
    std::size_t techHeld_ = 0;
    std::map<int, int> armyLevels_;
    bool firstMap_ = false;
    std::vector<ui::Button> deployButtons_, tacticButtons_, waitButtons_, dialogButtons_, autoButtons_;
    ui::RadioGroup soundGroup_, animGroup_, scrollGroup_;
    ui::ListBox deployList_;
    std::vector<const game::UnitType*> deployTypes_;
    std::vector<int> deployed_;
    std::vector<std::uint8_t> minimap_;
    std::optional<game::Point> infoTile_;
    std::vector<int> selection_;
    bool group_ = false, fire_ = false, tab_ = false;
    std::vector<game::PlannedMove> preview_;
    std::optional<game::Point> plannedTarget_;
    std::map<int, game::Point> destination_; // remembered destinations (MOVE ALL)
    std::set<int> done_;
    // Automated units view.
    bool automated_ = false;
    ui::ListBox autoList_;
    std::vector<const game::AutomatedType*> autoTypes_;
    std::vector<game::Point> autoPoints_; // target points given so far
    int lastAutomated_ = -1;              // the type selected last (explanation text)
};

} // namespace

std::unique_ptr<Screen> makeBattle(App& app, BattleConfig config) {
    auto session = game::BattleSession::load(config.entry.database, app.data().rules, app.data().clips.get(),
                                             static_cast<std::uint32_t>(std::random_device{}()), config.options,
                                             config.prepare);
    return std::make_unique<BattleScreen>(app, std::move(config), std::move(session));
}

std::unique_ptr<Screen> makeBattle(App& app, const ScenarioEntry& entry) {
    // Skirmish, single player: the human plays slot 1; units of the other
    // human slots join it *(OpenDay choice)*, and the budget is the catalog's
    // One Off Deployment (share 100 % for one player, battle-flow.md 1.2).
    BattleConfig config;
    config.entry = entry;
    config.options.aiDamage = false; // only campaign battles scale AI damage (campaign.md 4.1)
    config.prepare = [entry](game::BattleState& s) {
        for (std::size_t i = 0; i < s.units().size(); ++i) {
            game::BattleUnit& u = s.unit(static_cast<int>(i));
            if (u.owner >= 2) u.owner = kHuman;
        }
        for (std::size_t i = 0; i < s.buildings().size(); ++i) {
            game::BattleBuilding& b = s.building(static_cast<int>(i));
            if (b.owner >= 2) b.owner = kHuman;
        }
        s.players.clear();
        s.players = {0, kHuman};
        s.deployBudget[kHuman] = entry.budget;
        std::erase_if(s.automated[kHuman], [&](int id) {
            const auto it = s.rules().automated.find(id);
            return it == s.rules().automated.end() || it->second.race != 0;
        });
        s.deploying = !s.deployZone[kHuman].empty();
    };
    return makeBattle(app, std::move(config));
}

} // namespace openday::app
