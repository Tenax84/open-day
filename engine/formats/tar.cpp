#include "formats/tar.hpp"

#include <cstring>
#include <string_view>

#include "formats/inflate.hpp"

namespace openday::formats {
namespace {

std::uint32_t le32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | static_cast<std::uint32_t>(p[1]) << 8 | static_cast<std::uint32_t>(p[2]) << 16 |
           static_cast<std::uint32_t>(p[3]) << 24;
}

// An octal number field (NUL- or space-terminated).
std::uint64_t octal(const std::uint8_t* p, std::size_t n) {
    std::uint64_t v = 0;
    std::size_t i = 0;
    while (i < n && p[i] == ' ') ++i;
    for (; i < n && p[i] >= '0' && p[i] <= '7'; ++i) v = v * 8 + (p[i] - '0');
    return v;
}

std::string field(const std::uint8_t* p, std::size_t n) {
    std::size_t len = 0;
    while (len < n && p[len] != 0) ++len;
    return std::string(reinterpret_cast<const char*>(p), len);
}

// The "path" (and "linkpath") records of a pax extended header.
void paxRecords(const std::vector<std::uint8_t>& data, std::string& path, std::string& link) {
    std::string_view s(reinterpret_cast<const char*>(data.data()), data.size());
    while (!s.empty()) {
        const std::size_t space = s.find(' ');
        if (space == std::string_view::npos) break;
        std::size_t len = 0;
        for (char c : s.substr(0, space)) len = len * 10 + static_cast<std::size_t>(c - '0');
        if (len <= space + 1 || len > s.size()) break;
        std::string_view record = s.substr(space + 1, len - space - 2); // without the final '\n'
        const std::size_t eq = record.find('=');
        if (eq != std::string_view::npos) {
            if (record.substr(0, eq) == "path") path = std::string(record.substr(eq + 1));
            else if (record.substr(0, eq) == "linkpath") link = std::string(record.substr(eq + 1));
        }
        s.remove_prefix(len);
    }
}

} // namespace

std::vector<std::uint8_t> gunzip(std::span<const std::uint8_t> gz, std::size_t maxSize) {
    std::vector<std::uint8_t> out;
    std::size_t pos = 0;
    while (pos < gz.size()) {
        const std::uint8_t* h = gz.data() + pos;
        if (gz.size() - pos < 18 || h[0] != 0x1F || h[1] != 0x8B || h[2] != 8) {
            if (out.empty()) throw TarError("gzip: not a gzip file");
            break; // trailing padding after the last member
        }
        const std::uint8_t flags = h[3];
        std::size_t p = pos + 10;
        auto need = [&](std::size_t n) {
            if (gz.size() - p < n) throw TarError("gzip: truncated file");
        };
        if (flags & 4) { // FEXTRA
            need(2);
            const std::size_t xlen = gz[p] | static_cast<std::size_t>(gz[p + 1]) << 8;
            need(2 + xlen);
            p += 2 + xlen;
        }
        for (int bit : {8, 16}) // FNAME, FCOMMENT: zero-terminated
            if (flags & bit) {
                while (p < gz.size() && gz[p] != 0) ++p;
                need(1);
                ++p;
            }
        if (flags & 2) { // FHCRC
            need(2);
            p += 2;
        }
        std::size_t used = 0;
        std::vector<std::uint8_t> member = inflate(gz.subspan(p), 0, maxSize - out.size(), &used);
        p += used;
        need(8);
        if (le32(gz.data() + p) != crc32(member) || le32(gz.data() + p + 4) != static_cast<std::uint32_t>(member.size()))
            throw TarError("gzip: CRC or size mismatch");
        pos = p + 8;
        out.insert(out.end(), member.begin(), member.end());
    }
    return out;
}

std::vector<TarEntry> readTar(std::span<const std::uint8_t> tar) {
    std::vector<TarEntry> entries;
    std::string longPath, longLink;
    std::size_t pos = 0;
    while (pos + 512 <= tar.size()) {
        const std::uint8_t* h = tar.data() + pos;
        bool zero = true;
        for (int i = 0; i < 512 && zero; ++i) zero = h[i] == 0;
        if (zero) break; // end-of-archive blocks
        unsigned sum = 0;
        for (int i = 0; i < 512; ++i) sum += i >= 148 && i < 156 ? ' ' : h[i];
        if (sum != octal(h + 148, 8)) throw TarError("tar: header checksum mismatch");
        const std::uint64_t size = octal(h + 124, 12);
        const std::size_t dataPos = pos + 512;
        if (size > tar.size() - dataPos) throw TarError("tar: truncated member");
        std::vector<std::uint8_t> data(tar.begin() + static_cast<std::ptrdiff_t>(dataPos),
                                       tar.begin() + static_cast<std::ptrdiff_t>(dataPos + size));
        pos = dataPos + (static_cast<std::size_t>(size) + 511) / 512 * 512;

        const char type = static_cast<char>(h[156]);
        if (type == 'L' || type == 'K') { // GNU long name / long link name
            (type == 'L' ? longPath : longLink) = field(data.data(), data.size());
            continue;
        }
        if (type == 'x') {
            paxRecords(data, longPath, longLink);
            continue;
        }
        if (type == 'g') continue; // global pax header

        TarEntry e;
        e.path = field(h, 100);
        if (std::memcmp(h + 257, "ustar", 5) == 0) {
            const std::string prefix = field(h + 345, 155);
            if (!prefix.empty()) e.path = prefix + "/" + e.path;
        }
        e.target = field(h + 157, 100);
        if (!longPath.empty()) e.path = std::move(longPath);
        if (!longLink.empty()) e.target = std::move(longLink);
        longPath.clear();
        longLink.clear();
        while (!e.path.empty() && e.path.back() == '/') e.path.pop_back();
        e.mode = static_cast<unsigned>(octal(h + 100, 8)) & 07777;
        if (type == '0' || type == '\0' || type == '7') e.kind = TarEntry::Kind::File;
        else if (type == '5') e.kind = TarEntry::Kind::Directory;
        else if (type == '2') e.kind = TarEntry::Kind::Symlink;
        if (e.kind == TarEntry::Kind::File) e.data = std::move(data);
        entries.push_back(std::move(e));
    }
    return entries;
}

} // namespace openday::formats
