// Interface controls built from atlas frames: push buttons, toggles and
// radio groups, list boxes and text boxes (docs/mechanics/interface.md,
// "Menu mechanics"). Independent of the windowing system: the application
// translates its events into InputEvent.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "render/font.hpp"
#include "render/indexed_surface.hpp"
#include "ui/atlas.hpp"

namespace openday::ui {

struct Rect {
    int x = 0, y = 0, w = 0, h = 0;
    bool contains(int px, int py) const { return px >= x && py >= y && px < x + w && py < y + h; }
};

// Key codes: printable ASCII as lower case, plus these.
enum Key : int {
    kKeyEnter = 13,
    kKeyEscape = 27,
    kKeyBackspace = 8,
    kKeyLeft = 256,
    kKeyRight,
    kKeyUp,
    kKeyDown,
    kKeyPageUp,
    kKeyPageDown,
    kKeyTab,
    kKeyHome,
    kKeyEnd,
    kKeyF1,
    kKeyF2,
    kKeyF3,
    kKeyF4,
    kKeyF5,
    kKeyF6,
};

struct InputEvent {
    enum class Type { MouseMove, MouseDown, MouseUp, KeyDown, Wheel };
    Type type = Type::MouseMove;
    int x = 0, y = 0; // logical screen pixels
    int button = 1;   // 1 left, 2 middle, 3 right
    int key = 0;      // KeyDown: Key or lower-case ASCII; Wheel: +1 up, -1 down
    bool shift = false, ctrl = false;
};

// Shared drawing resources of the screens.
struct UiContext {
    const InterfaceAtlas* atlas = nullptr;
    const render::Font* standard = nullptr; // Arial 14 substitute
    const render::Font* large = nullptr;    // Arial 17 substitute
    std::uint8_t white = 255;
    std::uint8_t black = 0;
    std::uint8_t grey = 7;      // fallback controls
    std::uint8_t highlight = 9; // list selection
};

// Draws an atlas frame, or a plain box when the frame is missing.
void drawFrame(render::IndexedSurface& s, const UiContext& ui, int frame, const Rect& r);

// A push button or, with `toggle`, a toggle / radio item. Frames: first + 0
// normal, +1 hover, +2 pressed, +3 disabled; a checked toggle shows +4
// instead of the normal image (also while hovered *(OpenDay: the atlas
// often has no +5 frame)*). The action runs on release inside the button.
class Button {
public:
    Rect rect;
    int firstFrame = -1;
    int hotkey = 0;
    std::string caption; // drawn only when the atlas has no frame
    bool enabled = true;
    bool toggle = false;
    bool checked = false;
    std::function<void()> onClick;

    // Returns true if the event was used.
    bool handle(const InputEvent& e);
    void draw(render::IndexedSurface& s, const UiContext& ui) const;

private:
    enum class State { Normal, Hover, Pressed } state_ = State::Normal;
};

// Radio group: checking one item unchecks the others.
class RadioGroup {
public:
    std::vector<Button> items;
    std::function<void(int)> onChange;

    int checked() const;
    void check(int index);
    bool handle(const InputEvent& e);
    void draw(render::IndexedSurface& s, const UiContext& ui) const;
};

// A list of text rows; click selects, double use of the same row "activates".
class ListBox {
public:
    Rect rect;
    std::vector<std::string> rows;
    int selected = -1;
    int top = 0; // first visible row
    int rowHeight = 17;
    std::function<void(int)> onSelect;

    int visibleRows() const { return rowHeight > 0 ? rect.h / rowHeight : 0; }
    void scroll(int delta);
    bool handle(const InputEvent& e);
    void draw(render::IndexedSurface& s, const UiContext& ui) const;
};

// Word-wrapped text with a scroll offset.
class TextBox {
public:
    Rect rect;
    std::string text;
    bool largeFont = false;
    int firstLine = 0;

    bool handle(const InputEvent& e);
    void draw(render::IndexedSurface& s, const UiContext& ui) const;
};

} // namespace openday::ui
