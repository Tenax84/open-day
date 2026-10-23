// Animation decoding (color key, cropping) and the FF-backed cache, on
// synthetic data.

#include "render/animation.hpp"

#include <cstdio>
#include <memory>

using namespace openday;
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

constexpr int kW = 16, kH = 8;

SmkFrame frame(std::uint8_t background) {
    SmkFrame f;
    for (int i = 0; i < 256; ++i) f.palette[static_cast<std::size_t>(i)] = Rgb{0, 0, 0};
    f.pixels.assign(kW * kH, background);
    return f;
}

void set(SmkFrame& f, int x, int y, std::uint8_t v) { f.pixels[static_cast<std::size_t>(y * kW + x)] = v; }

std::vector<std::uint8_t> testVideo() {
    std::vector<SmkFrame> frames;
    // Frame 0: key 7, opaque pixels at (5,2), (7,3); a hole of key color at (6,2).
    frames.push_back(frame(7));
    set(frames[0], 5, 2, 30);
    set(frames[0], 7, 3, 31);
    // Frame 1: the key changes to 9; index 7 is now opaque.
    frames.push_back(frame(9));
    set(frames[1], 10, 6, 7);
    // Frame 2: nothing opaque.
    frames.push_back(frame(9));
    return writeSmk(kW, kH, frames, 66660);
}

void testDecode() {
    SmkVideo video(testVideo());
    const render::Animation a = render::decodeAnimation(video);
    CHECK(a.width == kW && a.height == kH);
    CHECK(a.frameMicros == 66660);
    CHECK(a.frames.size() == 3);
    if (a.frames.size() != 3) return;

    const render::AnimFrame& f0 = a.frames[0];
    CHECK(f0.left == 5 && f0.top == 2);
    CHECK(f0.sprite.width == 3 && f0.sprite.height == 2);
    CHECK(f0.sprite.opaque == std::vector<std::uint8_t>({1, 0, 0, 0, 0, 1}));
    CHECK(f0.sprite.pixels[0] == 30 && f0.sprite.pixels[5] == 31);

    const render::AnimFrame& f1 = a.frames[1];
    CHECK(f1.left == 10 && f1.top == 6 && f1.sprite.width == 1 && f1.sprite.height == 1);
    CHECK(f1.sprite.pixels[0] == 7 && f1.sprite.opaque[0] == 1);

    CHECK(a.frames[2].sprite.width == 0);
}

void testCache() {
    auto archive = std::make_shared<const FfArchive>(FfArchive::fromBytes(writeFf({{"ABC\\P1\\ABC1M0.SMK", testVideo()}})));
    render::AnimationCache cache(archive);
    CHECK(cache.contains("abc/p1/abc1m0.smk"));
    const render::Animation* a = cache.get("ABC\\P1\\ABC1M0.SMK");
    CHECK(a && a->frames.size() == 3);
    CHECK(cache.get("abc\\p1\\ABC1M0.smk") == a); // decoded once
    CHECK(cache.get("ABC\\P1\\ABC1M1.SMK") == nullptr);
}

} // namespace

int main() {
    testDecode();
    testCache();
    if (failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::puts("animation_test: ok");
    return 0;
}
