#include "formats/bmp_palette.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>

#include "formats/binary.hpp"

namespace openday::formats {

Palette readBmpPalette(const std::vector<std::uint8_t>& bytes) {
    using namespace binary;
    const Bytes d = bytes;
    // BITMAPFILEHEADER (14 bytes) followed by a BITMAPINFOHEADER of at least 40.
    if (d.size() < 54 || d[0] != 'B' || d[1] != 'M') throw BmpError("not a BMP file");
    const std::uint32_t infoSize = readU32(d, 14);
    const std::uint16_t bitCount = readU16(d, 28);
    std::uint32_t colors = readU32(d, 46);
    if (infoSize < 40) throw BmpError("unsupported BMP header");
    if (bitCount != 8) throw BmpError("BMP is not 8-bit");
    if (colors == 0 || colors > 256) colors = 256;

    const std::size_t table = 14 + infoSize;
    if (d.size() < table + colors * 4) throw BmpError("BMP color table truncated");
    Palette p{};
    for (std::size_t i = 0; i < colors; ++i) {
        const std::size_t off = table + i * 4; // blue, green, red, reserved
        p[i] = Rgb{d[off + 2], d[off + 1], d[off]};
    }
    return p;
}

BmpImage readBmpImage(const std::vector<std::uint8_t>& bytes) {
    using namespace binary;
    const Bytes d = bytes;
    BmpImage img;
    img.palette = readBmpPalette(bytes); // also checks the header
    const std::uint32_t dataOffset = readU32(d, 10);
    const auto width = static_cast<std::int32_t>(readU32(d, 18));
    const auto height = static_cast<std::int32_t>(readU32(d, 22));
    if (readU32(d, 30) != 0) throw BmpError("compressed BMP not supported");
    if (width <= 0 || height == 0 || width > 8192 || height > 8192 || height < -8192) throw BmpError("bad BMP size");
    img.width = width;
    img.height = height < 0 ? -height : height;
    const std::size_t stride = (static_cast<std::size_t>(width) + 3) & ~std::size_t{3};
    if (d.size() < dataOffset + stride * static_cast<std::size_t>(img.height)) throw BmpError("BMP pixels truncated");
    img.pixels.resize(static_cast<std::size_t>(img.width) * static_cast<std::size_t>(img.height));
    for (int y = 0; y < img.height; ++y) {
        const int src = height < 0 ? y : img.height - 1 - y; // bottom-up unless the height is negative
        const std::size_t off = dataOffset + stride * static_cast<std::size_t>(src);
        std::copy_n(bytes.begin() + static_cast<std::ptrdiff_t>(off), img.width,
                    img.pixels.begin() + static_cast<std::ptrdiff_t>(y) * img.width);
    }
    return img;
}

Palette loadBmpPalette(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw BmpError("cannot open " + path.string());
    return readBmpPalette(std::vector<std::uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>()));
}

} // namespace openday::formats
