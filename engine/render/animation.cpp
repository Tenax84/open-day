#include "render/animation.hpp"

#include <algorithm>

namespace openday::render {
namespace {

AnimFrame cropFrame(const std::vector<std::uint8_t>& pixels, int width, int height) {
    AnimFrame out;
    if (pixels.empty()) return out;
    const std::uint8_t key = pixels[0];
    int x0 = width, y0 = height, x1 = -1, y1 = -1;
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            if (pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)] != key) {
                x0 = std::min(x0, x);
                x1 = std::max(x1, x);
                y0 = std::min(y0, y);
                y1 = std::max(y1, y);
            }
    if (x1 < 0) return out; // fully transparent frame
    out.left = x0;
    out.top = y0;
    formats::DbiSprite& s = out.sprite;
    s.width = x1 - x0 + 1;
    s.height = y1 - y0 + 1;
    s.pixels.resize(static_cast<std::size_t>(s.width) * static_cast<std::size_t>(s.height));
    s.opaque.resize(s.pixels.size());
    for (int y = 0; y < s.height; ++y)
        for (int x = 0; x < s.width; ++x) {
            const std::uint8_t v =
                pixels[static_cast<std::size_t>(y + y0) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x + x0)];
            const std::size_t i = static_cast<std::size_t>(y) * static_cast<std::size_t>(s.width) + static_cast<std::size_t>(x);
            s.pixels[i] = v;
            s.opaque[i] = v != key;
        }
    return out;
}

} // namespace

Animation decodeAnimation(formats::SmkVideo& video) {
    Animation a;
    a.width = video.width();
    a.height = video.height();
    a.frameMicros = video.frameMicros();
    a.looping = video.hasRingFrame();
    a.frames.reserve(static_cast<std::size_t>(video.frameCount()));
    video.rewind();
    while (video.nextFrame()) a.frames.push_back(cropFrame(video.frame(), a.width, a.height));
    if (video.audioTrack(0).present) a.audio = video.decodeAudio(0);
    return a;
}

AnimationCache::AnimationCache(std::shared_ptr<const formats::FfArchive> archive) : archive_(std::move(archive)) {}

const Animation* AnimationCache::get(std::string_view name) {
    const auto index = archive_->find(name);
    if (!index) return nullptr;
    auto it = decoded_.find(*index);
    if (it == decoded_.end()) {
        formats::SmkVideo video(archive_->read(*index));
        it = decoded_.emplace(*index, std::make_unique<Animation>(decodeAnimation(video))).first;
    }
    return it->second.get();
}

} // namespace openday::render
