// FF archive and Smacker decoder tests on synthetic data. With
// --game-dir <dir> it additionally decodes every animation of a local game
// installation (optional, never in CI).

#include "formats/ff.hpp"
#include "formats/smk.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

using namespace openday::formats;

namespace {

int failures = 0;

#define CHECK(cond)                                                                       \
    do {                                                                                  \
        if (!(cond)) {                                                                    \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                                   \
        }                                                                                 \
    } while (0)

// Smacker stores 6-bit color components; pick values that survive the
// 6-bit -> 8-bit widening unchanged.
std::uint8_t six(int v) {
    const auto c = static_cast<unsigned>(v & 0x3F);
    return static_cast<std::uint8_t>((c << 2) | (c >> 4));
}

Palette testPalette(int shift) {
    Palette p{};
    for (int i = 0; i < 256; ++i)
        p[static_cast<std::size_t>(i)] = Rgb{six(i + shift), six(63 - i), six(i / 4)};
    return p;
}

// 16 x 8 frames covering every block kind: solid, two-color, full, and
// (in later frames) unchanged blocks.
std::vector<SmkFrame> testFrames() {
    const int w = 16, h = 8;
    std::vector<SmkFrame> frames(3);
    for (std::size_t f = 0; f < frames.size(); ++f) {
        frames[f].palette = testPalette(f == 2 ? 7 : 0);
        frames[f].pixels.assign(w * h, 253);
    }
    auto set = [&](std::size_t f, int x, int y, std::uint8_t v) {
        frames[f].pixels[static_cast<std::size_t>(y * w + x)] = v;
    };
    for (int y = 0; y < 4; ++y)
        for (int x = 4; x < 8; ++x) set(0, x, y, (x + y) % 2 ? 10 : 20); // two colors
    for (int y = 0; y < 4; ++y)
        for (int x = 8; x < 12; ++x) set(0, x, y, static_cast<std::uint8_t>(x * 16 + y)); // all different
    for (int y = 4; y < 8; ++y)
        for (int x = 0; x < 16; ++x) set(0, x, y, static_cast<std::uint8_t>(x < 8 ? 0 : 255));
    frames[1].pixels = frames[0].pixels;
    set(1, 13, 6, 77); // one block changes, the rest are skipped
    frames[2].pixels = frames[1].pixels;
    set(2, 0, 0, 1);
    return frames;
}

std::vector<std::uint8_t> testAudio() {
    std::vector<std::uint8_t> s;
    for (int i = 0; i < 500; ++i) s.push_back(static_cast<std::uint8_t>(128 + ((i * 37) % 61) - 30));
    return s;
}

void testSmkRoundTrip() {
    const std::vector<SmkFrame> frames = testFrames();
    const std::vector<std::uint8_t> audio = testAudio();
    SmkVideo v(writeSmk(16, 8, frames, 66660, audio, 22050));
    CHECK(v.width() == 16 && v.height() == 8);
    CHECK(v.frameCount() == 3);
    CHECK(!v.hasRingFrame());
    CHECK(v.frameMicros() == 66660);
    CHECK(v.audioTrack(0).present && v.audioTrack(0).compressed && v.audioTrack(0).sampleRate == 22050);
    CHECK(!v.audioTrack(1).present);

    for (const SmkFrame& expected : frames) {
        CHECK(v.nextFrame());
        CHECK(v.frame() == expected.pixels);
        CHECK(v.palette() == expected.palette);
        CHECK(v.unusedVideoBits() < 32);
    }
    CHECK(!v.nextFrame());

    v.rewind();
    CHECK(v.nextFrame() && v.currentFrame() == 0 && v.frame() == frames[0].pixels);

    const SmkAudio a = v.decodeAudio(0);
    CHECK(a.channels == 1 && a.bitsPerSample == 8 && a.sampleRate == 22050);
    CHECK(a.pcm == audio);
    CHECK(v.decodeAudio(1).pcm.empty());
}

// Bit writer for the hand-made stream below (least significant bit first).
struct Bits {
    std::vector<std::uint8_t> bytes;
    std::size_t n = 0;
    void put(unsigned v, int count = 1) {
        for (int i = 0; i < count; ++i, ++n) {
            if (n % 8 == 0) bytes.push_back(0);
            if ((v >> i) & 1) bytes.back() = static_cast<std::uint8_t>(bytes.back() | (1u << (n % 8)));
        }
    }
};

void putU32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
}

// The writer never uses escape values, so this hand-made file checks the
// "recently used value" cache: a tree leaf holding an escape value decodes
// to the most recently decoded value of that table.
void testRecentValueCache() {
    Bits t;
    t.put(0); // mmap table absent
    t.put(0); // mclr table absent
    t.put(0); // full table absent
    t.put(1); // type table present
    t.put(1), t.put(1), t.put(0), t.put(0x03, 8), t.put(0), t.put(0x34, 8), t.put(0); // low bytes {03, 34}
    t.put(1), t.put(1), t.put(0), t.put(0x05, 8), t.put(0), t.put(0x12, 8), t.put(0); // high bytes {05, 12}
    t.put(0x1234, 16), t.put(0xFFFF, 16), t.put(0xFFFE, 16);                          // escapes
    t.put(1);           // node
    t.put(0), t.put(0), t.put(0); // leaf 0x0503: solid block, color 5
    t.put(0), t.put(1), t.put(1); // leaf 0x1234: first escape
    t.put(0);

    Bits video; // 12 x 4 = three blocks: leaf 0, then the escape leaf twice
    video.put(0), video.put(1), video.put(1);
    std::vector<std::uint8_t> frame = video.bytes;
    frame.resize(4, 0);

    std::vector<std::uint8_t> f = {'S', 'M', 'K', '2'};
    putU32(f, 12);  // width
    putU32(f, 4);   // height
    putU32(f, 1);   // frames
    putU32(f, 100); // frame rate: 100 ms
    putU32(f, 0);   // flags
    for (int i = 0; i < 7; ++i) putU32(f, 0);
    putU32(f, static_cast<std::uint32_t>(t.bytes.size()));
    for (int i = 0; i < 4; ++i) putU32(f, 64);
    for (int i = 0; i < 7; ++i) putU32(f, 0);
    putU32(f, 0);
    putU32(f, static_cast<std::uint32_t>(frame.size()) | 1);
    f.push_back(0); // frame type: no palette, no audio
    f.insert(f.end(), t.bytes.begin(), t.bytes.end());
    f.insert(f.end(), frame.begin(), frame.end());

    SmkVideo v(f);
    CHECK(v.frameMicros() == 100000);
    CHECK(v.nextFrame());
    CHECK(v.frame() == std::vector<std::uint8_t>(48, 5));
}

void testFfArchive() {
    const std::vector<std::uint8_t> a = writeSmk(16, 8, {testFrames()[0]}, 100000);
    const std::vector<std::uint8_t> b = writeSmk(16, 8, testFrames(), 66660, testAudio());
    std::vector<std::uint8_t> bytes = writeFf({{"UNIT\\P1\\UNIT1M0.SMK", a}, {"BUILDING\\HQ.SMK", b}});
    CHECK(bytes.size() == FfArchive::kHeaderSize + 2 * FfArchive::kEntrySize + a.size() + b.size());

    // Swap the two directory entries: directory order is not data order,
    // and sizes must still come out right.
    std::vector<std::uint8_t> swapped = bytes;
    std::copy_n(bytes.begin() + 8, FfArchive::kEntrySize, swapped.begin() + 8 + FfArchive::kEntrySize);
    std::copy_n(bytes.begin() + 8 + FfArchive::kEntrySize, FfArchive::kEntrySize, swapped.begin() + 8);

    for (const auto& data : {bytes, swapped}) {
        const FfArchive ff = FfArchive::fromBytes(data);
        CHECK(ff.count() == 2);
        const auto ia = ff.find("unit/p1/unit1m0.smk");
        const auto ib = ff.find("BUILDING\\HQ.SMK");
        CHECK(ia && ib && *ia != *ib);
        CHECK(!ff.find("BUILDING\\NOPE.SMK"));
        if (!ia || !ib) continue;
        CHECK(ff.entry(*ia).name == "UNIT\\P1\\UNIT1M0.SMK");
        CHECK(ff.entry(*ia).size == a.size() && ff.entry(*ib).size == b.size());
        CHECK(ff.read(*ia) == a);
        CHECK(ff.read(*ib) == b);
        SmkVideo v(ff.read(*ib));
        CHECK(v.frameCount() == 3);
    }

    auto rejects = [](std::vector<std::uint8_t> d) {
        try {
            FfArchive::fromBytes(std::move(d));
        } catch (const FfError&) {
            return true;
        }
        return false;
    };
    std::vector<std::uint8_t> bad = bytes;
    bad[0] = 'X';
    CHECK(rejects(bad));
    CHECK(rejects(std::vector<std::uint8_t>(bytes.begin(), bytes.begin() + 100))); // directory cut off
    bad = bytes;
    std::fill_n(bad.begin() + 8, FfArchive::kNameSize, std::uint8_t{'A'}); // name without terminator
    CHECK(rejects(bad));
}

void testSmkRejectsBadInput() {
    std::vector<std::uint8_t> good = writeSmk(16, 8, testFrames(), 66660);
    auto rejects = [](std::vector<std::uint8_t> d) {
        try {
            SmkVideo v(std::move(d));
            while (v.nextFrame()) {
            }
        } catch (const SmkError&) {
            return true;
        }
        return false;
    };
    std::vector<std::uint8_t> bad = good;
    bad[3] = '4'; // SMK4 is not supported
    CHECK(rejects(bad));
    bad = good;
    bad[0x14] = 4; // doubled video
    CHECK(rejects(bad));
    CHECK(rejects(std::vector<std::uint8_t>(good.begin(), good.end() - 8))); // last frame cut off
}

void testGameFiles(const std::filesystem::path& gameDir) {
    std::filesystem::path path = gameDir / "ANMSUNIT" / "ANMSUNIT.FF";
    if (!std::filesystem::exists(path)) path = gameDir / "anmsunit" / "anmsunit.ff";
    std::size_t frames = 0, audio = 0, entries = 0;
    try {
        const FfArchive ff = FfArchive::open(path);
        for (std::size_t i = 0; i < ff.count(); ++i) {
            const FfEntry& e = ff.entry(i);
            CHECK(ff.find(e.name) == i);
            try {
                SmkVideo v(ff.read(i));
                while (v.nextFrame()) {
                    ++frames;
                    if (v.unusedVideoBits() >= 32) {
                        std::fprintf(stderr, "%s frame %d: %zu unread bits\n", e.name.c_str(), v.currentFrame(),
                                     v.unusedVideoBits());
                        ++failures;
                    }
                }
                audio += v.decodeAudio(0).pcm.size();
                ++entries;
            } catch (const SmkError& ex) {
                std::fprintf(stderr, "%s: %s\n", e.name.c_str(), ex.what());
                ++failures;
            }
        }
        CHECK(entries == ff.count());
    } catch (const FfError& ex) {
        std::fprintf(stderr, "%s: %s\n", path.string().c_str(), ex.what());
        ++failures;
    }
    std::printf("decoded %zu animations, %zu frames, %zu audio samples from the game installation\n", entries, frames,
                audio);
    CHECK(entries > 0);
}

} // namespace

int main(int argc, char** argv) {
    testSmkRoundTrip();
    testRecentValueCache();
    testSmkRejectsBadInput();
    testFfArchive();
    if (argc == 3 && std::string(argv[1]) == "--game-dir") testGameFiles(argv[2]);

    if (failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    std::puts("all FF tests passed");
    return EXIT_SUCCESS;
}
