#include "formats/pe_resources.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>

#include "formats/binary.hpp"

namespace openday::formats {
namespace {

using binary::readU16;
using binary::readU32;

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

std::vector<std::uint8_t> readFile(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw PeError("cannot open " + path.string());
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

} // namespace

std::map<int, std::vector<std::uint8_t>> loadResources(std::span<const std::uint8_t> d, std::uint32_t type) {
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
    std::map<int, std::vector<std::uint8_t>> out;
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
    for (const auto& [t, typeOff] : entries(root)) {
        if (t != type || !(typeOff & kSubdirectory)) continue;
        for (const auto& [id, idOff] : entries(root + (typeOff & ~kSubdirectory))) {
            if (!(idOff & kSubdirectory) || (id & kSubdirectory)) continue; // named resources are skipped
            const auto langs = entries(root + (idOff & ~kSubdirectory));
            if (langs.empty() || (langs.front().second & kSubdirectory)) continue;
            const std::size_t leaf = root + langs.front().second;
            need(d, leaf, 16);
            const std::size_t p = fileOffset(readU32(d, leaf));
            const std::uint32_t size = readU32(d, leaf + 4);
            need(d, p, size);
            out[static_cast<int>(id)] = std::vector<std::uint8_t>(d.begin() + static_cast<std::ptrdiff_t>(p),
                                                                  d.begin() + static_cast<std::ptrdiff_t>(p + size));
        }
    }
    return out;
}

std::map<int, std::string> loadStringTable(std::span<const std::uint8_t> d) {
    std::map<int, std::string> out;
    for (const auto& [block, data] : loadResources(d, kResourceString)) {
        const std::span<const std::uint8_t> b = data;
        std::size_t p = 0;
        for (int i = 0; i < 16 && p + 2 <= b.size(); ++i) {
            const std::uint16_t len = readU16(b, p);
            p += 2;
            need(b, p, len * 2u);
            if (len > 0) {
                std::string s;
                for (std::uint16_t k = 0; k < len; ++k) s += utf8(readU16(b, p + k * 2u));
                out[(block - 1) * 16 + i] = std::move(s);
            }
            p += len * 2u;
        }
    }
    return out;
}

std::map<int, std::vector<std::uint8_t>> loadResources(const std::filesystem::path& path, std::uint32_t type) {
    return loadResources(readFile(path), type);
}

DibImage decodeDib(std::span<const std::uint8_t> d) {
    // BITMAPINFOHEADER, color table, pixel rows (bottom-up unless the height
    // is negative), each row padded to 4 bytes.
    need(d, 0, 40);
    const std::uint32_t headerSize = readU32(d, 0);
    const auto width = static_cast<std::int32_t>(readU32(d, 4));
    const auto height = static_cast<std::int32_t>(readU32(d, 8));
    const std::uint16_t bits = readU16(d, 14);
    const std::uint32_t compression = readU32(d, 16);
    std::uint32_t colors = readU32(d, 32);
    if (headerSize < 40) throw PeError("unsupported bitmap header");
    if (compression != 0) throw PeError("compressed bitmap not supported");
    if (bits != 1 && bits != 4 && bits != 8 && bits != 24 && bits != 32) throw PeError("unsupported bitmap depth");
    if (width <= 0 || height == 0 || width > 4096 || height > 4096 || height < -4096) throw PeError("bad bitmap size");
    if (bits > 8) colors = 0;
    else if (colors == 0 || colors > (1u << bits)) colors = 1u << bits;
    std::vector<Rgb> table(colors);
    const std::size_t tableOff = headerSize;
    need(d, tableOff, colors * 4u);
    for (std::size_t i = 0; i < colors; ++i)
        table[i] = Rgb{d[tableOff + i * 4 + 2], d[tableOff + i * 4 + 1], d[tableOff + i * 4]};
    DibImage img;
    img.width = width;
    img.height = height < 0 ? -height : height;
    const std::size_t stride = ((static_cast<std::size_t>(width) * bits + 31) / 32) * 4;
    const std::size_t pixels = tableOff + colors * 4u;
    need(d, pixels, stride * static_cast<std::size_t>(img.height));
    img.pixels.resize(static_cast<std::size_t>(img.width) * static_cast<std::size_t>(img.height));
    for (int y = 0; y < img.height; ++y) {
        const int src = height < 0 ? y : img.height - 1 - y;
        const std::size_t row = pixels + stride * static_cast<std::size_t>(src);
        for (int x = 0; x < img.width; ++x) {
            Rgb c;
            const auto ux = static_cast<std::size_t>(x);
            if (bits == 24 || bits == 32) {
                const std::size_t o = row + ux * (bits / 8u);
                c = Rgb{d[o + 2], d[o + 1], d[o]};
            } else {
                const std::size_t bit = ux * bits;
                const unsigned byte = d[row + bit / 8];
                const unsigned index = (byte >> (8 - bits - bit % 8)) & ((1u << bits) - 1);
                if (index < table.size()) c = table[index];
            }
            img.pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(img.width) + ux] = c;
        }
    }
    return img;
}

std::map<int, std::string> loadStringTable(const std::filesystem::path& path) { return loadStringTable(readFile(path)); }

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
