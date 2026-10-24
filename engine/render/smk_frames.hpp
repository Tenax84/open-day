// All frames of a stand-alone Smacker file decoded once and kept in memory
// (colony map animations COLANMS\C<n>.SMK, docs/mechanics/strategic-interface.md).
#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

#include "formats/palette.hpp"

namespace openday::render {

class SmkFrames {
public:
    SmkFrames() = default;
    // Throws formats::SmkError or std::runtime_error.
    static SmkFrames load(const std::filesystem::path& smk);

    int width() const { return width_; }
    int height() const { return height_; }
    int count() const { return static_cast<int>(frames_.size()); }
    int frameMicros() const { return frameMicros_; }
    const std::vector<std::uint8_t>& frame(int i) const { return frames_[static_cast<std::size_t>(i)]; }
    const formats::Palette& palette() const { return palette_; }
    bool empty() const { return frames_.empty(); }

private:
    int width_ = 0, height_ = 0, frameMicros_ = 100000;
    std::vector<std::vector<std::uint8_t>> frames_;
    formats::Palette palette_{};
};

} // namespace openday::render
