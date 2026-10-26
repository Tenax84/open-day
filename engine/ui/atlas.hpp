// The interface atlas INTERF\640X480.SMK / 1024X768.SMK: one element per
// frame, anchored at the top-left, cropped to a size the caller knows
// (docs/formats/INTERFACE-SMK.md).
#pragma once

#include <filesystem>
#include <map>
#include <unordered_map>
#include <vector>

#include "formats/dbi.hpp"

namespace openday::ui {

struct AtlasElement {
    int frame = 0;
    int width = 0, height = 0;
};

class InterfaceAtlas {
public:
    InterfaceAtlas() = default;

    // Decodes the file once and keeps the requested elements. Frames are
    // decoded in order, so only frames up to the highest request are read.
    static InterfaceAtlas load(const std::filesystem::path& smk, const std::vector<AtlasElement>& wanted);
    // Same, from a whole file in memory (tests).
    static InterfaceAtlas fromBytes(std::vector<std::uint8_t> smk, const std::vector<AtlasElement>& wanted);

    // The element of a frame (all pixels opaque: the atlas is blitted
    // without a color key), or nullptr if it was not requested.
    const formats::DbiSprite* get(int frame) const;
    bool empty() const { return elements_.empty(); }
    // The frame of an element of this atlas, or -1.
    int frameOf(const formats::DbiSprite& sprite) const;
    const std::map<int, formats::DbiSprite>& elements() const { return elements_; }

private:
    std::map<int, formats::DbiSprite> elements_;
    std::unordered_map<const formats::DbiSprite*, int> frames_;
};

} // namespace openday::ui
