#include "formats/dbi.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <span>

namespace openday::formats {
namespace {

constexpr std::size_t kRecordHeaderSize = 20;
constexpr std::uint16_t kRunTransparent = 1;
constexpr std::uint16_t kRunLiteral = 2;

using Bytes = std::span<const std::uint8_t>;

std::uint16_t readU16(Bytes d, std::size_t off) {
    return static_cast<std::uint16_t>(d[off] | (d[off + 1] << 8));
}

std::uint32_t readU32(Bytes d, std::size_t off) {
    return static_cast<std::uint32_t>(d[off]) | (static_cast<std::uint32_t>(d[off + 1]) << 8) |
           (static_cast<std::uint32_t>(d[off + 2]) << 16) | (static_cast<std::uint32_t>(d[off + 3]) << 24);
}

void putU16(std::vector<std::uint8_t>& out, std::uint16_t v) {
    out.push_back(static_cast<std::uint8_t>(v));
    out.push_back(static_cast<std::uint8_t>(v >> 8));
}

void putU32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
}

// Names are NUL-padded to 8 bytes; a name may fill all 8 bytes without a
// terminator, and anything after the first NUL is garbage.
std::string readName(Bytes d, std::size_t off) {
    std::string s;
    for (std::size_t i = 0; i < 8 && d[off + i] != 0; ++i) s.push_back(static_cast<char>(d[off + i]));
    return s;
}

void putName(std::vector<std::uint8_t>& out, const std::string& name) {
    for (std::size_t i = 0; i < 8; ++i) out.push_back(i < name.size() ? static_cast<std::uint8_t>(name[i]) : 0);
}

} // namespace

DbiArchive DbiArchive::fromFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw DbiError("cannot open " + path.string());
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return fromBytes(std::move(bytes));
}

DbiArchive DbiArchive::fromBytes(std::vector<std::uint8_t> bytes) {
    DbiArchive a;
    a.data_ = std::move(bytes);
    Bytes d = a.data_;

    static constexpr std::uint8_t kMagic[4] = {'d', 'b', 'i', 0};
    if (d.size() < kSlotsOffset) throw DbiError("file too small");
    if (!std::equal(std::begin(kMagic), std::end(kMagic), d.begin())) throw DbiError("bad magic");
    if (readU32(d, 0x04) != 3) throw DbiError("unsupported version");

    a.count_ = readU32(d, 0x08);
    a.nominalWidth_ = static_cast<int>(readU32(d, 0x0C));
    a.nominalHeight_ = static_cast<int>(readU32(d, 0x10));
    a.transparentFlag_ = readU32(d, 0x14) != 0;

    if (d.size() != kSlotsOffset + a.count_ * (kSlotSize + kIndexEntrySize))
        throw DbiError("file size does not match slot count");

    // Palette entries are stored as blue, green, red, reserved.
    for (std::size_t i = 0; i < 256; ++i) {
        const std::size_t off = kPaletteOffset + i * 4;
        a.palette_[i] = Rgb{d[off + 2], d[off + 1], d[off]};
    }

    const std::size_t indexOffset = kSlotsOffset + a.count_ * kSlotSize;
    for (std::size_t i = 0; i < a.count_; ++i) {
        const std::size_t off = indexOffset + i * kIndexEntrySize;
        const std::uint32_t slot = readU32(d, off + 8);
        if (slot >= a.count_) throw DbiError("index entry points past last slot");
        a.index_.emplace(readName(d, off), slot);
    }
    return a;
}

std::string DbiArchive::slotName(std::size_t slot) const {
    if (slot >= count_) throw DbiError("slot out of range");
    return readName(data_, kSlotsOffset + slot * kSlotSize);
}

std::optional<std::size_t> DbiArchive::findSlot(const std::string& name) const {
    auto it = index_.find(name);
    if (it == index_.end()) return std::nullopt;
    return it->second;
}

DbiSprite DbiArchive::decode(std::size_t slot) const {
    if (slot >= count_) throw DbiError("slot out of range");
    // Never read outside this slot: the bytes after the record are garbage.
    Bytes d = Bytes(data_).subspan(kSlotsOffset + slot * kSlotSize, kSlotSize);

    DbiSprite s;
    s.name = readName(d, 0);
    s.width = readU16(d, 0x08);
    s.height = readU16(d, 0x0A);
    const std::size_t end = kRecordHeaderSize + readU16(d, 0x0C);
    if (end > kSlotSize) throw DbiError(s.name + ": record larger than slot");

    const std::size_t w = static_cast<std::size_t>(s.width);
    s.pixels.assign(w * static_cast<std::size_t>(s.height), 0);
    s.opaque.assign(s.pixels.size(), 0);

    std::size_t row = kRecordHeaderSize;
    for (int y = 0; y < s.height && row + 4 <= end; ++y) {
        const std::uint32_t rowLen = readU32(d, row);
        if (rowLen < 4 || row + rowLen > kSlotSize) throw DbiError(s.name + ": bad row length");

        std::size_t p = row + 4;
        std::size_t x = 0;
        // A run header crossing the record end is the truncated trailing
        // transparent run; the rest of the row stays transparent.
        while (p < row + rowLen && p + 4 <= end) {
            const std::uint16_t n = readU16(d, p);
            const std::uint16_t kind = readU16(d, p + 2);
            p += 4;
            if (x + n > w) throw DbiError(s.name + ": run exceeds sprite width");
            if (kind == kRunLiteral) {
                if (p + n > end) throw DbiError(s.name + ": literal run past record end");
                const std::size_t dst = static_cast<std::size_t>(y) * w + x;
                std::copy_n(d.begin() + static_cast<std::ptrdiff_t>(p), n,
                            s.pixels.begin() + static_cast<std::ptrdiff_t>(dst));
                std::fill_n(s.opaque.begin() + static_cast<std::ptrdiff_t>(dst), n, std::uint8_t{1});
                p += n;
            } else if (kind != kRunTransparent) {
                throw DbiError(s.name + ": unknown run kind " + std::to_string(kind));
            }
            x += n;
        }
        row += rowLen;
    }
    return s;
}

std::vector<std::uint8_t> writeDbi(const std::vector<DbiSprite>& sprites, const Palette& palette,
                                   bool transparentFlag) {
    std::vector<std::uint8_t> out = {'d', 'b', 'i', 0};
    putU32(out, 3);
    putU32(out, static_cast<std::uint32_t>(sprites.size()));
    putU32(out, sprites.empty() ? 0 : static_cast<std::uint32_t>(sprites[0].width));
    putU32(out, sprites.empty() ? 0 : static_cast<std::uint32_t>(sprites[0].height));
    putU32(out, transparentFlag ? 1 : 0);
    putU32(out, 0);
    putU32(out, 0);
    for (const Rgb& c : palette) {
        out.insert(out.end(), {c.b, c.g, c.r, 0});
    }

    for (const DbiSprite& s : sprites) {
        const std::size_t w = static_cast<std::size_t>(s.width);
        std::vector<std::uint8_t> rows;
        for (int y = 0; y < s.height; ++y) {
            std::vector<std::uint8_t> runs;
            for (std::size_t x = 0; x < w;) {
                const std::size_t i = static_cast<std::size_t>(y) * w + x;
                const bool op = s.opaque[i] != 0;
                std::size_t n = 1;
                while (x + n < w && (s.opaque[i + n] != 0) == op) ++n;
                putU16(runs, static_cast<std::uint16_t>(n));
                putU16(runs, op ? kRunLiteral : kRunTransparent);
                if (op)
                    runs.insert(runs.end(), s.pixels.begin() + static_cast<std::ptrdiff_t>(i),
                                s.pixels.begin() + static_cast<std::ptrdiff_t>(i + n));
                x += n;
            }
            putU32(rows, static_cast<std::uint32_t>(runs.size() + 4));
            rows.insert(rows.end(), runs.begin(), runs.end());
        }
        if (kRecordHeaderSize + rows.size() > DbiArchive::kSlotSize)
            throw DbiError(s.name + ": sprite does not fit a slot");

        const std::size_t start = out.size();
        putName(out, s.name);
        putU16(out, static_cast<std::uint16_t>(s.width));
        putU16(out, static_cast<std::uint16_t>(s.height));
        putU16(out, static_cast<std::uint16_t>(rows.size()));
        putU16(out, transparentFlag ? 1 : 0);
        putU32(out, 0);
        out.insert(out.end(), rows.begin(), rows.end());
        out.resize(start + DbiArchive::kSlotSize, 0);
    }

    for (std::size_t slot = 0; slot < sprites.size(); ++slot) {
        putName(out, sprites[slot].name);
        putU32(out, static_cast<std::uint32_t>(slot));
    }
    return out;
}

} // namespace openday::formats
