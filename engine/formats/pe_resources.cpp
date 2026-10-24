#include "formats/pe_resources.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>

#include "formats/binary.hpp"

namespace openday::formats {
namespace {

using binary::readU16;
using binary::readU32;

constexpr std::uint32_t kStringType = 6; // RT_STRING
constexpr std::uint32_t kSubdirectory = 0x80000000u;

void need(std::span<const std::uint8_t> d, std::size_t off, std::size_t n) {
    if (off > d.size() || n > d.size() - off) throw PeError("PE file truncated");
}

std::string utf8(char32_t c) {
    std::string s;
    if (c < 0x80) {
        s += static_cast<char>(c);
    } else if (c < 0x800) {
        s += static_cast<char>(0xC0 | (c >> 6));
        s += static_cast<char>(0x80 | (c & 0x3F));
    } else {
        s += static_cast<char>(0xE0 | (c >> 12));
        s += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        s += static_cast<char>(0x80 | (c & 0x3F));
    }
    return s;
}

struct Section {
    std::uint32_t va, vsize, raw, rawSize;
};

} // namespace

std::map<int, std::string> loadStringTable(std::span<const std::uint8_t> d) {
    need(d, 0, 0x40);
    if (d[0] != 'M' || d[1] != 'Z') throw PeError("not a PE file");
    const std::uint32_t pe = readU32(d, 0x3C);
    need(d, pe, 24);
    if (readU32(d, pe) != 0x00004550) throw PeError("missing PE signature");
    const std::uint16_t sections = readU16(d, pe + 6);
    const std::uint16_t optSize = readU16(d, pe + 20);
    const std::size_t opt = pe + 24;
    need(d, opt, optSize);
    const std::uint16_t magic = readU16(d, opt);
    const std::size_t dirs = opt + (magic == 0x20B ? 112 : 96);
    need(d, dirs, 3 * 8);
    const std::uint32_t resRva = readU32(d, dirs + 2 * 8);
    std::vector<Section> table;
    const std::size_t sec = opt + optSize;
    for (std::uint16_t i = 0; i < sections; ++i) {
        const std::size_t s = sec + i * 40u;
        need(d, s, 40);
        table.push_back({readU32(d, s + 12), readU32(d, s + 8), readU32(d, s + 20), readU32(d, s + 16)});
    }
    auto fileOffset = [&](std::uint32_t rva) -> std::size_t {
        for (const Section& s : table)
            if (rva >= s.va && rva < s.va + std::max(s.vsize, s.rawSize)) return s.raw + (rva - s.va);
        throw PeError("resource address outside the sections");
    };
    std::map<int, std::string> out;
    if (resRva == 0) return out;
    const std::size_t root = fileOffset(resRva);

    // Entries of a resource directory: (id or name, offset).
    auto entries = [&](std::size_t dir) {
        need(d, dir, 16);
        const unsigned count = readU16(d, dir + 12) + readU16(d, dir + 14);
        std::vector<std::pair<std::uint32_t, std::uint32_t>> e;
        for (unsigned i = 0; i < count; ++i) {
            need(d, dir + 16 + i * 8u, 8);
            e.emplace_back(readU32(d, dir + 16 + i * 8u), readU32(d, dir + 20 + i * 8u));
        }
        return e;
    };
    for (const auto& [type, typeOff] : entries(root)) {
        if (type != kStringType || !(typeOff & kSubdirectory)) continue;
        for (const auto& [block, blockOff] : entries(root + (typeOff & ~kSubdirectory))) {
            if (!(blockOff & kSubdirectory) || (block & kSubdirectory)) continue;
            const auto langs = entries(root + (blockOff & ~kSubdirectory));
            if (langs.empty() || (langs.front().second & kSubdirectory)) continue;
            const std::size_t leaf = root + langs.front().second;
            need(d, leaf, 16);
            std::size_t p = fileOffset(readU32(d, leaf));
            const std::size_t end = p + readU32(d, leaf + 4);
            for (int i = 0; i < 16 && p + 2 <= end; ++i) {
                need(d, p, 2);
                const std::uint16_t len = readU16(d, p);
                p += 2;
                need(d, p, len * 2u);
                if (len > 0) {
                    std::string s;
                    for (std::uint16_t k = 0; k < len; ++k) s += utf8(readU16(d, p + k * 2u));
                    out[static_cast<int>((block - 1) * 16 + static_cast<std::uint32_t>(i))] = std::move(s);
                }
                p += len * 2u;
            }
        }
    }
    return out;
}

std::map<int, std::string> loadStringTable(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw PeError("cannot open " + path.string());
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return loadStringTable(bytes);
}

std::string formatMessage(const std::string& pattern, const std::vector<std::string>& args) {
    std::string out;
    for (std::size_t i = 0; i < pattern.size(); ++i) {
        if (pattern[i] != '%' || i + 1 >= pattern.size()) {
            out += pattern[i];
            continue;
        }
        const char n = pattern[i + 1];
        if (n == '%') {
            out += '%';
            ++i;
        } else if (n >= '1' && n <= '9') {
            const std::size_t k = static_cast<std::size_t>(n - '1');
            if (k < args.size()) out += args[k];
            i += 1;
            // Skip an optional "!format!" part.
            if (i + 1 < pattern.size() && pattern[i + 1] == '!') {
                const std::size_t close = pattern.find('!', i + 2);
                if (close != std::string::npos) i = close;
            }
        } else {
            out += '%';
        }
    }
    return out;
}

} // namespace openday::formats
