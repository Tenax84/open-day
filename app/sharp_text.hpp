// Sharp text (docs/mechanics/interface.md, "Sharp text"): draws the texts of
// a frame (render/text_layer.hpp) anti-aliased at the window's resolution,
// over the scaled picture, where the pixel text was visible.
#pragma once

#include <SDL3/SDL.h>

#include <map>
#include <memory>
#include <tuple>
#include <vector>

#include "formats/palette.hpp"
#include "render/text_layer.hpp"

namespace openday::app {

class SharpText {
public:
    explicit SharpText(SDL_Renderer* renderer) : renderer_(renderer) {}
    ~SharpText();
    SharpText(const SharpText&) = delete;
    SharpText& operator=(const SharpText&) = delete;

    // `picture`: where the frame is shown in the window (output pixels),
    // `scale` output pixels per frame pixel.
    void draw(const render::TextLayer& layer, const formats::Palette& palette, const SDL_FRect& picture, float scale);

private:
    // A glyph's place in the atlas texture.
    struct Slot {
        int x = 0, y = 0;
    };
    const render::SmoothFont& font(render::FontFace face, int quarterPixels);
    // The glyph's slot, nullptr when the atlas is full.
    const Slot* slot(render::FontFace face, int quarterPixels, char32_t c, const render::SmoothFont::Glyph& g);
    void reset();

    SDL_Renderer* renderer_;
    SDL_Texture* atlas_ = nullptr;
    int shelfX_ = 0, shelfY_ = 0, shelfH_ = 0;
    std::map<std::pair<render::FontFace, int>, std::unique_ptr<render::SmoothFont>> fonts_;
    std::map<std::tuple<render::FontFace, int, char32_t>, Slot> slots_;
    std::vector<SDL_Vertex> vertices_;
    std::vector<int> indices_;
};

} // namespace openday::app
