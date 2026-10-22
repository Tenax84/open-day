// dbitool: inspect and extract DBI sprite archives.
//
//   dbitool info    <file.dbi>
//   dbitool list    <file.dbi>
//   dbitool extract <file.dbi> <out-dir> [name...]
//   dbitool sheet   <file.dbi> <out.png> [columns]
//
// Extracted images are RGBA PNGs with transparent runs as alpha 0. They are
// derived from your own game data: keep them out of the repository.

#include "formats/dbi.hpp"
#include "formats/png_writer.hpp"

#include <algorithm>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <string>
#include <vector>

using namespace openday::formats;

namespace {

int usage() {
    std::fprintf(stderr,
                 "usage:\n"
                 "  dbitool info    <file.dbi>\n"
                 "  dbitool list    <file.dbi>\n"
                 "  dbitool extract <file.dbi> <out-dir> [name...]\n"
                 "  dbitool sheet   <file.dbi> <out.png> [columns]\n");
    return 2;
}

// Blits a sprite into an RGBA buffer of the given stride (in pixels).
void blit(const DbiSprite& s, const Palette& pal, std::vector<std::uint8_t>& rgba, int stride, int ox, int oy) {
    for (int y = 0; y < s.height; ++y) {
        for (int x = 0; x < s.width; ++x) {
            const std::size_t src = static_cast<std::size_t>(y * s.width + x);
            if (!s.opaque[src]) continue;
            const Rgb& c = pal[s.pixels[src]];
            const std::size_t dst = (static_cast<std::size_t>(oy + y) * static_cast<std::size_t>(stride) +
                                     static_cast<std::size_t>(ox + x)) * 4;
            rgba[dst] = c.r;
            rgba[dst + 1] = c.g;
            rgba[dst + 2] = c.b;
            rgba[dst + 3] = 255;
        }
    }
}

int cmdInfo(const DbiArchive& a) {
    std::printf("sprites:        %zu\n", a.count());
    std::printf("nominal size:   %d x %d\n", a.nominalWidth(), a.nominalHeight());
    std::printf("transparent:    %s\n", a.transparentFlag() ? "yes" : "no");
    return 0;
}

int cmdList(const DbiArchive& a) {
    for (std::size_t i = 0; i < a.count(); ++i) {
        const DbiSprite s = a.decode(i);
        const auto opaque = std::count(s.opaque.begin(), s.opaque.end(), std::uint8_t{1});
        std::printf("%5zu  %-8s  %3d x %-3d  opaque px %ld\n", i, s.name.c_str(), s.width, s.height,
                    static_cast<long>(opaque));
    }
    return 0;
}

int cmdExtract(const DbiArchive& a, const std::filesystem::path& outDir, const std::vector<std::string>& names) {
    std::filesystem::create_directories(outDir);
    std::vector<std::size_t> slots;
    if (names.empty()) {
        for (std::size_t i = 0; i < a.count(); ++i) slots.push_back(i);
    } else {
        for (const std::string& n : names) {
            const auto slot = a.findSlot(n);
            if (!slot) {
                std::fprintf(stderr, "not found: %s\n", n.c_str());
                return 1;
            }
            slots.push_back(*slot);
        }
    }
    for (std::size_t slot : slots) {
        const DbiSprite s = a.decode(slot);
        std::vector<std::uint8_t> rgba(static_cast<std::size_t>(s.width * s.height) * 4, 0);
        blit(s, a.palette(), rgba, s.width, 0, 0);
        const std::string file = (s.name.empty() ? "slot" + std::to_string(slot) : s.name) + ".png";
        writePngRgba(outDir / file, s.width, s.height, rgba);
    }
    std::printf("wrote %zu images to %s\n", slots.size(), outDir.string().c_str());
    return 0;
}

int cmdSheet(const DbiArchive& a, const std::filesystem::path& out, int columns) {
    if (a.count() == 0) return 1;
    const int cellW = a.nominalWidth() + 2, cellH = a.nominalHeight() + 2;
    const int rows = static_cast<int>((a.count() + static_cast<std::size_t>(columns) - 1) / static_cast<std::size_t>(columns));
    const int w = columns * cellW, h = rows * cellH;
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4, 0);
    for (std::size_t i = 0; i < a.count(); ++i) {
        const DbiSprite s = a.decode(i);
        if (s.width > a.nominalWidth() || s.height > a.nominalHeight()) continue; // does not fit a cell
        const int col = static_cast<int>(i % static_cast<std::size_t>(columns));
        const int row = static_cast<int>(i / static_cast<std::size_t>(columns));
        blit(s, a.palette(), rgba, w, col * cellW + 1, row * cellH + 1);
    }
    writePngRgba(out, w, h, rgba);
    std::printf("wrote %s (%d x %d)\n", out.string().c_str(), w, h);
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) return usage();
    const std::string cmd = argv[1];
    try {
        const DbiArchive a = DbiArchive::fromFile(argv[2]);
        if (cmd == "info") return cmdInfo(a);
        if (cmd == "list") return cmdList(a);
        if (cmd == "extract" && argc >= 4) return cmdExtract(a, argv[3], std::vector<std::string>(argv + 4, argv + argc));
        if (cmd == "sheet" && argc >= 4) return cmdSheet(a, argv[3], argc >= 5 ? std::max(1, std::stoi(argv[4])) : 32);
        return usage();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}
