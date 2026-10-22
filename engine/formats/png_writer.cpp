#include "formats/png_writer.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <stdexcept>
#include <string_view>

namespace openday::formats {
namespace {

std::uint32_t crc32(const std::uint8_t* data, std::size_t len, std::uint32_t crc = 0) {
    static const auto table = [] {
        std::array<std::uint32_t, 256> t{};
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    crc = ~crc;
    for (std::size_t i = 0; i < len; ++i) crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

void putU32Be(std::vector<std::uint8_t>& out, std::uint32_t v) {
    for (int i = 3; i >= 0; --i) out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
}

void putChunk(std::vector<std::uint8_t>& out, std::string_view type, const std::vector<std::uint8_t>& body) {
    putU32Be(out, static_cast<std::uint32_t>(body.size()));
    const std::size_t typeStart = out.size();
    out.insert(out.end(), type.begin(), type.end());
    out.insert(out.end(), body.begin(), body.end());
    putU32Be(out, crc32(out.data() + typeStart, out.size() - typeStart));
}

// zlib stream made of stored (uncompressed) deflate blocks.
std::vector<std::uint8_t> zlibStore(const std::vector<std::uint8_t>& raw) {
    std::vector<std::uint8_t> z = {0x78, 0x01};
    std::size_t pos = 0;
    do {
        const std::size_t n = std::min<std::size_t>(raw.size() - pos, 65535);
        const bool last = pos + n == raw.size();
        z.push_back(last ? 1 : 0);
        z.push_back(static_cast<std::uint8_t>(n));
        z.push_back(static_cast<std::uint8_t>(n >> 8));
        z.push_back(static_cast<std::uint8_t>(~n));
        z.push_back(static_cast<std::uint8_t>(~n >> 8));
        z.insert(z.end(), raw.begin() + static_cast<std::ptrdiff_t>(pos),
                 raw.begin() + static_cast<std::ptrdiff_t>(pos + n));
        pos += n;
    } while (pos < raw.size());

    std::uint32_t a = 1, b = 0;
    for (std::uint8_t v : raw) {
        a = (a + v) % 65521;
        b = (b + a) % 65521;
    }
    putU32Be(z, (b << 16) | a);
    return z;
}

} // namespace

std::vector<std::uint8_t> encodePngRgba(int width, int height, const std::vector<std::uint8_t>& rgba) {
    const std::size_t stride = static_cast<std::size_t>(width) * 4;
    if (width <= 0 || height <= 0 || rgba.size() != stride * static_cast<std::size_t>(height))
        throw std::invalid_argument("encodePngRgba: bad dimensions");

    std::vector<std::uint8_t> raw;
    raw.reserve((stride + 1) * static_cast<std::size_t>(height));
    for (int y = 0; y < height; ++y) {
        raw.push_back(0); // filter: none
        const auto row = rgba.begin() + static_cast<std::ptrdiff_t>(stride * static_cast<std::size_t>(y));
        raw.insert(raw.end(), row, row + static_cast<std::ptrdiff_t>(stride));
    }

    std::vector<std::uint8_t> out = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    std::vector<std::uint8_t> ihdr;
    putU32Be(ihdr, static_cast<std::uint32_t>(width));
    putU32Be(ihdr, static_cast<std::uint32_t>(height));
    ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0}); // 8-bit, RGBA, deflate, adaptive filter, no interlace
    putChunk(out, "IHDR", ihdr);
    putChunk(out, "IDAT", zlibStore(raw));
    putChunk(out, "IEND", {});
    return out;
}

void writePngRgba(const std::filesystem::path& path, int width, int height, const std::vector<std::uint8_t>& rgba) {
    const std::vector<std::uint8_t> png = encodePngRgba(width, height, rgba);
    std::ofstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot write " + path.string());
    f.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
}

} // namespace openday::formats
