// Read-only Inno Setup installers (docs/formats/INNO.md), such as the GOG
// release of the game: the list of installed files and their contents.
// Setup data versions 5.0.0 to 6.4.3 are read, embedded or in external
// setup-N.bin slices; stored, zlib, LZMA and LZMA2 compression; the x86
// call filter; GOG Galaxy's multi-part files. Encrypted installers are
// refused. Written from docs/formats/INNO.md.
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

namespace openday::formats {

struct InnoError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// One stored file ("file location"): a byte range of a decompressed chunk.
struct InnoData {
    std::uint32_t firstSlice = 0, lastSlice = 0;
    std::uint64_t chunkOffset = 0; // offset of the chunk in its slice
    std::uint64_t chunkSize = 0;   // stored chunk bytes after the chunk magic
    std::uint64_t offset = 0;      // offset in the decompressed chunk
    std::uint64_t size = 0;        // stored bytes
    enum class Compression { Stored, Zlib, Bzip2, Lzma1, Lzma2 } compression = Compression::Stored;
    enum class Filter { None, Call4108, Call5200, Call5309 } filter = Filter::None;
    bool encrypted = false;
    enum class Checksum { None, Adler32, Crc32, Md5, Sha1, Sha256 } checksumType = Checksum::None;
    std::vector<std::uint8_t> checksum;
    // GOG Galaxy part: the stored bytes are a zlib stream of `inflatedSize`.
    bool galaxyPart = false;
    std::uint64_t inflatedSize = 0;
};

struct InnoFile {
    std::string destination; // UTF-8, with Inno constants: "{app}\IMGS\TERRAINS.DBI"
    std::vector<std::uint32_t> locations; // InnoData indices; several for a GOG Galaxy file
    std::uint64_t size = 0;
};

class InnoSetup {
public:
    // True if the file is an Inno Setup installer (cheap check of the loader).
    static bool probe(const std::filesystem::path& path);
    // Reads the setup headers; throws InnoError.
    static InnoSetup open(const std::filesystem::path& path);

    const std::string& version() const { return version_; } // "5.5.7 (unicode)"
    const std::string& appName() const { return appName_; }
    const std::vector<InnoFile>& files() const { return files_; }
    const std::vector<InnoData>& data() const { return data_; }

    // Receives the contents of a file in pieces: (file index, offset in the
    // file, bytes). Pieces of one file may come in any order.
    using Sink = std::function<void(std::size_t file, std::uint64_t offset, const std::uint8_t* data, std::size_t n)>;
    // (bytes done, bytes total) of the decompressed data that is read.
    using Progress = std::function<void(std::uint64_t done, std::uint64_t total)>;

    // Extracts the given files (indices into files()). Checksums are
    // verified. Throws InnoError (missing slice file, corrupt data,
    // unsupported compression or encryption).
    void extract(const std::vector<std::size_t>& files, const Sink& sink, const Progress& progress = {}) const;

private:
    std::filesystem::path path_;
    std::string version_, appName_, baseFilename_;
    std::uint64_t dataOffset_ = 0; // embedded data in the setup program; 0: external slices
    std::uint32_t slicesPerDisk_ = 1;
    std::vector<InnoFile> files_;
    std::vector<InnoData> data_;
};

} // namespace openday::formats
