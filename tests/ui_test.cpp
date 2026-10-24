// User-interface foundations on synthetic data: font metrics and wrapping,
// message formatting, PE string tables and bitmaps, the interface atlas,
// buttons.

#include "formats/binary.hpp"
#include "formats/pe_resources.hpp"
#include "formats/smk.hpp"
#include "render/font.hpp"
#include "ui/atlas.hpp"
#include "ui/widgets.hpp"

#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

using namespace openday;
using formats::binary::putU16;
using formats::binary::putU32;

namespace {

int failures = 0;

#define CHECK(cond)                                                                       \
    do {                                                                                  \
        if (!(cond)) {                                                                    \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                                   \
        }                                                                                 \
    } while (0)

void testFont() {
    const render::Font f(render::FontFace::Regular, 14);
    CHECK(f.lineHeight() == 14);
    CHECK(f.width("") == 0);
    CHECK(f.width("W") > f.width("i"));
    CHECK(f.width("ab") == f.width("a") + f.width("b"));
    const int w = f.width("aaa bbb");
    const std::vector<std::string> lines = f.wrap("aaa bbb ccc", w);
    CHECK(lines.size() == 2 && lines[0] == "aaa bbb" && lines[1] == "ccc");
    const std::vector<std::string> parts = f.wrap("one##two", 1000);
    CHECK(parts.size() == 2 && parts[1] == "two");
    // Text is drawn in one color, without intermediate shades.
    render::IndexedSurface s(64, 20, 0);
    f.draw(s, 2, 2, "Hi", 7);
    int set = 0, other = 0;
    for (int y = 0; y < s.height(); ++y)
        for (int x = 0; x < s.width(); ++x) {
            if (s.at(x, y) == 7) ++set;
            else if (s.at(x, y) != 0) ++other;
        }
    CHECK(set > 10 && other == 0);
    // UTF-8 and Latin-1 fallback decoding.
    CHECK(render::decodeUtf8("\xc3\xa9") == std::u32string(1, U'é'));
    CHECK(render::decodeUtf8("\xe9") == std::u32string(1, U'é'));
    CHECK(render::Font(render::FontFace::Bold, 17).width("Bold") > 0);
}

void testFormat() {
    CHECK(formats::formatMessage("%1!d! units selected", {"3"}) == "3 units selected");
    CHECK(formats::formatMessage("AP %1 / %2", {"40", "80"}) == "AP 40 / 80");
    CHECK(formats::formatMessage("%2 %1 100%%", {"a", "b"}) == "b a 100%");
}

// A minimal PE file with one resource of a type and id: by default one
// string-table block (ids 0-15) holding id 1.
std::vector<std::uint8_t> makePe(std::uint32_t type = 6, std::uint32_t id = 1, std::vector<std::uint8_t> data = {}) {
    std::vector<std::uint8_t> rsrc;
    auto dir = [&](std::uint32_t id, std::uint32_t offset) {
        for (int i = 0; i < 12; ++i) rsrc.push_back(0);
        putU16(rsrc, 0); // named entries
        putU16(rsrc, 1); // id entries
        putU32(rsrc, id);
        putU32(rsrc, offset);
    };
    dir(type, 0x80000000u | 24); // type -> id directory
    dir(id, 0x80000000u | 48);   // id -> language directory
    dir(0x409, 72);              // English -> data entry
    const std::uint32_t textRva = 0x1000 + 88;
    std::vector<std::uint8_t> text = std::move(data);
    if (text.empty()) {
        putU16(text, 0); // id 0: empty
        const std::u16string hello = u"Hellö";
        putU16(text, static_cast<std::uint16_t>(hello.size()));
        for (const char16_t c : hello) putU16(text, static_cast<std::uint16_t>(c));
        for (int i = 2; i < 16; ++i) putU16(text, 0);
    }
    putU32(rsrc, textRva);
    putU32(rsrc, static_cast<std::uint32_t>(text.size()));
    putU32(rsrc, 0);
    putU32(rsrc, 0);
    rsrc.insert(rsrc.end(), text.begin(), text.end());

    std::vector<std::uint8_t> pe(0x40, 0);
    pe[0] = 'M';
    pe[1] = 'Z';
    pe[0x3C] = 0x40;
    putU32(pe, 0x00004550);
    putU16(pe, 0x14C); // i386
    putU16(pe, 1);     // sections
    for (int i = 0; i < 12; ++i) pe.push_back(0);
    putU16(pe, 224); // optional header size
    putU16(pe, 0);
    const std::size_t opt = pe.size();
    pe.resize(opt + 224, 0);
    pe[opt] = 0x0B;
    pe[opt + 1] = 0x01; // PE32
    const std::size_t dirs = opt + 96 + 2 * 8;
    pe[dirs] = 0x00;
    pe[dirs + 1] = 0x10; // resource RVA 0x1000
    // Section header .rsrc
    const char name[8] = {'.', 'r', 's', 'r', 'c', 0, 0, 0};
    pe.insert(pe.end(), name, name + 8);
    putU32(pe, static_cast<std::uint32_t>(rsrc.size())); // virtual size
    putU32(pe, 0x1000);                                   // virtual address
    putU32(pe, static_cast<std::uint32_t>(rsrc.size())); // raw size
    putU32(pe, 0x200);                                    // raw pointer
    for (int i = 0; i < 16; ++i) pe.push_back(0);
    pe.resize(0x200, 0);
    pe.insert(pe.end(), rsrc.begin(), rsrc.end());
    return pe;
}

void testStrings() {
    const std::map<int, std::string> t = formats::loadStringTable(makePe());
    CHECK(t.size() == 1 && t.count(1) && t.at(1) == "Hell\xc3\xb6");
    bool threw = false;
    try {
        formats::loadStringTable(std::vector<std::uint8_t>(10, 0));
    } catch (const formats::PeError&) {
        threw = true;
    }
    CHECK(threw);
}

// A bottom-up DIB of 2 x 2 pixels: 8-bit with a 3-color table, or 4-bit.
std::vector<std::uint8_t> makeDib(int bits) {
    std::vector<std::uint8_t> d;
    putU32(d, 40);
    putU32(d, 2);
    putU32(d, 2);
    putU16(d, 1);
    putU16(d, static_cast<std::uint16_t>(bits));
    putU32(d, 0); // BI_RGB
    putU32(d, 0);
    putU32(d, 0);
    putU32(d, 0);
    putU32(d, 3); // colors used
    putU32(d, 0);
    const std::uint8_t table[3][4] = {{0, 0, 255, 0}, {0, 255, 0, 0}, {255, 0, 0, 0}}; // red, green, blue (BGR order)
    for (const auto& c : table) d.insert(d.end(), c, c + 4);
    if (bits == 8) {
        const std::uint8_t rows[2][4] = {{2, 2, 0, 0}, {0, 1, 0, 0}}; // bottom row first
        for (const auto& r : rows) d.insert(d.end(), r, r + 4);
    } else {
        const std::uint8_t rows[2][4] = {{0x22, 0, 0, 0}, {0x01, 0, 0, 0}};
        for (const auto& r : rows) d.insert(d.end(), r, r + 4);
    }
    return d;
}

void testBitmaps() {
    for (const int bits : {8, 4}) {
        const formats::DibImage img = formats::decodeDib(makeDib(bits));
        CHECK(img.width == 2 && img.height == 2);
        CHECK(img.pixels[0].r == 255 && img.pixels[1].g == 255 && img.pixels[2].b == 255 && img.pixels[3].b == 255);
    }
    const auto res = formats::loadResources(makePe(formats::kResourceBitmap, 185, makeDib(8)), formats::kResourceBitmap);
    CHECK(res.size() == 1 && res.count(185) && res.at(185).size() == makeDib(8).size());
    CHECK(formats::loadResources(makePe(), formats::kResourceBitmap).empty());
    bool threw = false;
    try {
        formats::decodeDib(std::vector<std::uint8_t>(20, 0));
    } catch (const formats::PeError&) {
        threw = true;
    }
    CHECK(threw);
}

void testAtlas() {
    std::vector<formats::SmkFrame> frames(3);
    for (int f = 0; f < 3; ++f) frames[static_cast<std::size_t>(f)].pixels.assign(8 * 8, static_cast<std::uint8_t>(10 + f));
    frames[1].pixels[0] = 99;
    const std::vector<std::uint8_t> smk = formats::writeSmk(8, 8, frames, 100000);
    const ui::InterfaceAtlas atlas = ui::InterfaceAtlas::fromBytes(smk, {{1, 4, 2}});
    const formats::DbiSprite* e = atlas.get(1);
    CHECK(e && e->width == 4 && e->height == 2 && e->pixels[0] == 99 && e->pixels[1] == 11 && e->opaque[0] == 1);
    CHECK(atlas.get(0) == nullptr && atlas.get(2) == nullptr);
}

void testButton() {
    int clicks = 0;
    ui::Button b;
    b.rect = {10, 10, 20, 10};
    b.hotkey = 'g';
    b.onClick = [&] { ++clicks; };
    auto ev = [](ui::InputEvent::Type t, int x, int y) {
        ui::InputEvent e;
        e.type = t;
        e.x = x;
        e.y = y;
        return e;
    };
    // Runs on release inside the button only.
    b.handle(ev(ui::InputEvent::Type::MouseMove, 15, 15));
    b.handle(ev(ui::InputEvent::Type::MouseDown, 15, 15));
    b.handle(ev(ui::InputEvent::Type::MouseUp, 15, 15));
    CHECK(clicks == 1);
    b.handle(ev(ui::InputEvent::Type::MouseDown, 15, 15));
    b.handle(ev(ui::InputEvent::Type::MouseMove, 50, 50));
    b.handle(ev(ui::InputEvent::Type::MouseUp, 50, 50));
    CHECK(clicks == 1);
    ui::InputEvent key;
    key.type = ui::InputEvent::Type::KeyDown;
    key.key = 'g';
    CHECK(b.handle(key) && clicks == 2);
    b.enabled = false;
    CHECK(!b.handle(key) && clicks == 2);

    ui::RadioGroup g;
    for (int i = 0; i < 3; ++i) {
        ui::Button item;
        item.rect = {0, 20 * i, 10, 10};
        g.items.push_back(item);
    }
    int changed = -1;
    g.onChange = [&](int i) { changed = i; };
    g.handle(ev(ui::InputEvent::Type::MouseDown, 5, 45));
    g.handle(ev(ui::InputEvent::Type::MouseUp, 5, 45));
    CHECK(changed == 2 && g.checked() == 2 && !g.items[0].checked);
}

} // namespace

int main() {
    testFont();
    testFormat();
    testStrings();
    testBitmaps();
    testAtlas();
    testButton();
    if (failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    std::puts("all ui tests passed");
    return EXIT_SUCCESS;
}
