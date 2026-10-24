#include "render/font.hpp"

#include <cmath>
#include <stdexcept>

#include "stb_truetype.h"

namespace openday::embedded {
std::span<const std::uint8_t> fontRegular();
std::span<const std::uint8_t> fontBold();
} // namespace openday::embedded

namespace openday::render {

std::u32string decodeUtf8(std::string_view s) {
    std::u32string out;
    for (std::size_t i = 0; i < s.size();) {
        const auto b = static_cast<unsigned char>(s[i]);
        int extra = b >= 0xF0 ? 3 : b >= 0xE0 ? 2 : b >= 0xC0 ? 1 : 0;
        char32_t c = extra == 3 ? b & 0x07u : extra == 2 ? b & 0x0Fu : extra == 1 ? b & 0x1Fu : b;
        bool ok = b < 0x80 || extra > 0;
        for (int k = 1; ok && k <= extra; ++k) {
            if (i + static_cast<std::size_t>(k) >= s.size()) {
                ok = false;
                break;
            }
            const auto n = static_cast<unsigned char>(s[i + static_cast<std::size_t>(k)]);
            if ((n & 0xC0) != 0x80) ok = false;
            c = (c << 6) | (n & 0x3Fu);
        }
        if (!ok) {
            out.push_back(b); // Latin-1 fallback
            ++i;
            continue;
        }
        out.push_back(c);
        i += static_cast<std::size_t>(extra) + 1;
    }
    return out;
}

std::string encodeUtf8(char32_t c) {
    std::string s;
    if (c < 0x80) {
        s += static_cast<char>(c);
    } else if (c < 0x800) {
        s += static_cast<char>(0xC0 | (c >> 6));
        s += static_cast<char>(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
        s += static_cast<char>(0xE0 | (c >> 12));
        s += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        s += static_cast<char>(0x80 | (c & 0x3F));
    } else {
        s += static_cast<char>(0xF0 | (c >> 18));
        s += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
        s += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        s += static_cast<char>(0x80 | (c & 0x3F));
    }
    return s;
}

struct Font::Impl {
    stbtt_fontinfo info{};
    float scale = 1;
};

Font::Font(FontFace face, int cellHeight) : impl_(std::make_unique<Impl>()), cellHeight_(cellHeight) {
    const std::span<const std::uint8_t> data = face == FontFace::Bold ? embedded::fontBold() : embedded::fontRegular();
    if (!stbtt_InitFont(&impl_->info, data.data(), stbtt_GetFontOffsetForIndex(data.data(), 0)))
        throw std::runtime_error("cannot read the embedded font");
    // Like a GDI font of positive height: ascent + descent = cell height.
    impl_->scale = stbtt_ScaleForPixelHeight(&impl_->info, static_cast<float>(cellHeight));
    int ascent = 0, descent = 0, gap = 0;
    stbtt_GetFontVMetrics(&impl_->info, &ascent, &descent, &gap);
    baseline_ = static_cast<int>(std::lround(ascent * impl_->scale));
}

Font::~Font() = default;

const Font::Glyph& Font::glyph(char32_t c) const {
    const auto it = glyphs_.find(c);
    if (it != glyphs_.end()) return it->second;
    Glyph g;
    const int index = stbtt_FindGlyphIndex(&impl_->info, static_cast<int>(c));
    int advance = 0, bearing = 0;
    stbtt_GetGlyphHMetrics(&impl_->info, index, &advance, &bearing);
    g.advance = static_cast<int>(std::lround(advance * impl_->scale));
    int w = 0, h = 0, xoff = 0, yoff = 0;
    unsigned char* bitmap = stbtt_GetGlyphBitmap(&impl_->info, impl_->scale, impl_->scale, index, &w, &h, &xoff, &yoff);
    if (bitmap) {
        g.width = w;
        g.height = h;
        g.left = xoff;
        g.top = yoff;
        g.mask.resize(static_cast<std::size_t>(w) * static_cast<std::size_t>(h));
        // No anti-aliasing: a pixel is set when the glyph covers half of it.
        for (std::size_t i = 0; i < g.mask.size(); ++i) g.mask[i] = bitmap[i] >= 128 ? 1 : 0;
        stbtt_FreeBitmap(bitmap, nullptr);
    }
    return glyphs_.emplace(c, std::move(g)).first->second;
}

int Font::width(std::string_view utf8) const {
    int w = 0;
    for (const char32_t c : decodeUtf8(utf8)) w += glyph(c).advance;
    return w;
}

void Font::draw(IndexedSurface& target, int x, int y, std::string_view utf8, std::uint8_t color) const {
    int pen = x;
    const int base = y + baseline_;
    for (const char32_t c : decodeUtf8(utf8)) {
        const Glyph& g = glyph(c);
        for (int gy = 0; gy < g.height; ++gy) {
            const int ty = base + g.top + gy;
            if (ty < 0 || ty >= target.height()) continue;
            std::uint8_t* row = target.row(ty);
            for (int gx = 0; gx < g.width; ++gx) {
                const int tx = pen + g.left + gx;
                if (tx >= 0 && tx < target.width() &&
                    g.mask[static_cast<std::size_t>(gy) * static_cast<std::size_t>(g.width) + static_cast<std::size_t>(gx)])
                    row[tx] = color;
            }
        }
        pen += g.advance;
    }
}

void Font::drawIn(IndexedSurface& target, int x, int y, int w, int h, std::string_view utf8, std::uint8_t color,
                  Align align) const {
    const int tw = width(utf8);
    int tx = x;
    if (align == Align::Center) tx = x + (w - tw) / 2;
    else if (align == Align::Right) tx = x + w - tw;
    draw(target, tx, y + (h - cellHeight_) / 2, utf8, color);
}

std::vector<std::string> Font::wrap(std::string_view text, int maxWidth) const {
    // Paragraphs first ("\n" or "##"), then words.
    std::vector<std::string> paragraphs;
    std::string cur;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\n' || (text[i] == '#' && i + 1 < text.size() && text[i + 1] == '#')) {
            paragraphs.push_back(cur);
            cur.clear();
            if (text[i] == '#') ++i;
        } else if (text[i] != '\r') {
            cur += text[i];
        }
    }
    paragraphs.push_back(cur);

    std::vector<std::string> lines;
    for (const std::string& p : paragraphs) {
        std::string line;
        std::size_t pos = 0;
        while (pos <= p.size()) {
            std::size_t sp = p.find(' ', pos);
            if (sp == std::string::npos) sp = p.size();
            const std::string word = p.substr(pos, sp - pos);
            const std::string candidate = line.empty() ? word : line + " " + word;
            if (!line.empty() && width(candidate) > maxWidth) {
                lines.push_back(line);
                line = word;
            } else {
                line = candidate;
            }
            pos = sp + 1;
        }
        lines.push_back(line);
    }
    return lines;
}

int Font::drawWrapped(IndexedSurface& target, int x, int y, int w, int h, std::string_view utf8, std::uint8_t color) const {
    int drawn = 0;
    for (const std::string& line : wrap(utf8, w)) {
        if ((drawn + 1) * cellHeight_ > h) break;
        draw(target, x, y + drawn * cellHeight_, line, color);
        ++drawn;
    }
    return drawn;
}

} // namespace openday::render
