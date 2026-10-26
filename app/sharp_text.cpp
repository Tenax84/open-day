#include "app/sharp_text.hpp"

#include <algorithm>
#include <cmath>

namespace openday::app {
namespace {

constexpr int kAtlasSize = 2048;
constexpr std::size_t kMaxFonts = 12; // sizes kept (the window may be resized often)

} // namespace

SharpText::~SharpText() {
    if (atlas_) SDL_DestroyTexture(atlas_);
}

void SharpText::reset() {
    slots_.clear();
    shelfX_ = shelfY_ = shelfH_ = 0;
    if (fonts_.size() > kMaxFonts) fonts_.clear();
}

const render::SmoothFont& SharpText::font(render::FontFace face, int quarterPixels) {
    std::unique_ptr<render::SmoothFont>& f = fonts_[{face, quarterPixels}];
    if (!f) f = std::make_unique<render::SmoothFont>(face, static_cast<float>(quarterPixels) / 4);
    return *f;
}

const SharpText::Slot* SharpText::slot(render::FontFace face, int quarterPixels, char32_t c,
                                       const render::SmoothFont::Glyph& g) {
    const auto key = std::make_tuple(face, quarterPixels, c);
    if (const auto it = slots_.find(key); it != slots_.end()) return &it->second;
    if (!atlas_) {
        atlas_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, kAtlasSize, kAtlasSize);
        if (!atlas_) return nullptr;
        SDL_SetTextureBlendMode(atlas_, SDL_BLENDMODE_BLEND);
        // Glyphs are drawn at their own size, texel for pixel.
        SDL_SetTextureScaleMode(atlas_, SDL_SCALEMODE_NEAREST);
    }
    // Shelves, with one pixel between the glyphs.
    if (shelfX_ + g.width + 1 > kAtlasSize) {
        shelfY_ += shelfH_ + 1;
        shelfX_ = shelfH_ = 0;
    }
    if (g.width + 1 > kAtlasSize || shelfY_ + g.height + 1 > kAtlasSize) return nullptr;
    Slot s{shelfX_, shelfY_};
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(g.width) * static_cast<std::size_t>(g.height) * 4, 255);
    for (std::size_t i = 0; i < g.coverage.size(); ++i) rgba[i * 4 + 3] = g.coverage[i];
    const SDL_Rect r{s.x, s.y, g.width, g.height};
    SDL_UpdateTexture(atlas_, &r, rgba.data(), g.width * 4);
    shelfX_ += g.width + 1;
    shelfH_ = std::max(shelfH_, g.height);
    return &slots_.emplace(key, s).first->second;
}

void SharpText::draw(const render::TextLayer& layer, const formats::Palette& palette, const SDL_FRect& picture, float scale) {
    if (scale <= 0) return;
    // A full atlas is started again, and the frame with it.
    for (int attempt = 0; attempt < 2; ++attempt) {
        vertices_.clear();
        indices_.clear();
        bool full = false;
        for (const render::TextLayer::Item& item : layer.items()) {
            if (!item.anyVisible || item.text.empty()) continue;
            const int quarter = std::max(4, static_cast<int>(std::lround(static_cast<float>(item.cellHeight) * scale * 4)));
            const render::SmoothFont& f = font(item.face, quarter);
            // The anti-aliased line is spread to the pixel line's width, so
            // that aligned text keeps its place.
            float natural = 0;
            for (const char32_t c : item.text) natural += f.glyph(c).advance;
            const float spread = natural > 0 ? static_cast<float>(item.width) * scale / natural : 1;
            const formats::Rgb rgb = palette[item.color];
            const SDL_FColor color{static_cast<float>(rgb.r) / 255, static_cast<float>(rgb.g) / 255,
                                   static_cast<float>(rgb.b) / 255, 1};
            float pen = picture.x + static_cast<float>(item.x) * scale;
            const int baseline = static_cast<int>(std::lround(picture.y + static_cast<float>(item.y) * scale + f.baseline()));

            // Whether the frame pixel under the output pixel (px, py) showed the text.
            auto visible = [&](int px, int py) {
                const int lx = static_cast<int>(std::floor((static_cast<float>(px) + 0.5f - picture.x) / scale)) - item.boxX;
                const int ly = static_cast<int>(std::floor((static_cast<float>(py) + 0.5f - picture.y) / scale)) - item.boxY;
                if (lx < 0 || ly < 0 || lx >= item.boxW || ly >= item.boxH) return false;
                return item.visible[static_cast<std::size_t>(ly) * static_cast<std::size_t>(item.boxW) +
                                    static_cast<std::size_t>(lx)] != 0;
            };
            for (const char32_t c : item.text) {
                const render::SmoothFont::Glyph& g = f.glyph(c);
                const int gx = static_cast<int>(std::lround(pen)) + g.left, gy = baseline + g.top;
                pen += g.advance * spread;
                if (g.width <= 0 || g.height <= 0) continue;
                const Slot* s = slot(item.face, quarter, c, g);
                if (!s) {
                    full = true;
                    continue;
                }
                // Output rectangle [x0, x1) x [y0, y1) of the glyph.
                auto quad = [&](int x0, int y0, int x1, int y1) {
                    const auto first = static_cast<int>(vertices_.size());
                    const float tx0 = static_cast<float>(s->x + x0 - gx) / kAtlasSize;
                    const float ty0 = static_cast<float>(s->y + y0 - gy) / kAtlasSize;
                    const float tx1 = static_cast<float>(s->x + x1 - gx) / kAtlasSize;
                    const float ty1 = static_cast<float>(s->y + y1 - gy) / kAtlasSize;
                    const auto fx0 = static_cast<float>(x0), fy0 = static_cast<float>(y0);
                    const auto fx1 = static_cast<float>(x1), fy1 = static_cast<float>(y1);
                    vertices_.push_back({{fx0, fy0}, color, {tx0, ty0}});
                    vertices_.push_back({{fx1, fy0}, color, {tx1, ty0}});
                    vertices_.push_back({{fx1, fy1}, color, {tx1, ty1}});
                    vertices_.push_back({{fx0, fy1}, color, {tx0, ty1}});
                    for (const int i : {0, 1, 2, 0, 2, 3}) indices_.push_back(first + i);
                };
                if (item.allVisible) {
                    quad(gx, gy, gx + g.width, gy + g.height);
                    continue;
                }
                // Partly covered: only over the frame pixels that showed the
                // text, in runs along each output row.
                for (int y = gy; y < gy + g.height; ++y) {
                    int run = -1;
                    for (int x = gx; x <= gx + g.width; ++x) {
                        const bool v = x < gx + g.width && visible(x, y);
                        if (v && run < 0) run = x;
                        if (!v && run >= 0) {
                            quad(run, y, x, y + 1);
                            run = -1;
                        }
                    }
                }
            }
        }
        if (!full || attempt == 1) break;
        reset();
    }
    if (!indices_.empty() && atlas_)
        SDL_RenderGeometry(renderer_, atlas_, vertices_.data(), static_cast<int>(vertices_.size()), indices_.data(),
                           static_cast<int>(indices_.size()));
}

} // namespace openday::app
