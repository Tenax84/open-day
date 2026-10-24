// Read-only ZIP archive reader (stored and Deflate members), used for the
// scenario and colony archives of the game (SCENS\*.ZIP, COLONIES\COLONIES.ZIP).
// Members are extracted into memory; nothing is written to disk.
#pragma once

#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace openday::formats {

struct ZipError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct ZipEntry {
    std::string name; // as stored ('/'-separated; code page 437 or UTF-8 bytes, unconverted)
    std::uint16_t method = 0; // 0 stored, 8 deflate
    std::uint16_t flags = 0;
    std::uint32_t crc = 0;
    std::uint64_t compressedSize = 0;
    std::uint64_t size = 0;
    std::uint64_t localHeaderOffset = 0;

    bool isDirectory() const { return !name.empty() && name.back() == '/'; }
};

class ZipArchive {
public:
    static ZipArchive open(const std::filesystem::path& path);
    static ZipArchive fromBytes(std::vector<std::uint8_t> bytes);

    // Entries in central-directory order.
    const std::vector<ZipEntry>& entries() const { return entries_; }

    // Decompresses one member and checks its CRC. Throws ZipError for
    // encrypted members and unsupported methods.
    std::vector<std::uint8_t> read(const ZipEntry& entry) const;

private:
    std::vector<std::uint8_t> data_;
    std::vector<ZipEntry> entries_;
};

// Builds a ZIP archive from (name, data, deflated data or empty) members.
// A member with empty `deflated` is stored uncompressed. Used to make
// synthetic test fixtures.
struct ZipWriterMember {
    std::string name;
    std::vector<std::uint8_t> data;
    std::vector<std::uint8_t> deflated;
};
std::vector<std::uint8_t> writeZip(const std::vector<ZipWriterMember>& members);

} // namespace openday::formats
