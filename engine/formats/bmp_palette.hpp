// Reads the color table of an 8-bit Windows BMP, such as the game's main
// palette INTERF\PAL.BMP.
#pragma once

#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <vector>

#include "formats/palette.hpp"

namespace openday::formats {

struct BmpError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

Palette readBmpPalette(const std::vector<std::uint8_t>& bytes);
Palette loadBmpPalette(const std::filesystem::path& path);

// An uncompressed 8-bit BMP: palette indices top row first, and its own
// color table (the province pictures stored in the databases).
struct BmpImage {
    int width = 0, height = 0;
    std::vector<std::uint8_t> pixels; // width * height, top-down
    Palette palette{};
};

BmpImage readBmpImage(const std::vector<std::uint8_t>& bytes);

} // namespace openday::formats
