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

} // namespace openday::formats
