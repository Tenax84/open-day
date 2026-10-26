#include "app/setup_ui.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <utility>

#include "app/setup.hpp"
#include "app/window_icon.hpp"
#include "formats/palette.hpp"

namespace openday::app::setup {
namespace {

namespace fs = std::filesystem;

std::unique_ptr<SetupUi> g_ui;
bool g_sharpText = true;

// Panel geometry: a 1-pixel outer line and a 2-pixel raised edge, the title
// strip below a gap, the content below the strip.
constexpr int kEdge = 3, kPad = 10, kStripGap = 7, kStripH = 24, kBelowStrip = 8;

// Camouflage: four tones (green, olive, brown, khaki), each in 8 shades for
// the grain.
constexpr formats::Rgb kCamoTones[4] = {{46, 58, 34}, {80, 80, 44}, {88, 66, 40}, {114, 106, 70}};

formats::Palette makePalette() {
    formats::Palette p{};
    const auto set = [&p](int i, std::uint8_t r, std::uint8_t g, std::uint8_t b) { p[static_cast<std::size_t>(i)] = {r, g, b}; };
    set(SetupUi::kBlack, 0, 0, 0);
    set(SetupUi::kText, 236, 226, 178);
    set(SetupUi::kTextDim, 150, 150, 150);
    set(SetupUi::kTitle, 226, 198, 112);
    set(SetupUi::kNote, 176, 214, 132);
    set(SetupUi::kEdgeLight, 200, 182, 118);
    set(SetupUi::kEdgeDark, 62, 52, 28);
    set(SetupUi::kEdgeOuter, 18, 16, 8);
    set(SetupUi::kFace, 86, 84, 44);
    set(SetupUi::kFaceHot, 106, 104, 56);
    set(SetupUi::kFaceDown, 66, 64, 34);
    set(SetupUi::kFaceOff, 48, 30, 14);
    set(SetupUi::kTextOff, 104, 80, 54);
    set(SetupUi::kWell, 10, 12, 8);
    set(SetupUi::kBar, 168, 148, 70);
    set(SetupUi::kBarLight, 222, 202, 116);
    set(SetupUi::kBarDark, 104, 90, 42);
    for (int tone = 0; tone < 4; ++tone)
        for (int shade = 0; shade < 8; ++shade) {
            const float k = 0.72f + 0.06f * static_cast<float>(shade);
            const formats::Rgb& c = kCamoTones[tone];
            const auto scale = [k](std::uint8_t v) { return static_cast<std::uint8_t>(std::min(255.0f, std::round(v * k))); };
            set(SetupUi::kCamo + tone * 8 + shade, scale(c.r), scale(c.g), scale(c.b));
        }
    return p;
}

const formats::Palette& palette() {
    static const formats::Palette p = makePalette();
    return p;
}

std::uint32_t hash(int x, int y, std::uint32_t seed) {
    std::uint32_t h = static_cast<std::uint32_t>(x) * 374761393u + static_cast<std::uint32_t>(y) * 668265263u + seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

// Smooth value noise in 0..1 with lattice spacing `cell`.
float noise(int x, int y, int cell, std::uint32_t seed) {
    const int gx = x / cell, gy = y / cell;
    const float fx = static_cast<float>(x % cell) / static_cast<float>(cell);
    const float fy = static_cast<float>(y % cell) / static_cast<float>(cell);
    const auto corner = [&](int cx, int cy) { return static_cast<float>(hash(cx, cy, seed) & 0xffff) / 65535.0f; };
    const auto smooth = [](float t) { return t * t * (3 - 2 * t); };
    const float sx = smooth(fx), sy = smooth(fy);
    const float top = corner(gx, gy) + (corner(gx + 1, gy) - corner(gx, gy)) * sx;
    const float bottom = corner(gx, gy + 1) + (corner(gx + 1, gy + 1) - corner(gx, gy + 1)) * sx;
    return top + (bottom - top) * sy;
}

// The camouflage of the whole screen; panels show the part under them.
render::IndexedSurface makeCamo() {
    render::IndexedSurface s(SetupUi::kWidth, SetupUi::kHeight);
    for (int y = 0; y < s.height(); ++y) {
        std::uint8_t* row = s.row(y);
        for (int x = 0; x < s.width(); ++x) {
            // Independent blotch fields, each with a ragged edge: green or
            // olive everywhere, brown and khaki patches over them.
            const auto field = [x, y](int cell, std::uint32_t seed) {
                return 0.65f * noise(x, y, cell, seed) + 0.25f * noise(x, y, cell / 3, seed + 1) +
                       0.10f * noise(x, y, 5, seed + 2);
            };
            int tone = field(56, 10) < 0.5f ? 0 : 1;
            if (field(44, 20) > 0.6f) tone = 2;
            if (field(30, 30) > 0.66f) tone = 3;
            const int shade = 2 + static_cast<int>(hash(x, y, 7) % 5u);
            row[x] = static_cast<std::uint8_t>(SetupUi::kCamo + tone * 8 + shade);
        }
    }
    return s;
}

// Lines of `text` no wider than `maxWidth`: the font's wrapping at spaces,
// then words wider than a line (paths) broken after a separator where one
// fits, else anywhere.
std::vector<std::string> wrapLong(const render::Font& font, const std::string& text, int maxWidth) {
    std::vector<std::string> out;
    for (const std::string& line : font.wrap(text, maxWidth)) {
        if (font.width(line) <= maxWidth) {
            out.push_back(line);
            continue;
        }
        const std::u32string chars = render::decodeUtf8(line);
        std::string cur;
        std::size_t breakAt = std::string::npos; // byte length of `cur` after the last separator
        for (const char32_t c : chars) {
            const std::string piece = render::encodeUtf8(c);
            if (!cur.empty() && font.width(cur + piece) > maxWidth) {
                if (breakAt != std::string::npos && breakAt > 0 && breakAt < cur.size()) {
                    out.push_back(cur.substr(0, breakAt));
                    cur.erase(0, breakAt);
                } else {
                    out.push_back(cur);
                    cur.clear();
                }
                breakAt = std::string::npos;
            }
            cur += piece;
            if (c == '\\' || c == '/' || c == ' ' || c == '_' || c == '-') breakAt = cur.size();
        }
        if (!cur.empty()) out.push_back(cur);
    }
    return out;
}

struct Choice {
    std::atomic<bool> done{false};
    bool failed = false;
    fs::path path;
};

void SDLCALL chosen(void* userdata, const char* const* filelist, int /*filter*/) {
    auto* choice = static_cast<Choice*>(userdata);
    if (!filelist) choice->failed = true;
    else if (filelist[0]) choice->path = fs::path(std::u8string(reinterpret_cast<const char8_t*>(filelist[0])));
    choice->done = true;
}

// While the window border is dragged the main loop stands still (a modal
// loop on Windows and macOS); the last frame is shown again in the new size.
bool SDLCALL redrawWhileResizing(void* userdata, SDL_Event* ev) {
    if (ev->type == SDL_EVENT_WINDOW_EXPOSED && ev->window.data1 == 1) static_cast<SetupUi*>(userdata)->present();
    return true;
}

} // namespace

SetupUi* SetupUi::open() {
    if (g_ui) return g_ui.get();
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    if (!SDL_CreateWindowAndRenderer("OpenDay", kWidth, kHeight, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN, &window, &renderer))
        return nullptr;
    setWindowIcon(window);
    placeWindow(window, 0);
    g_ui.reset(new SetupUi(window, renderer));
    return g_ui.get();
}

SetupUi* SetupUi::current() { return g_ui.get(); }

void SetupUi::setSharpText(bool on) { g_sharpText = on; }

bool SetupUi::release(SDL_Window*& window, SDL_Renderer*& renderer) {
    if (!g_ui) return false;
    SDL_RemoveEventWatch(redrawWhileResizing, g_ui.get());
    g_ui->sharp_.reset();
    window = std::exchange(g_ui->window_, nullptr);
    renderer = std::exchange(g_ui->renderer_, nullptr);
    SDL_DestroyTexture(std::exchange(g_ui->texture_, nullptr));
    g_ui.reset();
    return true;
}

void SetupUi::close() { g_ui.reset(); }

SetupUi::SetupUi(SDL_Window* window, SDL_Renderer* renderer)
    : window_(window), renderer_(renderer), surface_(kWidth, kHeight), shown_(kWidth, kHeight), camo_(makeCamo()),
      rgba_(static_cast<std::size_t>(kWidth) * kHeight * 4), body_(render::FontFace::Regular, 14),
      bold_(render::FontFace::Bold, 13), title_(render::FontFace::Bold, 16), small_(render::FontFace::Regular, 14) {
    vsync_ = SDL_SetRenderVSync(renderer_, 1);
    texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, kWidth, kHeight);
    SDL_SetTextureScaleMode(texture_, SDL_SCALEMODE_PIXELART);
    if (g_sharpText) sharp_ = std::make_unique<SharpText>(renderer_);
    SDL_AddEventWatch(redrawWhileResizing, this);
}

SetupUi::~SetupUi() {
    SDL_RemoveEventWatch(redrawWhileResizing, this);
    render::TextLayer::setActive(nullptr);
    sharp_.reset();
    if (texture_) SDL_DestroyTexture(texture_);
    if (renderer_) SDL_DestroyRenderer(renderer_);
    if (window_) SDL_DestroyWindow(window_);
}

void* SetupUi::nativeWindow() const {
    return SDL_GetPointerProperty(SDL_GetWindowProperties(window_), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
}

// Window coordinates -> screen pixels: the screen is scaled to fit and
// centred, as in the game.
void SetupUi::toLogical(float x, float y, int& lx, int& ly) const {
    int outW = 0, outH = 0;
    SDL_GetRenderOutputSize(renderer_, &outW, &outH);
    const float k = std::min(static_cast<float>(outW) / kWidth, static_cast<float>(outH) / kHeight);
    if (k <= 0) {
        lx = ly = -1;
        return;
    }
    float density = SDL_GetWindowPixelDensity(window_);
    if (density <= 0) density = 1;
    const float ox = (static_cast<float>(outW) - kWidth * k) / 2, oy = (static_cast<float>(outH) - kHeight * k) / 2;
    lx = static_cast<int>(std::floor((x * density - ox) / k));
    ly = static_cast<int>(std::floor((y * density - oy) / k));
}

void SetupUi::begin() {
    escape_ = enter_ = released_ = false;
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED: closing_ = true; break;
        case SDL_EVENT_MOUSE_MOTION: toLogical(ev.motion.x, ev.motion.y, mouseX_, mouseY_); break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (ev.button.button == SDL_BUTTON_LEFT) {
                toLogical(ev.button.x, ev.button.y, downX_, downY_);
                mouseX_ = downX_;
                mouseY_ = downY_;
                down_ = true;
            }
            break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (ev.button.button == SDL_BUTTON_LEFT && down_) {
                toLogical(ev.button.x, ev.button.y, upX_, upY_);
                down_ = false;
                released_ = true;
            }
            break;
        case SDL_EVENT_WINDOW_MOUSE_LEAVE: mouseX_ = mouseY_ = -1; break;
        case SDL_EVENT_KEY_DOWN:
            if (ev.key.key == SDLK_ESCAPE) escape_ = true;
            else if (ev.key.key == SDLK_RETURN || ev.key.key == SDLK_KP_ENTER) enter_ = true;
            break;
        default: break;
        }
    }
    surface_.fill(kBlack);
    textLayer_.clear();
    if (sharp_) {
        textLayer_.attach(surface_, 0, 0);
        render::TextLayer::setActive(&textLayer_);
    }
}

void SetupUi::end() {
    render::TextLayer::setActive(nullptr);
    shown_ = surface_;
    if (sharp_) {
        display_ = shown_;
        textLayer_.resolve(display_);
    }
    present();
}

void SetupUi::present() {
    (sharp_ ? display_ : shown_).toRgba(palette(), rgba_.data());
    SDL_UpdateTexture(texture_, nullptr, rgba_.data(), kWidth * 4);
    int outW = 0, outH = 0;
    SDL_GetRenderOutputSize(renderer_, &outW, &outH);
    const float k = std::min(static_cast<float>(outW) / kWidth, static_cast<float>(outH) / kHeight);
    const SDL_FRect dst{(static_cast<float>(outW) - kWidth * k) / 2, (static_cast<float>(outH) - kHeight * k) / 2, kWidth * k,
                        kHeight * k};
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
    SDL_RenderClear(renderer_);
    SDL_RenderTexture(renderer_, texture_, nullptr, &dst);
    if (sharp_) sharp_->draw(textLayer_, palette(), dst, k);
    SDL_RenderPresent(renderer_);
    if (!vsync_) SDL_Delay(16);
}

void SetupUi::fill(const ui::Rect& r, Colour c) {
    const int x0 = std::max(r.x, 0), x1 = std::min(r.x + r.w, kWidth);
    for (int y = std::max(r.y, 0); y < std::min(r.y + r.h, kHeight); ++y)
        if (x1 > x0) std::memset(surface_.row(y) + x0, c, static_cast<std::size_t>(x1 - x0));
}

void SetupUi::camo(const ui::Rect& r) {
    const int x0 = std::max(r.x, 0), x1 = std::min(r.x + r.w, kWidth);
    for (int y = std::max(r.y, 0); y < std::min(r.y + r.h, kHeight); ++y)
        if (x1 > x0) std::memcpy(surface_.row(y) + x0, camo_.row(y) + x0, static_cast<std::size_t>(x1 - x0));
}

// A 1-pixel frame: `topLeft` along the top and left, `bottomRight` along the
// bottom and right.
void SetupUi::edges(const ui::Rect& r, Colour topLeft, Colour bottomRight) {
    fill({r.x, r.y, r.w, 1}, topLeft);
    fill({r.x, r.y, 1, r.h}, topLeft);
    fill({r.x, r.y + r.h - 1, r.w, 1}, bottomRight);
    fill({r.x + r.w - 1, r.y + 1, 1, r.h - 1}, bottomRight);
}

// Capitals with extra space between the letters, centred in the box; a dark
// shadow under them.
void SetupUi::spaced(const render::Font& font, const ui::Rect& box, const std::string& text, int spacing, Colour colour,
                     int dy) {
    const std::u32string chars = render::decodeUtf8(text);
    int width = 0;
    for (const char32_t c : chars) width += font.width(render::encodeUtf8(c)) + spacing;
    if (!chars.empty()) width -= spacing;
    const int y = box.y + (box.h - font.lineHeight()) / 2 + dy;
    for (const bool shadow : {true, false}) {
        int x = box.x + (box.w - width) / 2 + dy;
        for (const char32_t c : chars) {
            const std::string piece = render::encodeUtf8(c);
            font.draw(surface_, shadow ? x + 1 : x, shadow ? y + 1 : y, piece, shadow ? kEdgeOuter : colour);
            x += font.width(piece) + spacing;
        }
    }
}

ui::Rect SetupUi::panel(const ui::Rect& r, const std::string& title) {
    edges(r, kEdgeOuter, kEdgeOuter);
    const ui::Rect inner{r.x + 1, r.y + 1, r.w - 2, r.h - 2};
    edges(inner, kEdgeLight, kEdgeDark);
    edges({inner.x + 1, inner.y + 1, inner.w - 2, inner.h - 2}, kEdgeLight, kEdgeDark);
    camo({r.x + kEdge, r.y + kEdge, r.w - 2 * kEdge, r.h - 2 * kEdge});
    int top = r.y + kEdge + kPad;
    if (!title.empty()) {
        // The title strip: sunk, with a raised bar at each end.
        const ui::Rect strip{r.x + kEdge + kStripGap, r.y + kEdge + kStripGap, r.w - 2 * (kEdge + kStripGap), kStripH};
        edges(strip, kEdgeDark, kEdgeLight);
        for (const int bx : {strip.x + 4, strip.x + strip.w - 4 - 5}) {
            const ui::Rect bar{bx, strip.y + 4, 5, strip.h - 8};
            fill(bar, kEdgeLight);
            edges(bar, kEdgeLight, kEdgeDark);
        }
        spaced(title_, strip, title, 2, kTitle);
        top = strip.y + strip.h + kBelowStrip;
    }
    return {r.x + kEdge + kPad, top, r.w - 2 * (kEdge + kPad), r.y + r.h - kEdge - kPad - top};
}

void SetupUi::well(const ui::Rect& r) {
    edges(r, kEdgeOuter, kEdgeLight);
    fill({r.x + 1, r.y + 1, r.w - 2, r.h - 2}, kWell);
}

int SetupUi::text(const ui::Rect& box, const std::string& text, Colour colour) {
    int y = box.y;
    for (const std::string& line : wrapLong(body_, text, box.w)) {
        if (y + body_.lineHeight() > box.y + box.h) break;
        if (!line.empty()) body_.draw(surface_, box.x, y, line, colour);
        y += body_.lineHeight();
    }
    return y - box.y;
}

bool SetupUi::button(const ui::Rect& r, const std::string& label, bool enabled) {
    const bool hot = enabled && hovered(r);
    const bool held = hot && down_ && r.contains(downX_, downY_);
    const bool clicked = enabled && released_ && r.contains(downX_, downY_) && r.contains(upX_, upY_);
    edges(r, kEdgeOuter, kEdgeOuter);
    const ui::Rect inner{r.x + 1, r.y + 1, r.w - 2, r.h - 2};
    fill(inner, !enabled ? kFaceOff : held ? kFaceDown : hot ? kFaceHot : kFace);
    if (held) edges(inner, kEdgeDark, kEdgeLight);
    else edges(inner, enabled ? kEdgeLight : kEdgeDark, kEdgeDark);
    spaced(bold_, inner, label, 1, !enabled ? kTextOff : hot ? kTitle : kText, held ? 1 : 0);
    return clicked;
}

void SetupUi::progress(const ui::Rect& r, float fraction) {
    well(r);
    const int w = static_cast<int>(std::lround(static_cast<float>(r.w - 4) * std::clamp(fraction, 0.0f, 1.0f)));
    if (w <= 0) return;
    const ui::Rect bar{r.x + 2, r.y + 2, w, r.h - 4};
    fill(bar, kBar);
    edges(bar, kBarLight, kBarDark);
}

void SetupUi::footer(const std::string& text) { small_.draw(surface_, 4, kHeight - 18, text, kTextDim); }

// Every other pixel black, like the game's shaded backgrounds.
void SetupUi::shade(render::IndexedSurface& s) const {
    for (int y = 0; y < s.height(); ++y) {
        std::uint8_t* row = s.row(y);
        for (int x = y & 1; x < s.width(); x += 2) row[x] = kBlack;
    }
}

int SetupUi::dialog(const std::string& title, const std::string& message, const std::vector<std::string>& buttons,
                    int escapeIndex) {
    render::IndexedSurface back = shown_;
    shade(back);
    constexpr int kW = 440, kButtonW = 120, kButtonH = 24, kGap = 10;
    const int innerW = kW - 2 * (kEdge + kPad);
    const int lines = static_cast<int>(wrapLong(body_, message, innerW - 16).size());
    const int textH = std::min(lines, 16) * body_.lineHeight();
    const int top = kEdge + kStripGap + kStripH + kBelowStrip;
    const int h = top + textH + 16 + 12 + kButtonH + kEdge + kPad;
    const ui::Rect box{(kWidth - kW) / 2, (kHeight - h) / 2, kW, h};
    for (;;) {
        begin();
        if (closing_ || escape_) return escapeIndex;
        if (enter_) return 0;
        surface_ = back;
        const ui::Rect in = panel(box, title);
        const ui::Rect w{in.x, in.y, in.w, textH + 16};
        well(w);
        text({w.x + 8, w.y + 8, w.w - 16, textH}, message);
        int x = in.x + in.w - static_cast<int>(buttons.size()) * (kButtonW + kGap) + kGap;
        int pressed = -1;
        for (std::size_t i = 0; i < buttons.size(); ++i, x += kButtonW + kGap)
            if (button({x, w.y + w.h + 12, kButtonW, kButtonH}, buttons[i])) pressed = static_cast<int>(i);
        end();
        if (pressed >= 0) return pressed;
    }
}

fs::path SetupUi::waitForChoice(bool folder, const std::vector<std::pair<const char*, const char*>>& filters,
                                bool& unavailable) {
    std::vector<SDL_DialogFileFilter> list;
    for (const auto& [name, pattern] : filters) list.push_back({name, pattern});
    Choice choice;
    if (folder) SDL_ShowOpenFolderDialog(chosen, &choice, window_, nullptr, false);
    else SDL_ShowOpenFileDialog(chosen, &choice, window_, list.data(), static_cast<int>(list.size()), nullptr, false);
    render::IndexedSurface back = shown_;
    shade(back);
    while (!choice.done) {
        begin();
        surface_ = back;
        end();
    }
    unavailable = choice.failed;
    return choice.path;
}

fs::path SetupUi::chooseFile(const std::vector<std::pair<const char*, const char*>>& filters, bool& unavailable) {
    return waitForChoice(false, filters, unavailable);
}

fs::path SetupUi::chooseFolder(bool& unavailable) { return waitForChoice(true, {}, unavailable); }

} // namespace openday::app::setup
