#include "ui/widgets.hpp"

#include <algorithm>

namespace openday::ui {
namespace {

void fillRect(render::IndexedSurface& s, const Rect& r, std::uint8_t color) {
    for (int y = std::max(0, r.y); y < std::min(s.height(), r.y + r.h); ++y)
        for (int x = std::max(0, r.x); x < std::min(s.width(), r.x + r.w); ++x) s.row(y)[x] = color;
}

void frameRect(render::IndexedSurface& s, const Rect& r, std::uint8_t color) {
    for (int x = r.x; x < r.x + r.w; ++x)
        for (const int y : {r.y, r.y + r.h - 1})
            if (x >= 0 && y >= 0 && x < s.width() && y < s.height()) s.row(y)[x] = color;
    for (int y = r.y; y < r.y + r.h; ++y)
        for (const int x : {r.x, r.x + r.w - 1})
            if (x >= 0 && y >= 0 && x < s.width() && y < s.height()) s.row(y)[x] = color;
}

} // namespace

void drawFrame(render::IndexedSurface& s, const UiContext& ui, int frame, const Rect& r) {
    if (const formats::DbiSprite* sprite = ui.atlas ? ui.atlas->get(frame) : nullptr) {
        s.blit(*sprite, r.x, r.y);
        return;
    }
    fillRect(s, r, ui.black);
    frameRect(s, r, ui.grey);
}

bool Button::handle(const InputEvent& e) {
    if (!enabled) {
        state_ = State::Normal;
        return false;
    }
    const bool inside = rect.contains(e.x, e.y);
    switch (e.type) {
    case InputEvent::Type::MouseMove:
        if (!inside) state_ = State::Normal;
        else if (state_ == State::Normal) state_ = State::Hover;
        return false;
    case InputEvent::Type::MouseDown:
        if (inside && e.button == 1) {
            state_ = State::Pressed;
            return true;
        }
        return false;
    case InputEvent::Type::MouseUp:
        if (e.button != 1) return false;
        if (state_ == State::Pressed && inside) {
            state_ = State::Hover;
            if (onClick) onClick();
            return true;
        }
        state_ = inside ? State::Hover : State::Normal;
        return false;
    case InputEvent::Type::KeyDown:
        if (hotkey != 0 && e.key == hotkey) {
            if (onClick) onClick();
            return true;
        }
        return false;
    default:
        return false;
    }
}

void Button::draw(render::IndexedSurface& s, const UiContext& ui) const {
    int offset = 0;
    if (!enabled) offset = 3;
    else if (state_ == State::Pressed) offset = 2;
    else if (toggle && checked) offset = 4;
    else if (state_ == State::Hover) offset = 1;
    if (firstFrame >= 0 && ui.atlas && ui.atlas->get(firstFrame + offset)) {
        drawFrame(s, ui, firstFrame + offset, rect);
        return;
    }
    // No art: a box with the caption.
    fillRect(s, rect, toggle && checked ? ui.highlight : ui.black);
    frameRect(s, rect, offset == 1 || offset == 2 ? ui.white : ui.grey);
    if (ui.standard && !caption.empty())
        ui.standard->drawIn(s, rect.x, rect.y, rect.w, rect.h, caption, enabled ? ui.white : ui.grey);
}

int RadioGroup::checked() const {
    for (std::size_t i = 0; i < items.size(); ++i)
        if (items[i].checked) return static_cast<int>(i);
    return -1;
}

void RadioGroup::check(int index) {
    for (std::size_t i = 0; i < items.size(); ++i) items[i].checked = static_cast<int>(i) == index;
}

bool RadioGroup::handle(const InputEvent& e) {
    for (std::size_t i = 0; i < items.size(); ++i) {
        Button& b = items[i];
        b.toggle = true;
        auto saved = b.onClick;
        const int index = static_cast<int>(i);
        b.onClick = [this, index] {
            check(index);
            if (onChange) onChange(index);
        };
        const bool used = b.handle(e);
        b.onClick = saved;
        if (used) return true;
    }
    return false;
}

void RadioGroup::draw(render::IndexedSurface& s, const UiContext& ui) const {
    for (const Button& b : items) b.draw(s, ui);
}

void ListBox::scroll(int delta) {
    const int maxTop = std::max(0, static_cast<int>(rows.size()) - visibleRows());
    top = std::clamp(top + delta, 0, maxTop);
}

bool ListBox::handle(const InputEvent& e) {
    if (e.type == InputEvent::Type::Wheel && rect.contains(e.x, e.y)) {
        scroll(-e.key);
        return true;
    }
    if (e.type == InputEvent::Type::MouseDown && e.button == 1 && rect.contains(e.x, e.y) && rowHeight > 0) {
        const int row = top + (e.y - rect.y) / rowHeight;
        if (row >= 0 && row < static_cast<int>(rows.size())) {
            selected = row;
            if (onSelect) onSelect(row);
        }
        return true;
    }
    if (e.type == InputEvent::Type::KeyDown && (e.key == kKeyUp || e.key == kKeyDown) && !rows.empty()) {
        selected = std::clamp(selected + (e.key == kKeyDown ? 1 : -1), 0, static_cast<int>(rows.size()) - 1);
        if (selected < top) top = selected;
        if (selected >= top + visibleRows()) top = selected - visibleRows() + 1;
        if (onSelect) onSelect(selected);
        return true;
    }
    return false;
}

void ListBox::draw(render::IndexedSurface& s, const UiContext& ui) const {
    if (!ui.standard) return;
    for (int i = 0; i < visibleRows(); ++i) {
        const int row = top + i;
        if (row >= static_cast<int>(rows.size())) break;
        const Rect r{rect.x, rect.y + i * rowHeight, rect.w, rowHeight};
        if (row == selected) fillRect(s, r, ui.highlight);
        ui.standard->drawIn(s, r.x + 2, r.y, r.w - 4, r.h, rows[static_cast<std::size_t>(row)], ui.white, render::Align::Left);
    }
}

bool TextBox::handle(const InputEvent& e) {
    if (e.type == InputEvent::Type::Wheel && rect.contains(e.x, e.y)) {
        firstLine = std::max(0, firstLine - e.key);
        return true;
    }
    return false;
}

void TextBox::draw(render::IndexedSurface& s, const UiContext& ui) const {
    const render::Font* font = largeFont ? ui.large : ui.standard;
    if (!font) return;
    const std::vector<std::string> lines = font->wrap(text, rect.w);
    const int visible = rect.h / font->lineHeight();
    for (int i = 0; i < visible; ++i) {
        const int line = firstLine + i;
        if (line >= static_cast<int>(lines.size())) break;
        font->draw(s, rect.x, rect.y + i * font->lineHeight(), lines[static_cast<std::size_t>(line)], ui.white);
    }
}

} // namespace openday::ui
