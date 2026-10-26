// The OpenDay game program: shared resources and the stack of screens.
#pragma once

#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "app/updater.hpp"
#include "formats/dbi.hpp"
#include "formats/palcycle.hpp"
#include "formats/palette.hpp"
#include "map/map_renderer.hpp"
#include "map/rules.hpp"
#include "platform/audio.hpp"
#include "render/animation.hpp"
#include "render/font.hpp"
#include "render/indexed_surface.hpp"
#include "render/smk_frames.hpp"
#include "ui/atlas.hpp"
#include "ui/widgets.hpp"

namespace openday::game {
struct EncyclopediaData;
}

namespace openday::app {

namespace fs = std::filesystem;

// One entry of the skirmish catalog (SKIRMISH.MDB, data-model.md).
struct ScenarioEntry {
    std::string title, province, description;
    int budget = 0; // One Off Deployment
    int size = 0;
    fs::path database; // converted scenario database
};

// Everything loaded from the game installation.
struct GameData {
    fs::path gameDir, dataDir;
    fs::path global; // converted GLOBAL database
    game::GlobalRules rules;
    game::SpriteLibrary sprites;
    formats::Palette palette{};
    formats::PalCycle cycle;
    std::unique_ptr<render::AnimationCache> clips;
    std::map<int, std::string> strings; // language DLL string table
    std::vector<ScenarioEntry> skirmish;
    std::map<std::string, fs::path> scenarios; // lower-case name -> database
    std::map<int, fs::path> colonies;          // 1-4 -> converted C<n> database

    std::string text(int id) const;
    fs::path scenario(const std::string& name) const;
    // A file of the installation by its Windows name ("INTERF/PAL.BMP"),
    // found case-insensitively (formats/game_path.hpp).
    fs::path file(const std::string& relative) const;

    static GameData load(const fs::path& gameDir, const fs::path& dataDir);
};

class App;
class BattleControl;

class Screen {
public:
    explicit Screen(App& app) : app_(app) {}
    virtual ~Screen() = default;

    virtual void handle(const ui::InputEvent& e) = 0;
    virtual void update(int elapsedMs) { (void)elapsedMs; }
    virtual void draw(render::IndexedSurface& target) = 0;
    // Logical screen size the screen is drawn at.
    virtual int width() const { return 640; }
    virtual int height() const { return 480; }
    // A palette of its own (videos), or nullptr for the game palette.
    virtual const formats::Palette* palette() const { return nullptr; }
    virtual bool paletteCycling() const { return false; }
    // An overlay (a dialog) is drawn over the screen below it, which stays
    // visible; with `updatesBelow` the screen below keeps running too.
    virtual bool overlay() const { return false; }
    virtual bool updatesBelow() const { return false; }
    // The running battle, for the debug server (app/battle_control.hpp).
    virtual BattleControl* battleControl() { return nullptr; }
    // A resizable screen lays itself out for any size from the standard size
    // of the resolution up: with the resizable-window option for the window's
    // size, otherwise for the standard size (App::layout). Other screens keep
    // their size and are scaled to the window, or centred over a resizable
    // screen.
    virtual bool resizable() const { return false; }
    virtual void resize(int w, int h) { (void)w, (void)h; }

protected:
    App& app_;
};

struct Settings {
    int resolution = 0; // 0 = 640x480, 1 = 1024x768 for the game screens
    bool sound = true;
    bool music = true;
    bool moveAnimations = true;
    int scrollSpeed = 1;
    bool autofire = true;
    bool overwatch = true;
    bool cheats = false; // Tab + W / L, as the original with Cheats=1
    // OpenDay's own options (the OPENDAY button of the in-game options):
    bool edgeScroll = false;       // the map scrolls with the cursor at the screen edge
    bool smoothEdgeScroll = true; // edge scrolling glides pixel by pixel, not by tiles
    bool dragScroll = true;       // the right button drags the map
    bool keepViewOnSelect = true; // a click on a unit does not centre the view on it
    bool exactMinimap = true;     // minimap click and frame centred on the exact point
    bool minimapDrag = true;      // dragging on the minimap moves the view
    bool resizableWindow = true;  // the map screens grow with the window
    bool sharpText = true;        // text drawn anew at the window's resolution
    bool hdInterface = false;     // the interface art drawn again in high resolution
    bool checkUpdates = true;   // ask GitHub for a newer release on start
    std::string updateDeclined; // a release the user said "No" to (not offered again)
};

class App {
public:
    App(GameData& data, platform::AudioPlayer& audio);
    ~App();

    GameData& data() { return data_; }
    platform::AudioPlayer& audio() { return audio_; }
    const ui::UiContext& ui() const { return ui_; }
    Settings& settings() { return settings_; }
    // Drawing resources for a game resolution (0 = 640x480, 1 = 1024x768).
    const ui::UiContext& ui(int resolution);
    // HD interface: the key of an interface atlas element (frame, plus
    // 100000 in the 1024 x 768 atlas), or -1 for other pictures.
    int hdKey(const formats::DbiSprite& sprite) const;

    // Screen changes take effect after the current event or frame.
    void push(std::unique_ptr<Screen> screen);
    void replace(std::unique_ptr<Screen> screen);
    void pop();
    // Removes every screen and shows `screen` alone.
    void reset(std::unique_ptr<Screen> screen);
    void quit() { quit_ = true; }
    bool quitting() const { return quit_ || (screens_.empty() && pending_.empty()); }

    Screen* top() { return screens_.empty() ? nullptr : screens_.back().get(); }
    // The stack, bottom first.
    const std::vector<std::unique_ptr<Screen>>& screens() const { return screens_; }
    void applyChanges();
    // The window's drawing area in pixels: lays out the resizable screens
    // (Screen::resizable) for it.
    void layout(int pixelW, int pixelH);
    // The size of the picture drawn this frame: the size of the lowest screen
    // drawn. A screen smaller than the picture is drawn centred in it.
    int frameWidth() const;
    int frameHeight() const;
    // The scale of the picture in the window, when it is laid out for the
    // window; 0: the standard size, scaled to fit the window.
    float pixelScale() const;
    // Passes an event in picture coordinates to the top screen.
    void handle(ui::InputEvent e);
    // A window size the debug server asked for (in window coordinates), taken
    // by the main loop.
    int requestedWindowW = 0, requestedWindowH = 0;
    // Updates the top screen (and the screens below it that overlays keep
    // running) and draws the top screen over the screens below its overlays.
    void update(int elapsedMs);
    void draw(render::IndexedSurface& target);

    // Loaded on first use: the encyclopedia texts and pictures, and the
    // bitmap resources of the game program (LIBDAY.EXE), converted to the
    // game palette; `keyed`: pixels of the top-left pixel's color are
    // transparent. nullptr when missing.
    const game::EncyclopediaData& encyclopedia();
    const render::SmkFrames* encyclopediaPictures();
    const formats::DbiSprite* exeBitmap(int id, bool keyed = false);

    // The updater (docs/updates.md), or nullptr when update checks are off.
    Updater* updater() { return updater_.get(); }
    void setUpdater(std::unique_ptr<Updater> updater) { updater_ = std::move(updater); }
    // The update offer is made once per session.
    bool updateOffered = false;

    // Plays a sound from SOUNDS\ by file name.
    void sound(const std::string& file);
    // Starts a random music track, as the original does when a game starts.
    void startMusic();

private:
    const Screen* frameScreen() const;
    void layoutScreens();

    GameData& data_;
    platform::AudioPlayer& audio_;
    Settings settings_;
    ui::InterfaceAtlas atlas_, atlasLarge_;
    ui::UiContext uiLarge_;
    bool largeLoaded_ = false;
    std::unique_ptr<render::Font> standard_, large_;
    ui::UiContext ui_;
    std::vector<std::unique_ptr<Screen>> screens_;
    struct Change {
        enum class Kind { Push, Replace, Pop, Reset } kind;
        std::unique_ptr<Screen> screen;
    };
    std::vector<Change> pending_;
    bool quit_ = false;
    int windowW_ = 0, windowH_ = 0; // the window's drawing area in pixels
    int layoutW_ = 640, layoutH_ = 480;
    float scale_ = 0;
    render::IndexedSurface scratch_; // a centred screen's picture
    std::unique_ptr<game::EncyclopediaData> encyclopedia_;
    std::unique_ptr<render::SmkFrames> encyclopediaPictures_;
    bool picturesLoaded_ = false;
    std::map<int, std::vector<std::uint8_t>> exeBitmapData_;
    bool exeLoaded_ = false;
    std::map<std::pair<int, bool>, formats::DbiSprite> exeBitmaps_;
    std::unique_ptr<Updater> updater_;
};

// The interface atlas elements the screens use (frame and size, from
// docs/formats/INTERFACE-SMK.md and docs/mechanics/*interface.md).
std::vector<ui::AtlasElement> atlasElements(int resolution = 0);

// Nearest palette index to a color.
std::uint8_t nearestColor(const formats::Palette& p, int r, int g, int b);

// First screens.
std::unique_ptr<Screen> makeIntro(App& app);
std::unique_ptr<Screen> makeMainMenu(App& app);

} // namespace openday::app
