// Shared 256-color palette types for the 8-bit formats.
#pragma once

#include <array>
#include <cstdint>

namespace openday::formats {

struct Rgb {
    std::uint8_t r = 0, g = 0, b = 0;
    friend bool operator==(const Rgb&, const Rgb&) = default;
};

using Palette = std::array<Rgb, 256>;

} // namespace openday::formats
