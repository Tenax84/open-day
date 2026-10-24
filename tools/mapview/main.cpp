// mapview: SDL3 viewer for the scenario maps: terrain, placed objects, and a
// hot-seat tactical battle on them (movement, combat, turns).
//
//   mapview [--game-dir DIR] --data DIR [--scenario NAME] [--scale N] [--mute]
//   mapview [--game-dir DIR] --data DIR --scenario NAME --screenshot OUT.png [--no-objects]
//           [--do ACTION]... [--ticks N]
//
// --game-dir  game installation (default: $OPENDAY_GAME_DIR); sprites are
//             read from its IMGS folder, animations from ANMSUNIT\ANMSUNIT.FF.
// --data      output folder of tools/mdb/mdb2sqlite.py (default:
//             $OPENDAY_DATA_DIR).
// --scenario  scenario database name without extension, e.g. s1-1-1.
// --do        (screenshot) send an order before the screenshot, as the player
//             whose turn it is: move:X,Y:TX,TY (unit at X,Y to TX,TY),
//             attack:X,Y:TX,TY (fire at TX,TY), end (end the turn).
//             Repeatable; each order waits for the previous animations.
// --ticks     (screenshot) animation ticks (1/15 s) to run after the last
//             order; default: until all animations have finished.
// --seed      random seed of the battle (default: 1).
//
// Controls: arrow keys / WASD or drag with the mouse to scroll, PageUp /
// PageDown for the previous / next scenario, 1-4 to set the zoom, C to toggle
// palette cycling (water, lights), O to toggle objects, M to toggle sound,
// Esc to quit.
// Battle (hot seat: every side is played from this window): left click
// selects a unit of the player whose turn it is, Shift + left click adds or
// removes units of a group; the path to the tile under the mouse is shown.
// Right click moves there, or fires at an enemy unit or building; Ctrl +
// right click fires at any tile. F toggles autofire, Space clears the
// selection, Enter ends the turn. The window title shows the turn, the
// player, the selected unit and the hit chance at the tile under the mouse.

#include "anim/battle_animations.hpp"
#include "anim/battle_view.hpp"
#include "battle/combat.hpp"
#include "battle/movement.hpp"
#include "battle/server.hpp"
#include "data/sqlite_db.hpp"
#include "formats/bmp_palette.hpp"
#include "formats/ff.hpp"
#include "formats/palcycle.hpp"
#include "formats/png_writer.hpp"
#include "map/map_objects.hpp"
#include "map/map_renderer.hpp"
#include "map/rules.hpp"
#include "map/terrain_map.hpp"
#include "render/animation.hpp"
#include "render/indexed_surface.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <functional>
#include <type_traits>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace openday;

namespace {

struct Options {
    fs::path gameDir;
    fs::path dataDir;
    std::string scenario;
    fs::path screenshot;
    std::vector<std::string> actions;
    int ticks = -1;
    int scale = 2;
    std::uint32_t seed = 1;
    bool objects = true;
    bool sound = true;
};

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

fs::path envPath(const char* name) {
    const char* v = std::getenv(name);
    return v ? fs::path(v) : fs::path();
}

// Scenario databases are the converted files that have a Map table.
std::vector<fs::path> findScenarios(const fs::path& dataDir) {
    std::vector<fs::path> found;
    for (const auto& e : fs::recursive_directory_iterator(dataDir)) {
        if (!e.is_regular_file() || lower(e.path().extension().string()) != ".sqlite") continue;
        if (data::SqliteDb(e.path()).hasTable("Map")) found.push_back(e.path());
    }
    std::sort(found.begin(), found.end(),
              [](const fs::path& a, const fs::path& b) { return lower(a.stem().string()) < lower(b.stem().string()); });
    return found;
}

fs::path findGlobalDb(const fs::path& dataDir) {
    for (const auto& e : fs::recursive_directory_iterator(dataDir))
        if (e.is_regular_file() && lower(e.path().filename().string()) == "global.sqlite") return e.path();
    throw data::DbError("GLOBAL.sqlite not found in " + dataDir.string());
}

// Main palette (INTERF\PAL.BMP) plus the color-cycling table (INTERF\PALCYCLE.PAL).
struct Palettes {
    formats::Palette base;
    formats::PalCycle cycle;

    formats::Palette at(std::uint64_t elapsedMs, bool cycling) const {
        return cycle.applyAt(base, cycling ? elapsedMs : 0);
    }
};

std::string describe(const game::BattleState& s, const game::Event& event);

// A loaded scenario with its battle: the server holds the authoritative
// state, the view the client's copy. Heap-allocated as a whole: the
// animations refer to the scene and the entities.
struct LoadedMap {
    std::string name;
    game::MapEntities entities;
    game::MapScene scene;
    game::BattleAnimations battle;
    game::BattleServer server;
    game::BattleView view;
    std::function<void(const game::Event&)> log; // optional

    LoadedMap(std::string n, game::TerrainMap terrain, std::vector<game::MapSprite> objects, game::MapEntities ent,
              game::BattleState state, const game::GlobalRules& rules, render::AnimationCache* clips, std::uint32_t seed)
        : name(std::move(n)), entities(std::move(ent)), scene(std::move(terrain), std::move(objects)),
          battle(scene, entities, rules, clips), server(state, game::CombatOptions{}, seed),
          view(std::move(state), scene, battle) {
        view.apply(server.start());
    }

    // Sends a request and plays the answer. Returns the refusal, if any.
    std::string send(const game::Request& r) {
        const std::vector<game::Event> events = server.handle(r);
        if (log)
            for (const game::Event& e : events) log(e);
        view.apply(events);
        std::string refused;
        for (const game::Event& e : events)
            if (const auto* rej = std::get_if<game::RequestRejected>(&e)) refused = rej->reason;
        return refused;
    }
};

std::unique_ptr<LoadedMap> loadMap(const fs::path& path, const game::GlobalRules& rules, render::AnimationCache* clips,
                                   std::uint32_t seed) {
    const data::SqliteDb db(path);
    game::TerrainMap terrain = game::loadTerrainMap(db);
    game::MapEntities entities;
    std::vector<game::MapSprite> objects = game::loadMapObjects(db, rules, terrain, &entities);
    game::BattleState state = game::loadBattle(db, rules, terrain);
    game::startBattle(state);
    return std::make_unique<LoadedMap>(path.stem().string(), std::move(terrain), std::move(objects), std::move(entities),
                                       std::move(state), rules, clips, seed);
}

// Requests built from the client's state.
game::MoveRequest moveOrder(const game::BattleState& s, const std::vector<int>& units, game::Point target) {
    game::MoveRequest r{s.currentPlayer, {}};
    for (const game::PlannedMove& m : game::planGroupMove(s, units, target)) {
        game::UnitOrder o{m.unit, m.path};
        o.path.resize(static_cast<std::size_t>(m.affordable));
        r.orders.push_back(std::move(o));
    }
    return r;
}

game::ShootRequest shootOrder(const game::BattleState& s, const std::vector<int>& units, game::Point target, bool autofire) {
    game::ShootRequest r{s.currentPlayer, {}, target, autofire};
    for (const int u : units) r.orders.push_back({u, {}});
    return r;
}

// Should a right click on this tile fire rather than move? True for enemy
// units and buildings of the player whose turn it is.
bool isEnemyTarget(const game::BattleState& s, game::Point t) {
    const int player = s.currentPlayer;
    if (const int b = s.buildingAt(t); b >= 0) return game::buildingSide(player, s.building(b), true) == game::Side::Enemy;
    const int u = s.unitAt(t);
    if (u < 0) return false;
    for (const game::BattleUnit& mine : s.units())
        if (mine.alive && mine.owner == player) return game::unitSide(mine, s.unit(u), true) == game::Side::Enemy;
    return s.unit(u).owner != player;
}

int usage() {
    std::fprintf(stderr, "usage: mapview [--game-dir DIR] [--data DIR] [--scenario NAME] [--scale N] [--mute] [--seed N]\n"
                         "               [--screenshot OUT.png] [--no-objects] [--do ACTION]... [--ticks N]\n");
    return 2;
}

game::Point parseTile(const std::string& s) {
    const auto comma = s.find(',');
    if (comma == std::string::npos) throw std::runtime_error("bad tile: " + s);
    return {std::stoi(s.substr(0, comma)), std::stoi(s.substr(comma + 1))};
}

std::string tileText(game::Point p) { return std::to_string(p.x) + "," + std::to_string(p.y); }

// One line per event, for the screenshot mode's log.
std::string describe(const game::BattleState& s, const game::Event& event) {
    auto unit = [&](int i) { return s.unit(i).type->image + "#" + std::to_string(i); };
    return std::visit(
        [&](const auto& e) -> std::string {
            using T = std::decay_t<decltype(e)>;
            if constexpr (std::is_same_v<T, game::UnitMoved>)
                return unit(e.unit) + " moves " + std::to_string(e.tiles.size()) + " tiles to " + tileText(e.tiles.back()) +
                       ", AP " + std::to_string(e.ap);
            else if constexpr (std::is_same_v<T, game::UnitShot>)
                return unit(e.unit) + " fires at " + tileText(e.aim) + ", lands on " + tileText(e.impact) + ", AP " +
                       std::to_string(e.ap);
            else if constexpr (std::is_same_v<T, game::UnitDamaged>)
                return unit(e.unit) + " armor " + std::to_string(e.armor) + " AP " + std::to_string(e.ap) + " negAP " +
                       std::to_string(e.negAp);
            else if constexpr (std::is_same_v<T, game::UnitDestroyed>)
                return unit(e.unit) + " destroyed" + (e.wreck.empty() ? "" : ", wreck " + e.wreck);
            else if constexpr (std::is_same_v<T, game::BuildingDamaged>)
                return "building #" + std::to_string(e.building) + " armor " + std::to_string(e.armor);
            else if constexpr (std::is_same_v<T, game::BuildingDestroyed>)
                return "building #" + std::to_string(e.building) + " destroyed, " + std::to_string(e.rubble.size()) + " rubble";
            else if constexpr (std::is_same_v<T, game::WallDamaged>)
                return "wall #" + std::to_string(e.wall) + " armor " + std::to_string(e.armor);
            else if constexpr (std::is_same_v<T, game::WallDestroyed>) return "wall #" + std::to_string(e.wall) + " destroyed";
            else if constexpr (std::is_same_v<T, game::RoadDestroyed>) return "road #" + std::to_string(e.road) + " destroyed";
            else if constexpr (std::is_same_v<T, game::MineDestroyed>) return "mine #" + std::to_string(e.mine) + " destroyed";
            else if constexpr (std::is_same_v<T, game::MineExploded>) return "mine #" + std::to_string(e.mine) + " explodes";
            else if constexpr (std::is_same_v<T, game::MinesRevealed>)
                return std::to_string(e.mines.size()) + " mines revealed to player " + std::to_string(e.player);
            else if constexpr (std::is_same_v<T, game::TerrainBurned>) return tileText(e.tile) + " burns to " + e.terrain;
            else if constexpr (std::is_same_v<T, game::TurnStarted>)
                return "turn " + std::to_string(e.turn) + ", player " + std::to_string(e.player);
            else if constexpr (std::is_same_v<T, game::BattleOver>) return "battle over";
            else return "refused: " + e.reason;
        },
        event);
}

// Sends an order given as move:X,Y:TX,TY, attack:X,Y:TX,TY or end.
void startAction(LoadedMap& map, const std::string& spec) {
    std::vector<std::string> parts;
    for (std::size_t start = 0;;) {
        const auto colon = spec.find(':', start);
        parts.push_back(spec.substr(start, colon == std::string::npos ? std::string::npos : colon - start));
        if (colon == std::string::npos) break;
        start = colon + 1;
    }
    const game::BattleState& s = map.view.state();
    std::string refused;
    if (parts[0] == "end") {
        refused = map.send(game::EndTurnRequest{s.currentPlayer});
    } else {
        if (parts.size() < 3) throw std::runtime_error("bad action: " + spec);
        const game::Point at = parseTile(parts[1]), to = parseTile(parts[2]);
        const int unit = s.unitAt(at);
        if (unit < 0) throw std::runtime_error("no unit for action: " + spec);
        if (parts[0] == "move") refused = map.send(moveOrder(s, {unit}, to));
        else if (parts[0] == "attack") refused = map.send(shootOrder(s, {unit}, to, false));
        else throw std::runtime_error("unknown action: " + spec);
    }
    if (!refused.empty()) std::printf("%s: %s\n", spec.c_str(), refused.c_str());
}

void drawScene(const LoadedMap& map, render::IndexedSurface& surface, const game::Rect& view, game::SpriteLibrary& sprites,
               bool objects, game::RenderStats* statsOut = nullptr, const std::vector<game::Overlay>& markers = {}) {
    game::SceneExtras extras = map.battle.extras();
    extras.overlays.insert(extras.overlays.end(), markers.begin(), markers.end());
    const game::RenderStats stats = map.scene.draw(surface, view, sprites, objects, &extras);
    if (statsOut) *statsOut = stats;
}

int screenshot(LoadedMap& map, game::SpriteLibrary& sprites, const Palettes& palettes, const Options& opt) {
    map.log = [&map](const game::Event& e) { std::printf("  %s\n", describe(map.server.state(), e).c_str()); };
    for (const std::string& a : opt.actions) {
        for (int i = 0; i < 100000 && map.battle.busy(); ++i) map.battle.tick();
        startAction(map, a);
    }
    if (opt.ticks >= 0)
        for (int i = 0; i < opt.ticks; ++i) map.battle.tick();
    else
        for (int i = 0; i < 100000 && map.battle.busy(); ++i) map.battle.tick();

    const int size = map.scene.terrain().size;
    const game::Rect bounds = game::iso::mapBounds(size);
    render::IndexedSurface surface(bounds.w, bounds.h, 0);
    game::RenderStats stats;
    drawScene(map, surface, bounds, sprites, opt.objects, &stats);
    formats::writePngRgba(opt.screenshot, surface.width(), surface.height(), surface.toRgba(palettes.at(0, false)));
    std::printf("%s: %dx%d tiles, %d drawn, %zu objects, %zu animation frames, %d missing sprites -> %s (%d x %d)\n",
                map.name.c_str(), size, size, stats.tilesDrawn, opt.objects ? map.scene.objects().size() : std::size_t{0},
                map.battle.animator().overlays().size(), stats.missingSprites, opt.screenshot.string().c_str(),
                surface.width(), surface.height());
    if (opt.objects && stats.missingSprites > 0) {
        std::set<std::string> missing;
        for (const game::MapSprite& o : map.scene.objects())
            if (!sprites.get(o.set, o.name)) missing.insert(o.name);
        for (const std::string& name : missing) std::printf("  missing: %s\n", name.c_str());
    }
    return stats.missingSprites == 0 ? 0 : 1;
}

// Plays the audio of animation clips; every sound gets its own stream on
// one device, and SDL mixes them.
class SoundPlayer {
public:
    SoundPlayer() {
        device_ = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr);
        if (!device_) std::fprintf(stderr, "no audio: %s\n", SDL_GetError());
    }
    ~SoundPlayer() {
        for (SDL_AudioStream* s : streams_) SDL_DestroyAudioStream(s);
        if (device_) SDL_CloseAudioDevice(device_);
    }
    SoundPlayer(const SoundPlayer&) = delete;
    SoundPlayer& operator=(const SoundPlayer&) = delete;

    bool enabled = true;

    void play(const formats::SmkAudio& audio) {
        if (!device_ || !enabled || audio.pcm.empty()) return;
        const SDL_AudioSpec spec{audio.bitsPerSample == 16 ? SDL_AUDIO_S16LE : SDL_AUDIO_U8, audio.channels,
                                 static_cast<int>(audio.sampleRate)};
        SDL_AudioStream* s = SDL_CreateAudioStream(&spec, nullptr);
        if (!s) return;
        if (!SDL_BindAudioStream(device_, s)) {
            SDL_DestroyAudioStream(s);
            return;
        }
        SDL_PutAudioStreamData(s, audio.pcm.data(), static_cast<int>(audio.pcm.size()));
        SDL_FlushAudioStream(s);
        streams_.push_back(s);
    }

    // Releases the streams that have played to the end.
    void collect() {
        std::erase_if(streams_, [](SDL_AudioStream* s) {
            if (SDL_GetAudioStreamQueued(s) > 0) return false;
            SDL_DestroyAudioStream(s);
            return true;
        });
    }

private:
    SDL_AudioDeviceID device_ = 0;
    std::vector<SDL_AudioStream*> streams_;
};

// Palette index of the brightest color, for the selection marker.
std::uint8_t brightestIndex(const formats::Palette& p) {
    std::size_t best = 0;
    int bestSum = -1;
    for (std::size_t i = 0; i < p.size(); ++i) {
        const int sum = p[i].r + p[i].g + p[i].b;
        if (sum > bestSum) {
            bestSum = sum;
            best = i;
        }
    }
    return static_cast<std::uint8_t>(best);
}

// Outline of a tile's ground diamond.
void drawTileMarker(render::IndexedSurface& s, game::Point tile, int mapSize, const game::Rect& view, std::uint8_t color) {
    const game::Point o = game::iso::tileOrigin(tile.x, tile.y, mapSize);
    for (int row = 0; row < 28; ++row) {
        const int half = row < 14 ? 1 + 2 * row : 1 + 2 * (27 - row);
        const int y = o.y + game::iso::kDiamondTop + row - view.y;
        if (y < 0 || y >= s.height()) continue;
        for (const int x : {o.x + 27 - half - view.x, o.x + 27 + half - 1 - view.x})
            if (x >= 0 && x < s.width()) s.row(y)[x] = color;
    }
}

int run(const std::vector<fs::path>& scenarios, std::size_t current, const game::GlobalRules& rules,
        game::SpriteLibrary& sprites, render::AnimationCache* clips, const Palettes& palettes, Options opt) {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    if (!SDL_CreateWindowAndRenderer("OpenDay map viewer", 1280, 800, SDL_WINDOW_RESIZABLE, &window, &renderer)) {
        std::fprintf(stderr, "SDL_CreateWindowAndRenderer failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_SetRenderVSync(renderer, 1);
    int returnCode = 0;
    {
        SoundPlayer sound;
        sound.enabled = opt.sound;
        const std::uint8_t markerColor = brightestIndex(palettes.base);
        int scale = opt.scale;
        bool showObjects = opt.objects;

        std::unique_ptr<LoadedMap> map;
        game::Rect bounds;
        std::vector<int> selection;
        bool autofire = false;
        std::string message; // last refusal, shown in the title
        auto open = [&](std::size_t index) {
            map = loadMap(scenarios[index], rules, clips, opt.seed);
            map->battle.animator().playSound = [&sound](const render::Animation& a) { sound.play(a.audio); };
            bounds = game::iso::mapBounds(map->scene.terrain().size);
            selection.clear();
            message.clear();
        };
        open(current);
        std::string shownTitle;

        // Path preview, recomputed when the hovered tile or the state changes.
        std::vector<game::PlannedMove> preview;
        game::Point previewTile{-1, -1};
        std::size_t previewEvents = static_cast<std::size_t>(-1);
        std::size_t eventCount = 0; // requests sent, to notice state changes
        auto send = [&](const game::Request& r) {
            message = map->send(r);
            ++eventCount;
            const game::BattleState& s = map->view.state();
            std::erase_if(selection, [&](int u) { return !s.unit(u).alive || s.unit(u).owner != s.currentPlayer; });
        };

        float camX = 0, camY = 0; // map pixel at the top-left of the view
        render::IndexedSurface surface;
        SDL_Texture* texture = nullptr;
        int texW = 0, texH = 0;
        bool centered = false;
        bool cycling = true;
        float dragged = 0; // mouse travel since the button went down
        float mouseX = 0, mouseY = 0;
        const Uint64 tickMs = 1000 / game::kTicksPerSecond;
        Uint64 last = SDL_GetTicks();
        Uint64 tickClock = 0;

        auto tileUnderMouse = [&] {
            const int px = static_cast<int>(camX + mouseX / static_cast<float>(scale));
            const int py = static_cast<int>(camY + mouseY / static_cast<float>(scale));
            return game::iso::tileAt(px, py, map->scene.terrain().size);
        };

        for (bool running = true; running;) {
            SDL_Event ev;
            while (SDL_PollEvent(&ev)) {
                switch (ev.type) {
                case SDL_EVENT_QUIT:
                    running = false;
                    break;
                case SDL_EVENT_KEY_DOWN:
                    if (ev.key.key == SDLK_ESCAPE) running = false;
                    if (ev.key.key == SDLK_C) cycling = !cycling;
                    if (ev.key.key == SDLK_O) showObjects = !showObjects;
                    if (ev.key.key == SDLK_M) sound.enabled = !sound.enabled;
                    if (ev.key.key == SDLK_SPACE) selection.clear();
                    if (ev.key.key == SDLK_F) autofire = !autofire;
                    if ((ev.key.key == SDLK_RETURN || ev.key.key == SDLK_KP_ENTER) && !map->battle.busy()) {
                        send(game::EndTurnRequest{map->view.state().currentPlayer});
                        selection.clear();
                    }
                    if (ev.key.key >= SDLK_1 && ev.key.key <= SDLK_4) scale = static_cast<int>(ev.key.key - SDLK_1) + 1;
                    if (ev.key.key == SDLK_PAGEDOWN || ev.key.key == SDLK_PAGEUP) {
                        const std::size_t n = scenarios.size();
                        current = ev.key.key == SDLK_PAGEDOWN ? (current + 1) % n : (current + n - 1) % n;
                        open(current);
                        centered = false;
                    }
                    break;
                case SDL_EVENT_MOUSE_BUTTON_DOWN:
                    dragged = 0;
                    break;
                case SDL_EVENT_MOUSE_BUTTON_UP:
                    if (dragged > 4 || map->battle.busy()) break;
                    {
                        const game::BattleState& s = map->view.state();
                        const game::Point t = tileUnderMouse();
                        const SDL_Keymod mods = SDL_GetModState();
                        if (ev.button.button == SDL_BUTTON_LEFT) {
                            const int u = s.unitAt(t);
                            const bool own = u >= 0 && s.unit(u).owner == s.currentPlayer;
                            if (mods & SDL_KMOD_SHIFT) {
                                if (own && std::erase(selection, u) == 0) selection.push_back(u);
                            } else {
                                selection.clear();
                                if (own) selection.push_back(u);
                            }
                        } else if (ev.button.button == SDL_BUTTON_RIGHT && !selection.empty() && s.inside(t)) {
                            if ((mods & SDL_KMOD_CTRL) || isEnemyTarget(s, t)) send(shootOrder(s, selection, t, autofire));
                            else send(moveOrder(s, selection, t));
                        }
                    }
                    break;
                case SDL_EVENT_MOUSE_MOTION:
                    mouseX = ev.motion.x;
                    mouseY = ev.motion.y;
                    if (ev.motion.state & (SDL_BUTTON_LMASK | SDL_BUTTON_RMASK)) {
                        dragged += std::abs(ev.motion.xrel) + std::abs(ev.motion.yrel);
                        if (dragged > 4) {
                            camX -= ev.motion.xrel / static_cast<float>(scale);
                            camY -= ev.motion.yrel / static_cast<float>(scale);
                        }
                    }
                    break;
                default:
                    break;
                }
            }

            const Uint64 now = SDL_GetTicks();
            const float dt = static_cast<float>(now - last) / 1000.0f;
            tickClock += now - last;
            last = now;
            // Animation clock: 15 ticks per second; skip ahead after a stall.
            if (tickClock > 10 * tickMs) tickClock = tickMs;
            for (; tickClock >= tickMs; tickClock -= tickMs) map->battle.tick();
            sound.collect();

            const bool* keys = SDL_GetKeyboardState(nullptr);
            const float speed = 900.0f * dt / static_cast<float>(scale);
            if (keys[SDL_SCANCODE_LEFT] || keys[SDL_SCANCODE_A]) camX -= speed;
            if (keys[SDL_SCANCODE_RIGHT] || keys[SDL_SCANCODE_D]) camX += speed;
            if (keys[SDL_SCANCODE_UP] || keys[SDL_SCANCODE_W]) camY -= speed;
            if (keys[SDL_SCANCODE_DOWN] || keys[SDL_SCANCODE_S]) camY += speed;

            int outW = 0, outH = 0;
            SDL_GetRenderOutputSize(renderer, &outW, &outH);
            const int viewW = std::max(1, outW / scale), viewH = std::max(1, outH / scale);
            if (!centered) {
                camX = static_cast<float>(bounds.w - viewW) / 2;
                camY = static_cast<float>(bounds.h - viewH) / 2;
                centered = true;
            }
            camX = std::clamp(camX, static_cast<float>(-viewW / 2), static_cast<float>(bounds.w - viewW / 2));
            camY = std::clamp(camY, static_cast<float>(-viewH / 2), static_cast<float>(bounds.h - viewH / 2));

            if (viewW != texW || viewH != texH) {
                SDL_DestroyTexture(texture);
                texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, viewW, viewH);
                SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
                surface.resize(viewW, viewH);
                texW = viewW;
                texH = viewH;
            }

            surface.fill(0);
            const game::Rect view{static_cast<int>(camX), static_cast<int>(camY), viewW, viewH};
            const game::BattleState& state = map->view.state();
            const int n = map->scene.terrain().size;
            const game::Point hover = tileUnderMouse();
            const bool idle = !map->battle.busy();
            const bool aiming = !selection.empty() && state.inside(hover) &&
                                ((SDL_GetModState() & SDL_KMOD_CTRL) || isEnemyTarget(state, hover));

            // Markers: path preview, target and selection (SYMBOLS.DBI).
            std::vector<game::Overlay> markers;
            auto marker = [&](game::Point t, const char* name) {
                const game::Point o = game::iso::tileOrigin(t.x, t.y, n);
                if (const formats::DbiSprite* sp = sprites.get(game::SpriteSet::Symbols, name))
                    markers.push_back({t.x, t.y, o.x, o.y, sp});
            };
            if (idle && !selection.empty() && !aiming) {
                if (hover.x != previewTile.x || hover.y != previewTile.y || previewEvents != eventCount) {
                    preview = state.inside(hover) ? game::planGroupMove(state, selection, hover)
                                                  : std::vector<game::PlannedMove>{};
                    previewTile = hover;
                    previewEvents = eventCount;
                }
                for (const game::PlannedMove& m : preview) {
                    const std::vector<std::string> names = game::previewSprites(m);
                    for (std::size_t i = 0; i < names.size(); ++i) marker(m.path[i].pos, names[i].c_str());
                }
            }
            if (idle && aiming) marker(hover, "TARGET");
            for (const int u : selection)
                if (idle && state.unit(u).alive) marker(state.unit(u).pos, "SEL000");
            drawScene(*map, surface, view, sprites, showObjects, nullptr, markers);
            if (!sprites.cache(game::SpriteSet::Symbols))
                for (const int u : selection)
                    if (state.unit(u).alive) drawTileMarker(surface, state.unit(u).pos, n, view, markerColor);

            // Title: battle status.
            std::string title = "OpenDay - " + map->name + " - turn " + std::to_string(state.turn);
            if (state.turnLimit > 0) title += "/" + std::to_string(state.turnLimit);
            title += state.over ? " - battle over" : " - player " + std::to_string(state.currentPlayer);
            if (selection.size() == 1) {
                const game::BattleUnit& u = state.unit(selection[0]);
                title += " - " + u.type->image + " AP " + std::to_string(u.availableAp()) + "/" +
                         std::to_string(u.type->actionPoints) + " armor " + std::to_string(u.armor) + "/" +
                         std::to_string(u.type->armor);
                if (aiming) {
                    const game::ShotCheck c = game::checkShot(state, selection[0], hover, true);
                    if (c.ok()) title += " - hit " + std::to_string(std::min(100, game::hitChance(state, selection[0], c.aim) + 1)) + "%";
                    else title += " - cannot fire";
                }
            } else if (!selection.empty()) {
                title += " - " + std::to_string(selection.size()) + " units";
            }
            if (autofire) title += " - autofire";
            if (!message.empty()) title += " - " + message;
            if (title != shownTitle) {
                SDL_SetWindowTitle(window, title.c_str());
                shownTitle = title;
            }

            void* pixels = nullptr;
            int pitch = 0;
            if (SDL_LockTexture(texture, nullptr, &pixels, &pitch)) {
                const std::vector<std::uint8_t> rgba = surface.toRgba(palettes.at(now, cycling));
                for (int y = 0; y < viewH; ++y)
                    std::copy_n(rgba.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(viewW) * 4,
                                static_cast<std::size_t>(viewW) * 4, static_cast<std::uint8_t*>(pixels) + y * pitch);
                SDL_UnlockTexture(texture);
            }
            SDL_RenderClear(renderer);
            const SDL_FRect dst{0, 0, static_cast<float>(viewW * scale), static_cast<float>(viewH * scale)};
            SDL_RenderTexture(renderer, texture, nullptr, &dst);
            SDL_RenderPresent(renderer);
        }
        SDL_DestroyTexture(texture);
    }
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return returnCode;
}

} // namespace

int main(int argc, char** argv) {
    Options opt;
    opt.gameDir = envPath("OPENDAY_GAME_DIR");
    opt.dataDir = envPath("OPENDAY_DATA_DIR");
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--no-objects") {
            opt.objects = false;
            continue;
        }
        if (a == "--mute") {
            opt.sound = false;
            continue;
        }
        if (i + 1 >= argc) return usage();
        if (a == "--game-dir") opt.gameDir = argv[++i];
        else if (a == "--data") opt.dataDir = argv[++i];
        else if (a == "--scenario") opt.scenario = argv[++i];
        else if (a == "--screenshot") opt.screenshot = argv[++i];
        else if (a == "--do") opt.actions.emplace_back(argv[++i]);
        else if (a == "--ticks") opt.ticks = std::max(0, std::atoi(argv[++i]));
        else if (a == "--scale") opt.scale = std::clamp(std::atoi(argv[++i]), 1, 4);
        else if (a == "--seed") opt.seed = static_cast<std::uint32_t>(std::strtoul(argv[++i], nullptr, 10));
        else return usage();
    }
    if (opt.gameDir.empty() || opt.dataDir.empty()) {
        std::fprintf(stderr, "set --game-dir / OPENDAY_GAME_DIR and --data / OPENDAY_DATA_DIR\n"
                             "(convert the databases first with tools/mdb/mdb2sqlite.py)\n");
        return usage();
    }

    try {
        const std::vector<fs::path> scenarios = findScenarios(opt.dataDir);
        if (scenarios.empty()) {
            std::fprintf(stderr, "no scenario databases found in %s\n", opt.dataDir.string().c_str());
            return 1;
        }
        std::size_t current = 0;
        if (!opt.scenario.empty()) {
            const auto it = std::find_if(scenarios.begin(), scenarios.end(),
                                         [&](const fs::path& p) { return lower(p.stem().string()) == lower(opt.scenario); });
            if (it == scenarios.end()) {
                std::fprintf(stderr, "scenario not found: %s\n", opt.scenario.c_str());
                return 1;
            }
            current = static_cast<std::size_t>(it - scenarios.begin());
        }

        const game::GlobalRules rules = game::loadGlobalRules(data::SqliteDb(findGlobalDb(opt.dataDir)));
        game::SpriteLibrary sprites = game::SpriteLibrary::fromGameDir(opt.gameDir);
        const Palettes palettes{formats::loadBmpPalette(opt.gameDir / "INTERF" / "PAL.BMP"),
                                formats::PalCycle::fromFile(opt.gameDir / "INTERF" / "PALCYCLE.PAL")};
        std::unique_ptr<render::AnimationCache> clips;
        const fs::path ff = opt.gameDir / "ANMSUNIT" / "ANMSUNIT.FF";
        if (fs::exists(ff))
            clips = std::make_unique<render::AnimationCache>(std::make_shared<const formats::FfArchive>(formats::FfArchive::open(ff)));
        else
            std::fprintf(stderr, "%s not found: actions run without animations\n", ff.string().c_str());

        if (!opt.screenshot.empty()) {
            const std::unique_ptr<LoadedMap> map = loadMap(scenarios[current], rules, clips.get(), opt.seed);
            return screenshot(*map, sprites, palettes, opt);
        }
        return run(scenarios, current, rules, sprites, clips.get(), palettes, opt);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}
