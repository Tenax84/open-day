// Reader for MFF archives (ANMSUNIT\ANMSUNIT.FF): a directory of named,
// embedded Smacker files. See docs/formats/FF.md.
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace openday::formats {

struct FfError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct FfEntry {
    std::string name;         // internal path as stored, with '\' separators
    std::uint32_t offset = 0; // absolute file offset of the embedded file
    std::uint32_t size = 0;   // derived: distance to the next entry's data (or end of file)
};

class FfArchive {
public:
    static constexpr std::size_t kHeaderSize = 8;
    static constexpr std::size_t kEntrySize = 264;
    static constexpr std::size_t kNameSize = 256;

    // Reads only the directory; entry data is read on demand by read().
    static FfArchive open(const std::filesystem::path& path);
    // Keeps the whole archive in memory (used for tests).
    static FfArchive fromBytes(std::vector<std::uint8_t> bytes);

    std::size_t count() const { return entries_.size(); }
    // Entries in directory order (which is not sorted).
    const std::vector<FfEntry>& entries() const { return entries_; }
    const FfEntry& entry(std::size_t i) const { return entries_.at(i); }
    std::uint64_t archiveSize() const { return archiveSize_; }

    // Case-insensitive lookup; '/' and '\' are treated as the same separator.
    std::optional<std::size_t> find(std::string_view name) const;

    // Returns the bytes of an embedded file.
    std::vector<std::uint8_t> read(std::size_t i) const;

    // Lookup key for a name: upper case, '\' separators.
    static std::string normalize(std::string_view name);

private:
    void parse(const std::uint8_t* dir, std::size_t dirSize, std::uint64_t archiveSize);

    std::filesystem::path path_;
    std::vector<std::uint8_t> memory_; // whole archive when created by fromBytes
    bool inMemory_ = false;
    std::uint64_t archiveSize_ = 0;
    std::vector<FfEntry> entries_;
    std::unordered_map<std::string, std::size_t> index_;
};

// Writes an archive from (name, data) pairs, data stored in the given order.
// Names must be shorter than 256 bytes. Used to build synthetic test fixtures.
std::vector<std::uint8_t> writeFf(const std::vector<std::pair<std::string, std::vector<std::uint8_t>>>& files);

} // namespace openday::formats
