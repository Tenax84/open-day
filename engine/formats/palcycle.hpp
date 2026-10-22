// Reader for the palette color-cycling table (INTERF\PALCYCLE.PAL).
// See docs/formats/PALCYCLE.md.
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <vector>

#include "formats/palette.hpp"

namespace openday::formats {

struct PalCycleError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// "At animation step `step`, palette entry `index` shows `color`."
struct PalCycleEntry {
    std::uint8_t index = 0;
    Rgb color;
    std::uint8_t step = 0;
    friend bool operator==(const PalCycleEntry&, const PalCycleEntry&) = default;
};

class PalCycle {
public:
    static constexpr std::size_t kHeaderSize = 4;
    static constexpr std::size_t kEntrySize = 5;
    // Nominal duration of one animation step in the original game.
    static constexpr std::uint64_t kStepMs = 100;

    static PalCycle fromFile(const std::filesystem::path& path);
    static PalCycle fromBytes(const std::vector<std::uint8_t>& bytes);
    static PalCycle fromEntries(std::vector<PalCycleEntry> entries);

    const std::vector<PalCycleEntry>& entries() const { return entries_; }
    // Cycle length in steps: largest step value + 1, or 0 without entries.
    std::uint32_t stepCount() const { return stepCount_; }

    // Step shown after `elapsedMs` milliseconds (0 without entries).
    std::uint32_t stepAt(std::uint64_t elapsedMs) const;

    // Palette for one step (taken modulo stepCount()): a copy of `base` with
    // every entry that has a record for this step replaced by that color.
    // Entries without a record for the step keep their base color.
    Palette apply(const Palette& base, std::uint32_t step) const;
    Palette applyAt(const Palette& base, std::uint64_t elapsedMs) const { return apply(base, stepAt(elapsedMs)); }

    // True if some record targets the given palette entry.
    bool cycles(std::uint8_t index) const;

private:
    std::vector<PalCycleEntry> entries_;
    std::uint32_t stepCount_ = 0;
};

// Serializes a table in the on-disk layout. Used to build synthetic test
// fixtures.
std::vector<std::uint8_t> writePalCycle(const std::vector<PalCycleEntry>& entries);

} // namespace openday::formats
