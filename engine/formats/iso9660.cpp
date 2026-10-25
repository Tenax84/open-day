#include "formats/iso9660.hpp"

#include <algorithm>
#include <cstring>
#include <set>

namespace openday::formats {
namespace {

constexpr std::uint32_t kBlock = 2048;
constexpr std::uint32_t kFirstDescriptor = 16;
constexpr int kMaxDepth = 32;

// Physical sector layouts: (sector size, offset of the 2048 user bytes).
constexpr std::pair<std::uint32_t, std::uint32_t> kLayouts[] = {
    {2048, 0},  // .iso
    {2352, 16}, // raw mode 1
    {2352, 24}, // raw mode 2 form 1 (XA)
};

std::uint32_t le32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

void appendUtf8(std::string& s, char32_t c) {
    if (c < 0x80) {
        s += static_cast<char>(c);
    } else if (c < 0x800) {
        s += static_cast<char>(0xC0 | (c >> 6));
        s += static_cast<char>(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
        s += static_cast<char>(0xE0 | (c >> 12));
        s += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        s += static_cast<char>(0x80 | (c & 0x3F));
    } else {
        s += static_cast<char>(0xF0 | (c >> 18));
        s += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
        s += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        s += static_cast<char>(0x80 | (c & 0x3F));
    }
}

std::string ucs2be(const std::uint8_t* p, std::size_t bytes) {
    std::string s;
    for (std::size_t i = 0; i + 1 < bytes; i += 2) {
        char32_t c = static_cast<char32_t>((p[i] << 8) | p[i + 1]);
        if (c >= 0xD800 && c < 0xDC00 && i + 3 < bytes) {
            const char32_t lo = static_cast<char32_t>((p[i + 2] << 8) | p[i + 3]);
            if (lo >= 0xDC00 && lo < 0xE000) {
                c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00);
                i += 2;
            }
        }
        appendUtf8(s, c);
    }
    return s;
}

// A primary-tree name: d-characters in ASCII; other bytes are kept as
// Latin-1 so that nothing is lost.
std::string latin1(const std::uint8_t* p, std::size_t bytes) {
    std::string s;
    for (std::size_t i = 0; i < bytes; ++i) appendUtf8(s, p[i]);
    return s;
}

std::string trimRight(std::string s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == '\0')) s.pop_back();
    return s;
}

// Removes the ";1" version and the dot of an extension-less name.
std::string cleanName(std::string name, bool directory) {
    if (!directory) {
        if (const auto semi = name.rfind(';'); semi != std::string::npos) name.resize(semi);
        if (!name.empty() && name.back() == '.') name.pop_back();
    }
    return name;
}

struct Root {
    std::uint32_t extent = 0, size = 0;
};

} // namespace

IsoImage::IsoImage(IsoImage&&) noexcept = default;
IsoImage& IsoImage::operator=(IsoImage&&) noexcept = default;
IsoImage::~IsoImage() = default;

void IsoImage::readSectors(std::uint32_t lba, std::uint8_t* out, std::size_t count) const {
    for (std::size_t i = 0; i < count; ++i) {
        const std::uint64_t off = static_cast<std::uint64_t>(lba + i) * sectorSize_ + dataOffset_;
        if (off + kBlock > file_->size()) throw IsoError("disc image truncated");
        file_->setRange(off, kBlock);
        file_->readExact(out + i * kBlock, kBlock, "disc image");
    }
}

void IsoImage::readBytes(std::uint32_t lba, std::uint64_t size,
                         const std::function<void(const std::uint8_t*, std::size_t)>& sink) const {
    if (sectorSize_ == kBlock) {
        const std::uint64_t off = static_cast<std::uint64_t>(lba) * kBlock;
        if (off + size > file_->size()) throw IsoError("disc image truncated");
        file_->setRange(off, size);
        std::vector<std::uint8_t> buf(1 << 20);
        while (size > 0) {
            const std::size_t n = static_cast<std::size_t>(std::min<std::uint64_t>(size, buf.size()));
            file_->readExact(buf.data(), n, "disc image");
            sink(buf.data(), n);
            size -= n;
        }
        return;
    }
    std::vector<std::uint8_t> buf(64 * kBlock);
    while (size > 0) {
        const std::uint64_t blocks = std::min<std::uint64_t>((size + kBlock - 1) / kBlock, 64);
        readSectors(lba, buf.data(), static_cast<std::size_t>(blocks));
        const std::size_t n = static_cast<std::size_t>(std::min<std::uint64_t>(size, blocks * kBlock));
        sink(buf.data(), n);
        size -= n;
        lba += static_cast<std::uint32_t>(blocks);
    }
}

bool IsoImage::probe(const std::filesystem::path& path) {
    try {
        FileSource f(path);
        for (const auto& [sector, offset] : kLayouts) {
            const std::uint64_t off = static_cast<std::uint64_t>(kFirstDescriptor) * sector + offset;
            if (off + 8 > f.size()) continue;
            std::uint8_t d[8];
            f.setRange(off, 8);
            f.readExact(d, 8);
            if (std::memcmp(d + 1, "CD001", 5) == 0) return true;
        }
    } catch (const std::exception&) {
    }
    return false;
}

IsoImage IsoImage::open(const std::filesystem::path& path) {
    IsoImage img;
    img.file_ = std::make_unique<FileSource>(path);
    bool found = false;
    for (const auto& [sector, offset] : kLayouts) {
        const std::uint64_t off = static_cast<std::uint64_t>(kFirstDescriptor) * sector + offset;
        if (off + kBlock > img.file_->size()) continue;
        std::uint8_t d[8];
        img.file_->setRange(off, 8);
        img.file_->readExact(d, 8);
        if (std::memcmp(d + 1, "CD001", 5) == 0) {
            img.sectorSize_ = sector;
            img.dataOffset_ = offset;
            found = true;
            break;
        }
    }
    if (!found) throw IsoError("not an ISO 9660 disc image: " + path.string());

    Root primary, joliet;
    std::string primaryName, jolietName;
    std::vector<std::uint8_t> d(kBlock);
    for (std::uint32_t lba = kFirstDescriptor; lba < kFirstDescriptor + 64; ++lba) {
        img.readSectors(lba, d.data(), 1);
        if (std::memcmp(d.data() + 1, "CD001", 5) != 0) break;
        const std::uint8_t type = d[0];
        if (type == 255) break;
        const std::uint8_t* root = d.data() + 156;
        if (type == 1 && primary.size == 0) {
            primary = {le32(root + 2), le32(root + 10)};
            primaryName = trimRight(latin1(d.data() + 40, 32));
        } else if (type == 2 && d[88] == '%' && d[89] == '/' && (d[90] == '@' || d[90] == 'C' || d[90] == 'E')) {
            joliet = {le32(root + 2), le32(root + 10)};
            jolietName = trimRight(ucs2be(d.data() + 40, 32));
        }
    }
    const bool useJoliet = joliet.size != 0;
    const Root root = useJoliet ? joliet : primary;
    if (root.size == 0) throw IsoError("ISO 9660 image without a root directory");
    img.volume_ = useJoliet && !jolietName.empty() ? jolietName : primaryName;

    std::set<std::uint32_t> visited;
    std::function<void(const Root&, const std::string&, int)> walk = [&](const Root& dir, const std::string& prefix,
                                                                         int depth) {
        if (depth > kMaxDepth || !visited.insert(dir.extent).second) return;
        const std::uint32_t blocks = (dir.size + kBlock - 1) / kBlock;
        std::vector<std::uint8_t> data(static_cast<std::size_t>(blocks) * kBlock);
        img.readSectors(dir.extent, data.data(), blocks);
        std::vector<Root> subdirs;
        std::vector<std::string> subdirPaths;
        std::size_t off = 0;
        bool pendingMore = false;
        while (off < dir.size) {
            const std::uint8_t len = data[off];
            if (len == 0) { // records do not cross block boundaries
                off = (off / kBlock + 1) * kBlock;
                continue;
            }
            if (len < 34 || off + len > data.size()) throw IsoError("corrupt ISO 9660 directory record");
            const std::uint8_t* r = data.data() + off;
            off += len;
            const std::uint8_t nameLen = r[32];
            if (33u + nameLen > len) throw IsoError("corrupt ISO 9660 directory record");
            if (nameLen == 1 && (r[33] == 0 || r[33] == 1)) continue; // "." and ".."
            const bool isDir = (r[25] & 0x02) != 0;
            const bool more = (r[25] & 0x80) != 0;
            std::string name = cleanName(useJoliet ? ucs2be(r + 33, nameLen) : latin1(r + 33, nameLen), isDir);
            if (name.empty() || name == "." || name == ".." || name.find('/') != std::string::npos) continue;
            const std::string path = prefix.empty() ? name : prefix + "/" + name;
            // A multi-extent file: the records of its parts follow each other.
            if (!img.entries_.empty() && pendingMore && img.entries_.back().path == path) {
                IsoEntry& e = img.entries_.back();
                e.moreExtents.emplace_back(le32(r + 2), le32(r + 10));
                e.size += le32(r + 10);
                pendingMore = more;
                continue;
            }
            pendingMore = more;
            IsoEntry e;
            e.path = path;
            e.directory = isDir;
            e.extent = le32(r + 2);
            e.size = le32(r + 10);
            img.entries_.push_back(e);
            if (isDir) {
                subdirs.push_back({e.extent, static_cast<std::uint32_t>(e.size)});
                subdirPaths.push_back(path);
            }
        }
        for (std::size_t i = 0; i < subdirs.size(); ++i) walk(subdirs[i], subdirPaths[i], depth + 1);
    };
    walk(root, "", 0);
    return img;
}

void IsoImage::read(const IsoEntry& entry, const std::function<void(const std::uint8_t*, std::size_t)>& sink) const {
    if (entry.directory) throw IsoError("not a file: " + entry.path);
    std::uint64_t firstSize = entry.size;
    for (const auto& [lba, size] : entry.moreExtents) firstSize -= size;
    readBytes(entry.extent, firstSize, sink);
    for (const auto& [lba, size] : entry.moreExtents) readBytes(lba, size, sink);
}

std::vector<std::uint8_t> IsoImage::read(const IsoEntry& entry) const {
    std::vector<std::uint8_t> out;
    out.reserve(static_cast<std::size_t>(entry.size));
    read(entry, [&](const std::uint8_t* p, std::size_t n) { out.insert(out.end(), p, p + n); });
    return out;
}

} // namespace openday::formats
