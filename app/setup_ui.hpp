// The window of the first start (docs/mechanics/interface.md, "Setup
// window"): one 640 x 480 screen, scaled into the window like the game's,
// whose pages (the choice of the game files, the GOG sign-in and download,
// the copy and the conversion) change in it, with dialogs over them. The
// game's own art is not there yet, so the look of its menus is drawn here:
// camouflage panels with raised edges, a title strip, olive buttons with
// spaced capitals, black text wells.
//
// Immediate mode: a page loop calls begin(), draws (button() returns true
// when clicked), then end(). Everything runs on the main thread.
#pragma once

#include <SDL3/SDL.h>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "app/sharp_text.hpp"
#include "render/font.hpp"
#include "render/indexed_surface.hpp"
#include "render/text_layer.hpp"
#include "ui/widgets.hpp"

namespace openday::app::setup {

class SetupUi {
public:
    static constexpr int kWidth = 640, kHeight = 480;

    // Palette indices of the drawing.
    enum Colour : std::uint8_t {
        kBlack,
        kText,      // body text
        kTextDim,   // the footer, hints
        kTitle,     // title strips, button labels under the pointer
        kNote,      // a line that answers the user
        kEdgeLight, // raised edges: top and left
        kEdgeDark,  // raised edges: bottom and right
        kEdgeOuter, // the line around panels and buttons
        kFace,      // button face
        kFaceHot,
        kFaceDown,
        kFaceOff,   // a button that cannot be pressed
        kTextOff,
        kWell,      // text wells and the progress bar's groove
        kBar,
        kBarLight,
        kBarDark,
        kCamo = 32, // 4 tones x 8 shades
    };

    // The window, opened (centred, scaled to the display) on first use. Null
    // when no window can be made.
    static SetupUi* open();
    // Text drawn smooth at the window's resolution (the "Sharp text" option,
    // docs/mechanics/interface.md); set before open().
    static void setSharpText(bool on);
    static SetupUi* current(); // null unless open
    // Gives the window and its renderer away (to the game) and closes the
    // rest; false (nothing given) when it was not open.
    static bool release(SDL_Window*& window, SDL_Renderer*& renderer);
    // Closes the window if open.
    static void close();

    SDL_Window* window() const { return window_; }
    void* nativeWindow() const; // the HWND on Windows; else null

    // Starts a frame: takes the events and clears the screen.
    void begin();
    // Shows the frame.
    void end();
    // Shows the last frame again (in the window's new size).
    void present();
    // True from the moment the user closed the window.
    bool closing() const { return closing_; }
    bool escapePressed() const { return escape_; }
    bool enterPressed() const { return enter_; }

    render::IndexedSurface& surface() { return surface_; }
    const render::Font& body() const { return body_; }

    // A camouflage panel with raised edges; a title strip on top when
    // `title` is not empty. Returns the area inside the edges below it.
    ui::Rect panel(const ui::Rect& r, const std::string& title);
    // A sunk black well.
    void well(const ui::Rect& r);
    // Wrapped text from the top of the box (long words broken); returns the
    // height used.
    int text(const ui::Rect& box, const std::string& text, Colour colour = kText);
    // A push button; true when it was clicked (pressed and released on it).
    bool button(const ui::Rect& r, const std::string& label, bool enabled = true);
    bool hovered(const ui::Rect& r) const { return r.contains(mouseX_, mouseY_); }
    // The rest of this frame takes no pointer: nothing is hovered or clicked
    // (a page drawn under a panel that has the input).
    void ignoreMouse() {
        mouseX_ = mouseY_ = -1;
        down_ = released_ = false;
    }
    // Shades what is drawn so far, as behind a dialog.
    void shade() { shade(surface_); }
    // A progress bar in a well; `fraction` in 0..1.
    void progress(const ui::Rect& r, float fraction);
    // The small line at the bottom of the screen.
    void footer(const std::string& text);

    // A dialog over the last frame, which is shaded. Returns the index of the
    // button pressed; Enter gives the first, Escape or closing the window
    // `escapeIndex`.
    int dialog(const std::string& title, const std::string& message, const std::vector<std::string>& buttons,
               int escapeIndex);
    void message(const std::string& title, const std::string& text) { dialog(title, text, {"OK"}, 0); }

    // The system's file or folder dialog over the window, which stays drawn
    // (shaded) behind it. Empty when cancelled; `unavailable` is set when the
    // system has no dialog.
    std::filesystem::path chooseFile(const std::vector<std::pair<const char*, const char*>>& filters, bool& unavailable);
    std::filesystem::path chooseFolder(bool& unavailable);

    ~SetupUi();

private:
    SetupUi(SDL_Window* window, SDL_Renderer* renderer);
    void toLogical(float x, float y, int& lx, int& ly) const;
    void spaced(const render::Font& font, const ui::Rect& box, const std::string& text, int spacing, Colour colour,
                int dy = 0);
    void camo(const ui::Rect& r);
    void fill(const ui::Rect& r, Colour c);
    void edges(const ui::Rect& r, Colour topLeft, Colour bottomRight);
    void shade(render::IndexedSurface& s) const;
    std::filesystem::path waitForChoice(bool folder, const std::vector<std::pair<const char*, const char*>>& filters,
                                        bool& unavailable);

    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    SDL_Texture* texture_ = nullptr;
    bool vsync_ = false;
    render::IndexedSurface surface_, shown_, camo_;
    render::IndexedSurface display_; // with sharp text: the frame without its pixel text
    render::TextLayer textLayer_;
    std::unique_ptr<SharpText> sharp_;
    std::vector<std::uint8_t> rgba_;
    render::Font body_, bold_, title_, small_;
    bool closing_ = false, escape_ = false, enter_ = false;
    int mouseX_ = -1, mouseY_ = -1;
    bool down_ = false, released_ = false;
    int downX_ = -1, downY_ = -1, upX_ = -1, upY_ = -1;
};

} // namespace openday::app::setup
