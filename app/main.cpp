// openday: the game program.
//
//   openday [--game-dir DIR] [--data DIR] [--scale N] [--no-intro] [--mute] [--cheats]
//           [--script STEPS --shot OUT.png]
//
// --game-dir  the game installation (default: $OPENDAY_GAME_DIR)
// --data      output folder of tools/mdb/mdb2sqlite.py (default: $OPENDAY_DATA_DIR)
// --scale     window scale of the 640 x 480 screen (default: the largest that
//             fits the screen)
// --cheats    enables the battle cheat keys Tab + W (win) and Tab + L (lose)
// --script    test input, steps separated by ';': click:X,Y (left), rclick:X,Y,
//             move:X,Y, key:NAME (a letter, enter, esc, tab, up, down, left,
//             right), wait:MS. With --shot the screen is saved to a PNG after
//             the last step and the program ends.

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <exception>

#include "app/app.hpp"
#include "app/screens.hpp"
#include "formats/png_writer.hpp"

#include <deque>
#include <sstream>
#include <string>

namespace fs = std::filesystem;
using namespace openday;

namespace {

fs::path envPath(const char* name) {
    const char* v = std::getenv(name);
    return v ? fs::path(v) : fs::path();
}

int translateKey(SDL_Keycode k) {
    switch (k) {
    case SDLK_RETURN:
    case SDLK_KP_ENTER: return ui::kKeyEnter;
    case SDLK_ESCAPE: return ui::kKeyEscape;
    case SDLK_BACKSPACE: return ui::kKeyBackspace;
    case SDLK_LEFT: return ui::kKeyLeft;
    case SDLK_RIGHT: return ui::kKeyRight;
    case SDLK_UP: return ui::kKeyUp;
    case SDLK_DOWN: return ui::kKeyDown;
    case SDLK_PAGEUP: return ui::kKeyPageUp;
    case SDLK_PAGEDOWN: return ui::kKeyPageDown;
    case SDLK_TAB: return ui::kKeyTab;
    case SDLK_HOME: return ui::kKeyHome;
    case SDLK_END: return ui::kKeyEnd;
    case SDLK_F1: return ui::kKeyF1;
    case SDLK_F2: return ui::kKeyF2;
    case SDLK_F3: return ui::kKeyF3;
    case SDLK_F4: return ui::kKeyF4;
    case SDLK_F5: return ui::kKeyF5;
    case SDLK_F6: return ui::kKeyF6;
    default: return k < 128 ? static_cast<int>(k) : 0; // SDL keycodes of printable keys are their lower-case ASCII
    }
}

// One scripted test step.
struct Step {
    std::string kind;
    int a = 0, b = 0;
    std::string key;
};

std::deque<Step> parseScript(const std::string& script) {
    std::deque<Step> out;
    std::stringstream ss(script);
    std::string item;
    while (std::getline(ss, item, ';')) {
        const auto colon = item.find(':');
        if (colon == std::string::npos) continue;
        Step st;
        st.kind = item.substr(0, colon);
        const std::string arg = item.substr(colon + 1);
        if (st.kind == "key") {
            st.key = arg;
        } else {
            const auto comma = arg.find(',');
            st.a = std::atoi(arg.c_str());
            if (comma != std::string::npos) st.b = std::atoi(arg.c_str() + comma + 1);
        }
        out.push_back(st);
    }
    return out;
}

int scriptKey(const std::string& k) {
    if (k == "enter") return ui::kKeyEnter;
    if (k == "esc") return ui::kKeyEscape;
    if (k == "tab") return ui::kKeyTab;
    if (k == "up") return ui::kKeyUp;
    if (k == "down") return ui::kKeyDown;
    if (k == "left") return ui::kKeyLeft;
    if (k == "right") return ui::kKeyRight;
    if (k == "space") return ' ';
    if (k.size() == 2 && k[0] == 'f' && k[1] >= '1' && k[1] <= '6') return ui::kKeyF1 + (k[1] - '1');
    return k.empty() ? 0 : static_cast<unsigned char>(k[0]);
}

// Sizes the (hidden) window so that it fits the usable area of its display,
// title bar and taskbar included, and shows it centred there. scale > 0 asks
// for that multiple of 640 x 480 (reduced when it does not fit); 0 picks the
// largest whole multiple that fits, or the largest 4:3 size below 2x.
void placeWindow(SDL_Window* window, int scale) {
    SDL_Rect area{};
    if (!SDL_GetDisplayUsableBounds(SDL_GetDisplayForWindow(window), &area) || area.w <= 0 || area.h <= 0) {
        SDL_SetWindowSize(window, 640 * std::max(scale, 1), 480 * std::max(scale, 1));
        SDL_ShowWindow(window);
        return;
    }
    int top = 0, left = 0, bottom = 0, right = 0;
    if (!SDL_GetWindowBordersSize(window, &top, &left, &bottom, &right) || top == 0) {
        top = 32; // typical title bar when the borders are not known yet
        left = right = bottom = 8;
    }
    const int maxW = area.w - left - right, maxH = area.h - top - bottom;
    const int fit = std::max(1, std::min(maxW / 640, maxH / 480));
    int w = 0, h = 0;
    if (scale > 0 || fit >= 2) {
        const int s = scale > 0 ? std::min(scale, fit) : fit;
        w = 640 * s;
        h = 480 * s;
    } else {
        h = std::max(240, std::min(maxH, maxW * 3 / 4));
        w = h * 4 / 3;
    }
    SDL_SetWindowSize(window, w, h);
    SDL_SetWindowPosition(window, area.x + left + std::max(0, (maxW - w) / 2), area.y + top + std::max(0, (maxH - h) / 2));
    SDL_ShowWindow(window);
}

int run(app::GameData& data, int scale, bool intro, bool mute, bool cheats, std::deque<Step> script, const fs::path& shot) {
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    if (!SDL_CreateWindowAndRenderer("OpenDay", 640, 480, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN, &window, &renderer)) {
        std::fprintf(stderr, "SDL_CreateWindowAndRenderer failed: %s\n", SDL_GetError());
        return 1;
    }
    placeWindow(window, scale);
    SDL_SetRenderVSync(renderer, 1);
    {
        platform::AudioPlayer audio;
        app::App app(data, audio);
        app.settings().sound = !mute;
        app.settings().music = !mute;
        app.settings().cheats = cheats;
        audio.soundEnabled = !mute;
        app.push(intro ? app::makeIntro(app) : app::makeMainMenu(app));
        app.applyChanges();

        render::IndexedSurface surface(640, 480);
        SDL_Texture* texture = nullptr;
        int texW = 0, texH = 0;
        Uint64 last = SDL_GetTicks();
        int waitMs = 0;
        bool scripted = !script.empty() || !shot.empty();
        while (!app.quitting()) {
            app::Screen* screen = app.top();
            const int w = screen->width(), h = screen->height();
            // Window pixels -> logical screen pixels (letter-boxed).
            int outW = 0, outH = 0;
            SDL_GetRenderOutputSize(renderer, &outW, &outH);
            const float k = std::min(static_cast<float>(outW) / static_cast<float>(w), static_cast<float>(outH) / static_cast<float>(h));
            const float ox = (static_cast<float>(outW) - static_cast<float>(w) * k) / 2;
            const float oy = (static_cast<float>(outH) - static_cast<float>(h) * k) / 2;
            float pixelRatio = SDL_GetWindowPixelDensity(window);
            if (pixelRatio <= 0) pixelRatio = 1;
            auto toLogical = [&](float x, float y, int& lx, int& ly) {
                lx = static_cast<int>((x * pixelRatio - ox) / k);
                ly = static_cast<int>((y * pixelRatio - oy) / k);
            };

            SDL_Event ev;
            while (SDL_PollEvent(&ev)) {
                ui::InputEvent e;
                const SDL_Keymod mods = SDL_GetModState();
                e.shift = (mods & SDL_KMOD_SHIFT) != 0;
                e.ctrl = (mods & SDL_KMOD_CTRL) != 0;
                bool send = true;
                switch (ev.type) {
                case SDL_EVENT_QUIT: app.quit(); send = false; break;
                case SDL_EVENT_MOUSE_MOTION:
                    e.type = ui::InputEvent::Type::MouseMove;
                    toLogical(ev.motion.x, ev.motion.y, e.x, e.y);
                    break;
                case SDL_EVENT_MOUSE_BUTTON_DOWN:
                case SDL_EVENT_MOUSE_BUTTON_UP:
                    e.type = ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN ? ui::InputEvent::Type::MouseDown : ui::InputEvent::Type::MouseUp;
                    toLogical(ev.button.x, ev.button.y, e.x, e.y);
                    e.button = ev.button.button;
                    break;
                case SDL_EVENT_MOUSE_WHEEL: {
                    e.type = ui::InputEvent::Type::Wheel;
                    float mx = 0, my = 0;
                    SDL_GetMouseState(&mx, &my);
                    toLogical(mx, my, e.x, e.y);
                    e.key = ev.wheel.y > 0 ? 1 : -1;
                    break;
                }
                case SDL_EVENT_KEY_DOWN:
                    e.type = ui::InputEvent::Type::KeyDown;
                    e.key = translateKey(ev.key.key);
                    send = e.key != 0;
                    break;
                default: send = false; break;
                }
                if (send && app.top()) app.top()->handle(e);
                app.applyChanges();
                if (app.quitting()) break;
            }
            if (app.quitting()) break;

            const Uint64 now = SDL_GetTicks();
            const int elapsed = static_cast<int>(std::min<Uint64>(now - last, 250));
            last = now;
            // Scripted input: one step per frame, waits in milliseconds.
            if (scripted) {
                if (waitMs > 0) {
                    waitMs -= elapsed;
                } else if (!script.empty()) {
                    const Step st = script.front();
                    script.pop_front();
                    auto send = [&](ui::InputEvent e) {
                        if (app.top()) app.top()->handle(e);
                        app.applyChanges();
                    };
                    ui::InputEvent e;
                    e.x = st.a;
                    e.y = st.b;
                    if (st.kind == "wait") {
                        waitMs = st.a;
                    } else if (st.kind == "move") {
                        e.type = ui::InputEvent::Type::MouseMove;
                        send(e);
                    } else if (st.kind == "click" || st.kind == "rclick") {
                        e.button = st.kind == "click" ? 1 : 3;
                        e.type = ui::InputEvent::Type::MouseMove;
                        send(e);
                        e.type = ui::InputEvent::Type::MouseDown;
                        send(e);
                        e.type = ui::InputEvent::Type::MouseUp;
                        send(e);
                    } else if (st.kind == "key") {
                        e.type = ui::InputEvent::Type::KeyDown;
                        e.key = scriptKey(st.key);
                        send(e);
                    }
                    if (app.quitting()) break;
                } else if (!shot.empty()) {
                    app::Screen* s = app.top();
                    const formats::Palette* own = s->palette();
                    const formats::Palette pal = own ? *own : data.palette;
                    formats::writePngRgba(shot, surface.width(), surface.height(), surface.toRgba(pal));
                    std::printf("saved %s\n", shot.string().c_str());
                    break;
                }
            }
            app.update(elapsed);
            app.applyChanges();
            if (app.quitting()) break;
            audio.update();

            screen = app.top();
            if (surface.width() != screen->width() || surface.height() != screen->height())
                surface.resize(screen->width(), screen->height());
            app.draw(surface);
            if (surface.width() != texW || surface.height() != texH) {
                SDL_DestroyTexture(texture);
                texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, surface.width(),
                                            surface.height());
                SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
                texW = surface.width();
                texH = surface.height();
            }
            const formats::Palette* own = screen->palette();
            const formats::Palette pal =
                own ? *own : data.cycle.applyAt(data.palette, screen->paletteCycling() ? now : 0);
            void* pixels = nullptr;
            int pitch = 0;
            if (SDL_LockTexture(texture, nullptr, &pixels, &pitch)) {
                const std::vector<std::uint8_t> rgba = surface.toRgba(pal);
                for (int y = 0; y < texH; ++y)
                    std::copy_n(rgba.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(texW) * 4,
                                static_cast<std::size_t>(texW) * 4, static_cast<std::uint8_t*>(pixels) + y * pitch);
                SDL_UnlockTexture(texture);
            }
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
            SDL_RenderClear(renderer);
            SDL_GetRenderOutputSize(renderer, &outW, &outH);
            const float kk = std::min(static_cast<float>(outW) / static_cast<float>(texW), static_cast<float>(outH) / static_cast<float>(texH));
            const SDL_FRect dst{(static_cast<float>(outW) - static_cast<float>(texW) * kk) / 2,
                                (static_cast<float>(outH) - static_cast<float>(texH) * kk) / 2, static_cast<float>(texW) * kk,
                                static_cast<float>(texH) * kk};
            SDL_RenderTexture(renderer, texture, nullptr, &dst);
            SDL_RenderPresent(renderer);
        }
        SDL_DestroyTexture(texture);
    }
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    fs::path gameDir = envPath("OPENDAY_GAME_DIR"), dataDir = envPath("OPENDAY_DATA_DIR");
    int scale = 0; // 0 = fit the screen
    bool intro = true, mute = false, cheats = false;
    std::string script;
    fs::path shot;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--no-intro") intro = false;
        else if (a == "--mute") mute = true;
        else if (a == "--cheats") cheats = true;
        else if (a == "--game-dir" && i + 1 < argc) gameDir = argv[++i];
        else if (a == "--data" && i + 1 < argc) dataDir = argv[++i];
        else if (a == "--scale" && i + 1 < argc) scale = std::clamp(std::atoi(argv[++i]), 1, 4);
        else if (a == "--script" && i + 1 < argc) script = argv[++i];
        else if (a == "--shot" && i + 1 < argc) shot = argv[++i];
        else {
            std::fprintf(stderr, "usage: openday [--game-dir DIR] [--data DIR] [--scale N] [--no-intro] [--mute]\n"
                                 "               [--script STEPS --shot OUT.png]\n");
            return 2;
        }
    }
    if (gameDir.empty() || dataDir.empty()) {
        std::fprintf(stderr, "set --game-dir / OPENDAY_GAME_DIR and --data / OPENDAY_DATA_DIR\n"
                             "(convert the databases first with tools/mdb/mdb2sqlite.py)\n");
        return 2;
    }
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    int rc = 0;
    try {
        app::GameData data = app::GameData::load(gameDir, dataDir);
        rc = run(data, scale, intro, mute, cheats, parseScript(script), shot);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        rc = 1;
    }
    SDL_Quit();
    return rc;
}
