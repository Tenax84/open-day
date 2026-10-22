// PALCYCLE reader tests on synthetic data. With --game-dir <dir> it
// additionally loads INTERF\PALCYCLE.PAL from a local game installation
// (optional, never in CI).

#include "formats/palcycle.hpp"

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

Palette greyPalette() {
    Palette p{};
    for (int i = 0; i < 256; ++i) {
        const auto v = static_cast<std::uint8_t>(i);
        p[static_cast<std::size_t>(i)] = Rgb{v, v, v};
    }
    return p;
}

// Entry 200 cycles over 4 steps; entry 201 only has steps 0 and 1, so it
// falls back to its base color for steps 2 and 3.
std::vector<PalCycleEntry> sampleEntries() {
    return {
        {200, Rgb{10, 0, 0}, 0}, {200, Rgb{20, 0, 0}, 1}, {200, Rgb{30, 0, 0}, 2}, {200, Rgb{40, 0, 0}, 3},
        {201, Rgb{0, 0, 99}, 1}, {201, Rgb{0, 0, 88}, 0},
    };
}

void testRoundTrip() {
    const std::vector<PalCycleEntry> entries = sampleEntries();
    const std::vector<std::uint8_t> bytes = writePalCycle(entries);
    CHECK(bytes.size() == PalCycle::kHeaderSize + entries.size() * PalCycle::kEntrySize);
    CHECK(bytes[0] == entries.size() && bytes[1] == 0);
    CHECK(bytes[4] == 200 && bytes[5] == 10 && bytes[8] == 0); // index, red, ..., step

    const PalCycle c = PalCycle::fromBytes(bytes);
    CHECK(c.entries() == entries);
    CHECK(c.stepCount() == 4);
    CHECK(c.cycles(200) && c.cycles(201) && !c.cycles(202));
}

void testApply() {
    const PalCycle c = PalCycle::fromEntries(sampleEntries());
    const Palette base = greyPalette();

    const Palette s0 = c.apply(base, 0);
    CHECK(s0[200] == (Rgb{10, 0, 0}));
    CHECK(s0[201] == (Rgb{0, 0, 88}));
    CHECK(s0[199] == base[199] && s0[202] == base[202]);

    const Palette s1 = c.apply(base, 1);
    CHECK(s1[200] == (Rgb{20, 0, 0}) && s1[201] == (Rgb{0, 0, 99}));

    // No record for entry 201 at step 2: it shows its base color again.
    const Palette s2 = c.apply(base, 2);
    CHECK(s2[200] == (Rgb{30, 0, 0}) && s2[201] == base[201]);

    // Steps wrap modulo the cycle length.
    CHECK(c.apply(base, 5) == s1);

    // 100 ms per step.
    CHECK(c.stepAt(0) == 0 && c.stepAt(99) == 0 && c.stepAt(100) == 1);
    CHECK(c.stepAt(399) == 3 && c.stepAt(400) == 0);
    CHECK(c.applyAt(base, 250) == s2);
}

void testEdgeCases() {
    // Empty and non-positive counts give an inert table.
    const PalCycle empty = PalCycle::fromBytes({0, 0, 0, 0});
    CHECK(empty.entries().empty() && empty.stepCount() == 0 && empty.stepAt(12345) == 0);
    CHECK(empty.apply(greyPalette(), 3) == greyPalette());
    CHECK(PalCycle::fromBytes({0xFF, 0xFF, 0xFF, 0xFF}).entries().empty());

    // Trailing bytes after the last record are ignored.
    std::vector<std::uint8_t> bytes = writePalCycle(sampleEntries());
    bytes.push_back(0x55);
    CHECK(PalCycle::fromBytes(bytes).entries().size() == sampleEntries().size());

    auto rejects = [](const std::vector<std::uint8_t>& b) {
        try {
            PalCycle::fromBytes(b);
        } catch (const PalCycleError&) {
            return true;
        }
        return false;
    };
    CHECK(rejects({1, 0, 0}));
    std::vector<std::uint8_t> truncated = writePalCycle(sampleEntries());
    truncated.pop_back();
    CHECK(rejects(truncated));
}

void testGameFile(const std::filesystem::path& gameDir) {
    try {
        const PalCycle c = PalCycle::fromFile(gameDir / "INTERF" / "PALCYCLE.PAL");
        std::printf("loaded %zu cycle records, %u steps\n", c.entries().size(), c.stepCount());
        CHECK(!c.entries().empty());
        CHECK(c.stepCount() > 1);
        // The water entries documented in PALCYCLE.md.
        CHECK(c.cycles(239) && c.cycles(240) && c.cycles(241));
    } catch (const PalCycleError& e) {
        std::fprintf(stderr, "%s\n", e.what());
        ++failures;
    }
}

} // namespace

int main(int argc, char** argv) {
    testRoundTrip();
    testApply();
    testEdgeCases();
    if (argc == 3 && std::string(argv[1]) == "--game-dir") testGameFile(argv[2]);

    if (failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    std::puts("all PALCYCLE tests passed");
    return EXIT_SUCCESS;
}
