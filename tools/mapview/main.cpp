// mapview: SDL3 viewer for the scenario maps (terrain and placed objects).
//
//   mapview [--game-dir DIR] --data DIR [--scenario NAME] [--scale N]
//   mapview [--game-dir DIR] --data DIR --scenario NAME --screenshot OUT.png [--no-objects]
//
// --game-dir  game installation (default: $OPENDAY_GAME_DIR); the sprite
//             archives are read from its IMGS folder.
// --data      output folder of tools/mdb/mdb2sqlite.py (default:
//             $OPENDAY_DATA_DIR).
// --scenario  scenario database name without extension, e.g. s1-1-1.
//
// Controls: arrow keys / WASD or drag with the mouse to scroll, PageUp /
// PageDown for the previous / next scenario, 1-4 to set the zoom, C to toggle
// palette cycling (water, lights), O to toggle objects, Esc to quit.

#include "data/sqlite_db.hpp"
#include "formats/bmp_palette.hpp"
#include "formats/palcycle.hpp"
#include "formats/png_writer.hpp"
#include "map/map_objects.hpp"
#include "map/map_renderer.hpp"
#include "map/rules.hpp"
#include "map/terrain_map.hpp"
#include "render/indexed_surface.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <memory>
#include <set>
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
    int scale = 2;
    bool objects = true;
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

struct LoadedMap {
    std::string name;
    game::MapScene scene;
};

LoadedMap loadMap(const fs::path& path, const game::GlobalRules& rules) {
    const data::SqliteDb db(path);
    game::TerrainMap terrain = game::loadTerrainMap(db);
    std::vector<game::MapSprite> objects = game::loadMapObjects(db, rules, terrain);
    return {path.stem().string(), game::MapScene(std::move(terrain), std::move(objects))};
}

int usage() {
    std::fprintf(stderr, "usage: mapview [--game-dir DIR] [--data DIR] [--scenario NAME] [--scale N]\n"
                         "               [--screenshot OUT.png] [--no-objects]\n");
    return 2;
}

int screenshot(const LoadedMap& map, game::SpriteLibrary& sprites, const Palettes& palettes, const fs::path& out,
               bool objects) {
    const int size = map.scene.terrain().size;
    const game::Rect bounds = game::iso::mapBounds(size);
    render::IndexedSurface surface(bounds.w, bounds.h, 0);
    const game::RenderStats stats = map.scene.draw(surface, bounds, sprites, objects);
    formats::writePngRgba(out, surface.width(), surface.height(), surface.toRgba(palettes.at(0, false)));
    std::printf("%s: %dx%d tiles, %d drawn, %zu objects, %d missing sprites -> %s (%d x %d)\n", map.name.c_str(), size,
                size, stats.tilesDrawn, objects ? map.scene.objects().size() : std::size_t{0}, stats.missingSprites,
                out.string().c_str(), surface.width(), surface.height());
    if (objects && stats.missingSprites > 0) {
        std::set<std::string> missing;
        for (const game::MapSprite& o : map.scene.objects())
            if (!sprites.get(o.set, o.name)) missing.insert(o.name);
        for (const std::string& name : missing) std::printf("  missing: %s\n", name.c_str());
    }
    return stats.missingSprites == 0 ? 0 : 1;
}

int run(const std::vector<fs::path>& scenarios, std::size_t current, const game::GlobalRules& rules,
        game::SpriteLibrary& sprites, const Palettes& palettes, int scale, bool showObjects) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
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

    LoadedMap map = loadMap(scenarios[current], rules);
    game::Rect bounds = game::iso::mapBounds(map.scene.terrain().size);
    float camX = 0, camY = 0; // map pixel at the top-left of the view
    auto centerCamera = [&](int viewW, int viewH) {
        camX = static_cast<float>(bounds.w - viewW) / 2;
        camY = static_cast<float>(bounds.h - viewH) / 2;
    };
    auto updateTitle = [&] {
        const int n = map.scene.terrain().size;
        const std::string title =
            "OpenDay map viewer - " + map.name + " (" + std::to_string(n) + "x" + std::to_string(n) + ")";
        SDL_SetWindowTitle(window, title.c_str());
    };
    updateTitle();

    render::IndexedSurface surface;
    SDL_Texture* texture = nullptr;
    int texW = 0, texH = 0;
    bool centered = false;
    bool cycling = true;
    Uint64 last = SDL_GetTicks();

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
                if (ev.key.key >= SDLK_1 && ev.key.key <= SDLK_4) scale = static_cast<int>(ev.key.key - SDLK_1) + 1;
                if (ev.key.key == SDLK_PAGEDOWN || ev.key.key == SDLK_PAGEUP) {
                    const std::size_t n = scenarios.size();
                    current = ev.key.key == SDLK_PAGEDOWN ? (current + 1) % n : (current + n - 1) % n;
                    map = loadMap(scenarios[current], rules);
                    bounds = game::iso::mapBounds(map.scene.terrain().size);
                    centered = false;
                    updateTitle();
                }
                break;
            case SDL_EVENT_MOUSE_MOTION:
                if (ev.motion.state & (SDL_BUTTON_LMASK | SDL_BUTTON_RMASK)) {
                    camX -= ev.motion.xrel / static_cast<float>(scale);
                    camY -= ev.motion.yrel / static_cast<float>(scale);
                }
                break;
            default:
                break;
            }
        }

        const Uint64 now = SDL_GetTicks();
        const float dt = static_cast<float>(now - last) / 1000.0f;
        last = now;
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
            centerCamera(viewW, viewH);
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
        map.scene.draw(surface, view, sprites, showObjects);

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
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
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
        if (i + 1 >= argc) return usage();
        if (a == "--game-dir") opt.gameDir = argv[++i];
        else if (a == "--data") opt.dataDir = argv[++i];
        else if (a == "--scenario") opt.scenario = argv[++i];
        else if (a == "--screenshot") opt.screenshot = argv[++i];
        else if (a == "--scale") opt.scale = std::clamp(std::atoi(argv[++i]), 1, 4);
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

        if (!opt.screenshot.empty())
            return screenshot(loadMap(scenarios[current], rules), sprites, palettes, opt.screenshot, opt.objects);
        return run(scenarios, current, rules, sprites, palettes, opt.scale, opt.objects);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}
