#include "formats/palcycle.hpp"

#include "formats/binary.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <utility>

namespace openday::formats {

PalCycle PalCycle::fromFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw PalCycleError("cannot open " + path.string());
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return fromBytes(bytes);
}

PalCycle PalCycle::fromBytes(const std::vector<std::uint8_t>& bytes) {
    const binary::Bytes d = bytes;
    if (d.size() < kHeaderSize) throw PalCycleError("file too small");

    // The count is signed in the original; a non-positive count means an
    // empty table. Bytes after the last record are ignored.
    const auto count = static_cast<std::int32_t>(binary::readU32(d, 0));
    const std::size_t n = count > 0 ? static_cast<std::size_t>(count) : 0;
    if ((d.size() - kHeaderSize) / kEntrySize < n) throw PalCycleError("file shorter than its record count");

    std::vector<PalCycleEntry> entries;
    entries.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        const std::size_t off = kHeaderSize + i * kEntrySize;
        entries.push_back(PalCycleEntry{d[off], Rgb{d[off + 1], d[off + 2], d[off + 3]}, d[off + 4]});
    }
    return fromEntries(std::move(entries));
}

PalCycle PalCycle::fromEntries(std::vector<PalCycleEntry> entries) {
    PalCycle c;
    c.entries_ = std::move(entries);
    for (const PalCycleEntry& e : c.entries_) c.stepCount_ = std::max(c.stepCount_, std::uint32_t{e.step} + 1);
    return c;
}

std::uint32_t PalCycle::stepAt(std::uint64_t elapsedMs) const {
    if (stepCount_ == 0) return 0;
    return static_cast<std::uint32_t>((elapsedMs / kStepMs) % stepCount_);
}

Palette PalCycle::apply(const Palette& base, std::uint32_t step) const {
    Palette out = base;
    if (stepCount_ == 0) return out;
    step %= stepCount_;
    for (const PalCycleEntry& e : entries_)
        if (e.step == step) out[e.index] = e.color;
    return out;
}

bool PalCycle::cycles(std::uint8_t index) const {
    return std::any_of(entries_.begin(), entries_.end(), [index](const PalCycleEntry& e) { return e.index == index; });
}

std::vector<std::uint8_t> writePalCycle(const std::vector<PalCycleEntry>& entries) {
    std::vector<std::uint8_t> out;
    out.reserve(PalCycle::kHeaderSize + entries.size() * PalCycle::kEntrySize);
    binary::putU32(out, static_cast<std::uint32_t>(entries.size()));
    for (const PalCycleEntry& e : entries) {
        out.push_back(e.index);
        out.push_back(e.color.r);
        out.push_back(e.color.g);
        out.push_back(e.color.b);
        out.push_back(e.step);
    }
    return out;
}

} // namespace openday::formats
