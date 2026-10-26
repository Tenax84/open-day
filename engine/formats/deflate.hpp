// Raw DEFLATE compression (RFC 1951): LZ77 matching with the fixed Huffman
// codes, in one block. Small and fast rather than tight; used for the PNG
// screenshots of the debug server.
#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace openday::formats {

// Compresses `data` into a raw DEFLATE stream (no zlib or gzip header).
std::vector<std::uint8_t> deflate(std::span<const std::uint8_t> data);

} // namespace openday::formats
