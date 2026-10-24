// Raw DEFLATE decompression (RFC 1951) and the CRC-32 used by ZIP archives.
// A small, self-contained decoder: the game's ZIP archives are the only
// compressed data the engine reads.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace openday::formats {

struct InflateError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// Decompresses a raw DEFLATE stream (no zlib or gzip header). `sizeHint`
// reserves the output buffer when the size is known. Throws InflateError on
// malformed or truncated input.
std::vector<std::uint8_t> inflate(std::span<const std::uint8_t> compressed, std::size_t sizeHint = 0);

// CRC-32 (ISO-HDLC, as in ZIP and PNG). `crc` continues a running value.
std::uint32_t crc32(std::span<const std::uint8_t> data, std::uint32_t crc = 0);

} // namespace openday::formats
