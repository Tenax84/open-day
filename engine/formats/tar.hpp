// gzip (RFC 1952) and tar (POSIX ustar, with GNU long names and pax path
// records) readers, for the Linux update package (a .tar.gz). Everything
// is read into memory.
#pragma once

#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace openday::formats {

struct TarError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// The decompressed contents of a gzip file (all members); checks the CRC
// and size of each. Throws TarError (or InflateError).
std::vector<std::uint8_t> gunzip(std::span<const std::uint8_t> gz, std::size_t maxSize = std::size_t{1} << 30);

struct TarEntry {
    enum class Kind { File, Directory, Symlink, Other };
    std::string path; // '/'-separated, as stored (a trailing '/' removed)
    Kind kind = Kind::Other;
    unsigned mode = 0;  // permission bits
    std::string target; // Symlink
    std::vector<std::uint8_t> data;
};

// The entries of an uncompressed tar archive in stored order.
std::vector<TarEntry> readTar(std::span<const std::uint8_t> tar);

} // namespace openday::formats
