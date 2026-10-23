// Smacker animations turned into color-keyed sprites, ready to draw on an
// IndexedSurface. See docs/formats/FF.md ("Transparency").
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "formats/dbi.hpp"
#include "formats/ff.hpp"
#include "formats/smk.hpp"

namespace openday::render {

// One frame, cropped to its opaque pixels. (left, top) is the position of
// the cropped sprite inside the full video frame.
struct AnimFrame {
    int left = 0;
    int top = 0;
    formats::DbiSprite sprite;
};

struct Animation {
    int width = 0;  // full video frame size
    int height = 0;
    std::uint32_t frameMicros = 0;
    bool looping = false; // the file has a ring frame
    std::vector<AnimFrame> frames;
    formats::SmkAudio audio; // audio track 0; empty pcm when the clip is silent
};

// Decodes every frame and the first audio track. In each frame, pixels with
// the index of the frame's top-left pixel are transparent.
Animation decodeAnimation(formats::SmkVideo& video);

// Lazily decoded animations of an FF archive, looked up by entry name.
class AnimationCache {
public:
    explicit AnimationCache(std::shared_ptr<const formats::FfArchive> archive);

    // nullptr when the archive has no such entry. Throws on corrupt data.
    const Animation* get(std::string_view name);
    bool contains(std::string_view name) const { return archive_->find(name).has_value(); }

private:
    std::shared_ptr<const formats::FfArchive> archive_;
    std::unordered_map<std::size_t, std::unique_ptr<Animation>> decoded_;
};

} // namespace openday::render
