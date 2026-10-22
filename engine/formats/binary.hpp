// Little-endian helpers shared by the format readers and test writers.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace openday::formats::binary {

using Bytes = std::span<const std::uint8_t>;

inline std::uint16_t readU16(Bytes d, std::size_t off) {
    return static_cast<std::uint16_t>(d[off] | (d[off + 1] << 8));
}

inline std::uint32_t readU32(Bytes d, std::size_t off) {
    return static_cast<std::uint32_t>(d[off]) | (static_cast<std::uint32_t>(d[off + 1]) << 8) |
           (static_cast<std::uint32_t>(d[off + 2]) << 16) | (static_cast<std::uint32_t>(d[off + 3]) << 24);
}

inline void putU16(std::vector<std::uint8_t>& out, std::uint16_t v) {
    out.push_back(static_cast<std::uint8_t>(v));
    out.push_back(static_cast<std::uint8_t>(v >> 8));
}

inline void putU32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
}

} // namespace openday::formats::binary
