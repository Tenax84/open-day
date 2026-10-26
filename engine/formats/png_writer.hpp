// Minimal dependency-free PNG encoder: RGBA8 in uncompressed deflate blocks,
// and compressed 8-bit palette images. Meant for tools and debugging output.
#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

namespace openday::formats {

// rgba holds width * height * 4 bytes, row-major, top row first.
std::vector<std::uint8_t> encodePngRgba(int width, int height, const std::vector<std::uint8_t>& rgba);

// An 8-bit palette image, compressed (formats/deflate.hpp): `pixels` holds
// width * height indices, `palette` 256 RGB triplets.
std::vector<std::uint8_t> encodePngIndexed(int width, int height, const std::vector<std::uint8_t>& pixels,
                                           const std::vector<std::uint8_t>& palette);

void writePngRgba(const std::filesystem::path& path, int width, int height, const std::vector<std::uint8_t>& rgba);

} // namespace openday::formats
