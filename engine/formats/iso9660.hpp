// Read-only ISO 9660 disc images (docs/formats/ISO9660.md): a copy of the
// game CD. Plain 2048-byte-sector images (.iso) and raw 2352-byte-sector
// images (.bin of a .bin/.cue pair, mode 1 or mode 2 form 1) are read; the
// Joliet tree is preferred for its long, mixed-case names. Written from
// ECMA-119 and the Joliet specification.
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "formats/byte_source.hpp"

namespace openday::formats {

struct IsoError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct IsoEntry {
    std::string path; // '/'-separated below the root, UTF-8 ("INTERF/PAL.BMP")
    bool directory = false;
    std::uint32_t extent = 0; // first logical block
    std::uint64_t size = 0;   // bytes (sum of the extents of a multi-extent file)
    // Further extents of a file larger than 4 GiB (rare): (block, bytes).
    std::vector<std::pair<std::uint32_t, std::uint32_t>> moreExtents;
};

class IsoImage {
public:
    // Throws IsoError if the file is not an ISO 9660 image.
    static IsoImage open(const std::filesystem::path& path);
    // True if the file looks like an ISO 9660 image (cheap check).
    static bool probe(const std::filesystem::path& path);

    IsoImage(IsoImage&&) noexcept;
    IsoImage& operator=(IsoImage&&) noexcept;
    ~IsoImage();

    const std::string& volumeName() const { return volume_; }
    // Every file and directory, parents before children.
    const std::vector<IsoEntry>& entries() const { return entries_; }

    // Streams a file's contents to `sink` in pieces.
    void read(const IsoEntry& entry, const std::function<void(const std::uint8_t*, std::size_t)>& sink) const;
    std::vector<std::uint8_t> read(const IsoEntry& entry) const;

private:
    IsoImage() = default;
    void readSectors(std::uint32_t lba, std::uint8_t* out, std::size_t count) const;
    void readBytes(std::uint32_t lba, std::uint64_t size,
                   const std::function<void(const std::uint8_t*, std::size_t)>& sink) const;

    std::unique_ptr<FileSource> file_;
    std::uint32_t sectorSize_ = 2048; // physical sector size in the image file
    std::uint32_t dataOffset_ = 0;    // offset of the user data in a physical sector
    std::string volume_;
    std::vector<IsoEntry> entries_;
};

} // namespace openday::formats
