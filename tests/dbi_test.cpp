// DBI reader tests on synthetic data. With --game-dir <dir> it additionally
// decodes every sprite of a local game installation (optional, never in CI).

#include "formats/dbi.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

using namespace openday::formats;

namespace {

int failures = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                      \
        }                                                                    \
    } while (0)

Palette testPalette() {
    Palette p{};
    for (int i = 0; i < 256; ++i)
        p[static_cast<std::size_t>(i)] = Rgb{static_cast<std::uint8_t>(i), static_cast<std::uint8_t>(255 - i),
                                             static_cast<std::uint8_t>(i / 2)};
    return p;
}

// A diamond-shaped sprite, like an isometric tile, with index 0 used as an
// ordinary opaque color to make sure it is not treated as a color key.
DbiSprite diamond(const std::string& name, int w, int h) {
    DbiSprite s{name, w, h, {}, {}};
    s.pixels.assign(static_cast<std::size_t>(w * h), 0);
    s.opaque.assign(s.pixels.size(), 0);
    for (int y = 0; y < h; ++y) {
        const int half = (y < h / 2 ? y : h - 1 - y) * w / h;
        for (int x = w / 2 - half; x < w / 2 + half; ++x) {
            const std::size_t i = static_cast<std::size_t>(y * w + x);
            s.opaque[i] = 1;
            s.pixels[i] = static_cast<std::uint8_t>((x * 7 + y) % 256);
        }
    }
    return s;
}

void testRoundTrip() {
    std::vector<DbiSprite> sprites = {diamond("TILE000", 54, 56), diamond("FULLNAME", 54, 56)};
    std::fill(sprites[1].opaque.begin(), sprites[1].opaque.end(), std::uint8_t{1});
    const Palette pal = testPalette();

    const DbiArchive a = DbiArchive::fromBytes(writeDbi(sprites, pal, true));
    CHECK(a.count() == 2);
    CHECK(a.nominalWidth() == 54 && a.nominalHeight() == 56);
    CHECK(a.transparentFlag());
    CHECK(a.palette()[10].r == 10 && a.palette()[10].g == 245 && a.palette()[10].b == 5);
    CHECK(a.slotName(1) == "FULLNAME"); // 8 characters, no terminator
    CHECK(a.findSlot("TILE000") == 0u);
    CHECK(a.findSlot("FULLNAME") == 1u);
    CHECK(!a.findSlot("MISSING"));

    for (std::size_t i = 0; i < sprites.size(); ++i) {
        const DbiSprite s = a.decode(i);
        CHECK(s.name == sprites[i].name);
        CHECK(s.width == sprites[i].width && s.height == sprites[i].height);
        CHECK(s.opaque == sprites[i].opaque);
        bool same = true;
        for (std::size_t p = 0; p < s.pixels.size(); ++p)
            if (s.opaque[p] && s.pixels[p] != sprites[i].pixels[p]) same = false;
        CHECK(same);
    }
}

// Reproduces the truncated trailing run described in docs/formats/DBI.md:
// the record size ends inside the last run header, and the slot bytes after
// it are garbage that must be ignored.
void testTruncatedTrailingRun() {
    DbiSprite s{"TRUNC", 4, 2, {}, {}};
    s.pixels = {1, 2, 3, 4, 5, 6, 0, 0};
    s.opaque = {1, 1, 1, 1, 1, 1, 0, 0};
    std::vector<std::uint8_t> bytes = writeDbi({s}, testPalette(), true);

    // Last row: [u32 len=14][2,lit][5,6][2,transparent]. Shrink the record
    // size by 3 and put garbage over the cut-off part of the final header.
    const std::size_t rec = DbiArchive::kSlotsOffset;
    const std::uint16_t size = static_cast<std::uint16_t>(bytes[rec + 12] | (bytes[rec + 13] << 8));
    const std::uint16_t cut = static_cast<std::uint16_t>(size - 3);
    bytes[rec + 12] = static_cast<std::uint8_t>(cut);
    bytes[rec + 13] = static_cast<std::uint8_t>(cut >> 8);
    const std::size_t dataEnd = rec + 20 + size;
    bytes[dataEnd - 3] = 0xAB;
    bytes[dataEnd - 2] = 0x02; // looks like a literal run
    bytes[dataEnd - 1] = 0x00;

    const DbiSprite d = DbiArchive::fromBytes(bytes).decode(0);
    CHECK(d.opaque == s.opaque);
    CHECK(d.pixels[4] == 5 && d.pixels[5] == 6);
}

void testRejectsBadInput() {
    std::vector<std::uint8_t> bytes = writeDbi({diamond("A", 8, 8)}, testPalette(), true);
    auto rejects = [](std::vector<std::uint8_t> b) {
        try {
            DbiArchive::fromBytes(std::move(b));
        } catch (const DbiError&) {
            return true;
        }
        return false;
    };
    std::vector<std::uint8_t> badMagic = bytes;
    badMagic[0] = 'x';
    CHECK(rejects(badMagic));
    std::vector<std::uint8_t> truncated(bytes.begin(), bytes.end() - 1);
    CHECK(rejects(truncated));

    std::vector<std::uint8_t> badRun = bytes;
    badRun[DbiArchive::kSlotsOffset + 20 + 4 + 2] = 7; // first run of first row: unknown kind
    bool threw = false;
    try {
        DbiArchive::fromBytes(badRun).decode(0);
    } catch (const DbiError&) {
        threw = true;
    }
    CHECK(threw);
}

void testGameFiles(const std::filesystem::path& gameDir) {
    std::size_t total = 0;
    for (const auto& entry : std::filesystem::directory_iterator(gameDir / "IMGS")) {
        if (entry.path().extension() != ".DBI" && entry.path().extension() != ".dbi") continue;
        try {
            const DbiArchive a = DbiArchive::fromFile(entry.path());
            for (std::size_t i = 0; i < a.count(); ++i) {
                const DbiSprite s = a.decode(i);
                CHECK(a.findSlot(s.name) == i);
            }
            total += a.count();
        } catch (const DbiError& e) {
            std::fprintf(stderr, "%s: %s\n", entry.path().string().c_str(), e.what());
            ++failures;
        }
    }
    std::printf("decoded %zu sprites from the game installation\n", total);
    CHECK(total > 0);
}

} // namespace

int main(int argc, char** argv) {
    testRoundTrip();
    testTruncatedTrailingRun();
    testRejectsBadInput();
    if (argc == 3 && std::string(argv[1]) == "--game-dir") testGameFiles(argv[2]);

    if (failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    std::puts("all DBI tests passed");
    return EXIT_SUCCESS;
}
