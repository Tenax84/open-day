// Minimal dependency-free PNG encoder (RGBA8, uncompressed deflate blocks).
// Meant for tools and debugging output, not for size-efficient files.
#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

namespace openday::formats {

// rgba holds width * height * 4 bytes, row-major, top row first.
std::vector<std::uint8_t> encodePngRgba(int width, int height, const std::vector<std::uint8_t>& rgba);

void writePngRgba(const std::filesystem::path& path, int width, int height, const std::vector<std::uint8_t>& rgba);

} // namespace openday::formats
