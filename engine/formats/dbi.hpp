// Reader for DBI sprite archives. See docs/formats/DBI.md.
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "formats/palette.hpp"

namespace openday::formats {

struct DbiError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// Decoded sprite: one palette index per pixel plus a parallel opacity mask.
struct DbiSprite {
    std::string name;
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> pixels; // width * height palette indices
    std::vector<std::uint8_t> opaque; // width * height, 1 = opaque, 0 = transparent
};

class DbiArchive {
public:
    static constexpr std::size_t kPaletteOffset = 0x20;
    static constexpr std::size_t kSlotsOffset = 0x420;
    static constexpr std::size_t kSlotSize = 8192;
    static constexpr std::size_t kIndexEntrySize = 12;

    static DbiArchive fromFile(const std::filesystem::path& path);
    static DbiArchive fromBytes(std::vector<std::uint8_t> bytes);

    std::size_t count() const { return count_; }
    int nominalWidth() const { return nominalWidth_; }
    int nominalHeight() const { return nominalHeight_; }
    bool transparentFlag() const { return transparentFlag_; }
    const Palette& palette() const { return palette_; }

    // Name stored in the given slot's record.
    std::string slotName(std::size_t slot) const;
    // Slot for a name, via the trailing name index.
    std::optional<std::size_t> findSlot(const std::string& name) const;

    DbiSprite decode(std::size_t slot) const;

private:
    std::vector<std::uint8_t> data_;
    std::size_t count_ = 0;
    int nominalWidth_ = 0;
    int nominalHeight_ = 0;
    bool transparentFlag_ = false;
    Palette palette_{};
    std::unordered_map<std::string, std::size_t> index_;
};

// Writes a minimal DBI file (exact data sizes, zero padding). Used to build
// synthetic test fixtures; every sprite must fit its 8192-byte slot.
std::vector<std::uint8_t> writeDbi(const std::vector<DbiSprite>& sprites, const Palette& palette,
                                   bool transparentFlag);

} // namespace openday::formats
