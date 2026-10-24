#include "formats/zip.hpp"

#include "formats/binary.hpp"
#include "formats/inflate.hpp"

#include <fstream>
#include <iterator>
#include <utility>

namespace openday::formats {
namespace {

constexpr std::uint32_t kLocalSig = 0x04034B50;
constexpr std::uint32_t kCentralSig = 0x02014B50;
constexpr std::uint32_t kEndSig = 0x06054B50;
constexpr std::size_t kEndSize = 22;
constexpr std::size_t kCentralSize = 46;
constexpr std::size_t kLocalSize = 30;

} // namespace

ZipArchive ZipArchive::open(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw ZipError("cannot open " + path.string());
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return fromBytes(std::move(bytes));
}

ZipArchive ZipArchive::fromBytes(std::vector<std::uint8_t> bytes) {
    ZipArchive z;
    z.data_ = std::move(bytes);
    const binary::Bytes d = z.data_;
    if (d.size() < kEndSize) throw ZipError("not a ZIP archive");

    // The end-of-central-directory record is at the end, before an optional
    // comment of up to 65535 bytes.
    std::size_t end = d.size() - kEndSize;
    const std::size_t lowest = d.size() > kEndSize + 0xFFFF ? d.size() - kEndSize - 0xFFFF : 0;
    for (;;) {
        if (binary::readU32(d, end) == kEndSig && end + kEndSize + binary::readU16(d, end + 20) == d.size()) break;
        if (end == lowest) throw ZipError("ZIP end record not found");
        --end;
    }
    const std::uint16_t count = binary::readU16(d, end + 10);
    const std::uint32_t dirSize = binary::readU32(d, end + 12);
    const std::uint32_t dirOffset = binary::readU32(d, end + 16);
    if (count == 0xFFFF || dirOffset == 0xFFFFFFFFu) throw ZipError("ZIP64 archives are not supported");
    if (static_cast<std::uint64_t>(dirOffset) + dirSize > end) throw ZipError("bad central directory");

    std::size_t pos = dirOffset;
    for (std::uint16_t i = 0; i < count; ++i) {
        if (pos + kCentralSize > end || binary::readU32(d, pos) != kCentralSig) throw ZipError("bad central directory entry");
        ZipEntry e;
        e.flags = binary::readU16(d, pos + 8);
        e.method = binary::readU16(d, pos + 10);
        e.crc = binary::readU32(d, pos + 16);
        e.compressedSize = binary::readU32(d, pos + 20);
        e.size = binary::readU32(d, pos + 24);
        const std::size_t nameLen = binary::readU16(d, pos + 28);
        const std::size_t extraLen = binary::readU16(d, pos + 30);
        const std::size_t commentLen = binary::readU16(d, pos + 32);
        e.localHeaderOffset = binary::readU32(d, pos + 42);
        if (pos + kCentralSize + nameLen + extraLen + commentLen > end) throw ZipError("bad central directory entry");
        e.name.assign(reinterpret_cast<const char*>(d.data() + pos + kCentralSize), nameLen);
        z.entries_.push_back(std::move(e));
        pos += kCentralSize + nameLen + extraLen + commentLen;
    }
    return z;
}

std::vector<std::uint8_t> ZipArchive::read(const ZipEntry& e) const {
    const binary::Bytes d = data_;
    if (e.flags & 0x0001) throw ZipError(e.name + ": encrypted members are not supported");
    if (e.localHeaderOffset + kLocalSize > d.size() || binary::readU32(d, e.localHeaderOffset) != kLocalSig)
        throw ZipError(e.name + ": bad local header");
    // Sizes come from the central directory: with a data descriptor (flag
    // bit 3) the local header holds zeros.
    const std::size_t start = e.localHeaderOffset + kLocalSize + binary::readU16(d, e.localHeaderOffset + 26) +
                              binary::readU16(d, e.localHeaderOffset + 28);
    if (start + e.compressedSize > d.size()) throw ZipError(e.name + ": truncated member");
    const auto packed = d.subspan(start, e.compressedSize);

    std::vector<std::uint8_t> out;
    if (e.method == 0) {
        out.assign(packed.begin(), packed.end());
    } else if (e.method == 8) {
        try {
            out = inflate(packed, e.size, e.size);
        } catch (const InflateError& err) {
            throw ZipError(e.name + ": " + err.what());
        }
    } else {
        throw ZipError(e.name + ": unsupported compression method " + std::to_string(e.method));
    }
    if (out.size() != e.size) throw ZipError(e.name + ": size mismatch");
    if (crc32(out) != e.crc) throw ZipError(e.name + ": CRC mismatch");
    return out;
}

std::vector<std::uint8_t> writeZip(const std::vector<ZipWriterMember>& members) {
    std::vector<std::uint8_t> out, dir;
    for (const ZipWriterMember& m : members) {
        const bool deflated = !m.deflated.empty();
        const std::vector<std::uint8_t>& body = deflated ? m.deflated : m.data;
        const std::uint32_t crc = crc32(m.data);
        const auto offset = static_cast<std::uint32_t>(out.size());
        auto header = [&](std::vector<std::uint8_t>& o, bool central) {
            binary::putU32(o, central ? kCentralSig : kLocalSig);
            if (central) binary::putU16(o, 20); // made by
            binary::putU16(o, 20);              // needed to extract
            binary::putU16(o, 0);               // flags
            binary::putU16(o, deflated ? 8 : 0);
            binary::putU32(o, 0); // DOS time and date
            binary::putU32(o, crc);
            binary::putU32(o, static_cast<std::uint32_t>(body.size()));
            binary::putU32(o, static_cast<std::uint32_t>(m.data.size()));
            binary::putU16(o, static_cast<std::uint16_t>(m.name.size()));
            binary::putU16(o, 0); // extra
            if (central) {
                binary::putU16(o, 0); // comment
                binary::putU16(o, 0); // disk
                binary::putU16(o, 0); // internal attributes
                binary::putU32(o, 0); // external attributes
                binary::putU32(o, offset);
            }
            o.insert(o.end(), m.name.begin(), m.name.end());
        };
        header(out, false);
        out.insert(out.end(), body.begin(), body.end());
        header(dir, true);
    }
    const auto dirOffset = static_cast<std::uint32_t>(out.size());
    out.insert(out.end(), dir.begin(), dir.end());
    binary::putU32(out, kEndSig);
    binary::putU16(out, 0);
    binary::putU16(out, 0);
    binary::putU16(out, static_cast<std::uint16_t>(members.size()));
    binary::putU16(out, static_cast<std::uint16_t>(members.size()));
    binary::putU32(out, static_cast<std::uint32_t>(dir.size()));
    binary::putU32(out, dirOffset);
    binary::putU16(out, 0);
    return out;
}

} // namespace openday::formats
