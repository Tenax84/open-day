// fftool: inspect and extract MFF animation archives (ANMSUNIT.FF).
//
//   fftool info    <file.ff>
//   fftool list    <file.ff> [filter]
//   fftool check   <file.ff>
//   fftool extract <file.ff> <out-dir> [filter] [--opaque]
//   fftool raw     <file.ff> <out-dir> [filter]
//   fftool sheet   <file.ff> <out.png> <filter> [max-columns] [--opaque]
//
// A filter is a case-insensitive substring of the internal path ('/' or '\').
// Frames are written as RGBA PNGs. Like the game, pixels with the palette
// index of the frame's top-left pixel are treated as transparent; --opaque
// keeps them. Output is derived from your own game data: keep it out of the
// repository.

#include "formats/ff.hpp"
#include "formats/png_writer.hpp"
#include "formats/smk.hpp"

#include <algorithm>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

using namespace openday::formats;
namespace fs = std::filesystem;

namespace {

bool gOpaque = false;

int usage() {
    std::fprintf(stderr,
                 "usage:\n"
                 "  fftool info    <file.ff>\n"
                 "  fftool list    <file.ff> [filter]\n"
                 "  fftool check   <file.ff>                                 decode everything\n"
                 "  fftool extract <file.ff> <out-dir> [filter] [--opaque]   frames as PNG, audio as WAV\n"
                 "  fftool raw     <file.ff> <out-dir> [filter]              embedded .SMK files unchanged\n"
                 "  fftool sheet   <file.ff> <out.png> <filter> [max-columns] [--opaque]\n"
                 "                 one row per matching entry, one column per frame\n");
    return 2;
}

// Entry indices matching the filter, sorted by name.
std::vector<std::size_t> select(const FfArchive& a, const std::string& filter) {
    const std::string f = FfArchive::normalize(filter);
    std::vector<std::size_t> out;
    for (std::size_t i = 0; i < a.count(); ++i)
        if (FfArchive::normalize(a.entry(i).name).find(f) != std::string::npos) out.push_back(i);
    std::sort(out.begin(), out.end(), [&](std::size_t x, std::size_t y) { return a.entry(x).name < a.entry(y).name; });
    return out;
}

// Internal path ('\' separated) as a relative filesystem path.
fs::path toPath(const std::string& name) {
    fs::path p;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= name.size(); ++i) {
        if (i == name.size() || name[i] == '\\') {
            if (i > start) p /= name.substr(start, i - start);
            start = i + 1;
        }
    }
    return p;
}

// Draws the current frame into an RGBA buffer of the given stride (pixels).
void blit(const SmkVideo& v, std::vector<std::uint8_t>& rgba, int stride, int ox, int oy) {
    const Palette& pal = v.palette();
    const std::vector<std::uint8_t>& px = v.frame();
    const std::uint8_t key = px[0];
    for (int y = 0; y < v.height(); ++y)
        for (int x = 0; x < v.width(); ++x) {
            const std::uint8_t i = px[static_cast<std::size_t>(y * v.width() + x)];
            const std::size_t dst =
                (static_cast<std::size_t>(oy + y) * static_cast<std::size_t>(stride) + static_cast<std::size_t>(ox + x)) * 4;
            if (i == key && !gOpaque) {
                std::fill_n(rgba.begin() + static_cast<std::ptrdiff_t>(dst), 4, std::uint8_t{0});
                continue;
            }
            const Rgb& c = pal[i];
            rgba[dst] = c.r;
            rgba[dst + 1] = c.g;
            rgba[dst + 2] = c.b;
            rgba[dst + 3] = 255;
        }
}

void writeWav(const fs::path& path, const SmkAudio& a) {
    std::vector<std::uint8_t> h;
    auto u32 = [&](std::uint32_t v) {
        for (int i = 0; i < 4; ++i) h.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
    };
    auto u16 = [&](std::uint32_t v) {
        h.push_back(static_cast<std::uint8_t>(v));
        h.push_back(static_cast<std::uint8_t>(v >> 8));
    };
    const auto blockAlign = static_cast<std::uint32_t>(a.channels * a.bitsPerSample / 8);
    h.insert(h.end(), {'R', 'I', 'F', 'F'});
    u32(static_cast<std::uint32_t>(36 + a.pcm.size()));
    h.insert(h.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
    u32(16);
    u16(1); // PCM
    u16(static_cast<std::uint32_t>(a.channels));
    u32(a.sampleRate);
    u32(a.sampleRate * blockAlign);
    u16(blockAlign);
    u16(static_cast<std::uint32_t>(a.bitsPerSample));
    h.insert(h.end(), {'d', 'a', 't', 'a'});
    u32(static_cast<std::uint32_t>(a.pcm.size()));
    std::ofstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot write " + path.string());
    f.write(reinterpret_cast<const char*>(h.data()), static_cast<std::streamsize>(h.size()));
    f.write(reinterpret_cast<const char*>(a.pcm.data()), static_cast<std::streamsize>(a.pcm.size()));
}

int cmdInfo(const FfArchive& a) {
    std::uint64_t data = 0;
    std::map<std::string, std::size_t> groups;
    for (const FfEntry& e : a.entries()) {
        data += e.size;
        groups[e.name.substr(0, e.name.find('\\'))]++;
    }
    std::printf("archive size:   %llu\n", static_cast<unsigned long long>(a.archiveSize()));
    std::printf("entries:        %zu\n", a.count());
    std::printf("directory:      %zu bytes\n", FfArchive::kHeaderSize + a.count() * FfArchive::kEntrySize);
    std::printf("entry data:     %llu bytes\n", static_cast<unsigned long long>(data));
    std::printf("top-level folders (%zu):\n", groups.size());
    for (const auto& [g, n] : groups) std::printf("  %-10s %5zu\n", g.c_str(), n);
    return 0;
}

int cmdList(const FfArchive& a, const std::string& filter) {
    for (std::size_t i : select(a, filter)) {
        const FfEntry& e = a.entry(i);
        const SmkVideo v(a.read(i));
        const SmkAudioTrack& t = v.audioTrack(0);
        char audio[40] = "-";
        if (t.present)
            std::snprintf(audio, sizeof audio, "%u Hz %d-bit %s", t.sampleRate, t.sixteenBit ? 16 : 8,
                          t.stereo ? "stereo" : "mono");
        std::printf("%5zu  %9u  %4d x %-4d  %3d fr%s  %5.2f fps  %-18s  %s\n", i, e.size, v.width(), v.height(),
                    v.frameCount(), v.hasRingFrame() ? "+r" : "  ", 1e6 / v.frameMicros(), audio, e.name.c_str());
    }
    return 0;
}

// Decodes every frame and audio track of every entry.
int cmdCheck(const FfArchive& a) {
    std::size_t ok = 0, frames = 0, audioBytes = 0, maxUnusedBits = 0;
    for (std::size_t i = 0; i < a.count(); ++i) {
        try {
            SmkVideo v(a.read(i));
            while (v.nextFrame()) {
                ++frames;
                maxUnusedBits = std::max(maxUnusedBits, v.unusedVideoBits());
            }
            for (int t = 0; t < SmkVideo::kAudioTracks; ++t) audioBytes += v.decodeAudio(t).pcm.size();
            ++ok;
        } catch (const std::exception& ex) {
            std::printf("FAIL %s: %s\n", a.entry(i).name.c_str(), ex.what());
        }
    }
    std::printf("decoded %zu / %zu entries: %zu frames, %zu audio bytes, at most %zu unread video bits per frame\n", ok,
                a.count(), frames, audioBytes, maxUnusedBits);
    return ok == a.count() ? 0 : 1;
}

int cmdExtract(const FfArchive& a, const fs::path& outDir, const std::string& filter, bool raw) {
    std::size_t files = 0;
    for (std::size_t i : select(a, filter)) {
        const fs::path rel = toPath(a.entry(i).name);
        const fs::path dir = outDir / rel.parent_path();
        fs::create_directories(dir);
        std::vector<std::uint8_t> bytes = a.read(i);
        if (raw) {
            std::ofstream f(dir / rel.filename(), std::ios::binary);
            if (!f) throw std::runtime_error("cannot write " + (dir / rel.filename()).string());
            f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            ++files;
            continue;
        }
        SmkVideo v(std::move(bytes));
        const std::string stem = rel.stem().string();
        std::vector<std::uint8_t> rgba(static_cast<std::size_t>(v.width()) * static_cast<std::size_t>(v.height()) * 4);
        while (v.nextFrame()) {
            blit(v, rgba, v.width(), 0, 0);
            char suffix[16];
            std::snprintf(suffix, sizeof suffix, "_%03d.png", v.currentFrame());
            writePngRgba(dir / (stem + suffix), v.width(), v.height(), rgba);
            ++files;
        }
        for (int t = 0; t < SmkVideo::kAudioTracks; ++t) {
            if (!v.audioTrack(t).present) continue;
            const SmkAudio audio = v.decodeAudio(t);
            if (audio.pcm.empty()) continue;
            writeWav(dir / (stem + (t ? "_" + std::to_string(t) : std::string()) + ".wav"), audio);
            ++files;
        }
    }
    std::printf("wrote %zu files to %s\n", files, outDir.string().c_str());
    return 0;
}

int cmdSheet(const FfArchive& a, const fs::path& out, const std::string& filter, int maxColumns) {
    const std::vector<std::size_t> sel = select(a, filter);
    if (sel.empty()) {
        std::fprintf(stderr, "no entry matches '%s'\n", filter.c_str());
        return 1;
    }
    int cellW = 0, cellH = 0, columns = 0;
    for (std::size_t i : sel) {
        const SmkVideo v(a.read(i));
        cellW = std::max(cellW, v.width() + 2);
        cellH = std::max(cellH, v.height() + 2);
        columns = std::max(columns, std::min(v.frameCount(), maxColumns));
    }
    const int w = columns * cellW, h = static_cast<int>(sel.size()) * cellH;
    if (static_cast<double>(w) * h > 64.0 * 1024 * 1024) {
        std::fprintf(stderr, "sheet would be %d x %d pixels; use a narrower filter or fewer columns\n", w, h);
        return 1;
    }
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4, 0);
    for (std::size_t row = 0; row < sel.size(); ++row) {
        SmkVideo v(a.read(sel[row]));
        while (v.nextFrame() && v.currentFrame() < columns)
            blit(v, rgba, w, v.currentFrame() * cellW + 1, static_cast<int>(row) * cellH + 1);
    }
    writePngRgba(out, w, h, rgba);
    std::printf("wrote %s (%d x %d, %zu rows)\n", out.string().c_str(), w, h, sel.size());
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--opaque")
            gOpaque = true;
        else
            args.emplace_back(argv[i]);
    }
    if (args.size() < 2) return usage();
    const std::string& cmd = args[0];
    auto arg = [&](std::size_t i) { return i < args.size() ? args[i] : std::string(); };
    try {
        const FfArchive a = FfArchive::open(args[1]);
        if (cmd == "info") return cmdInfo(a);
        if (cmd == "list") return cmdList(a, arg(2));
        if (cmd == "check") return cmdCheck(a);
        if ((cmd == "extract" || cmd == "raw") && args.size() >= 3) return cmdExtract(a, args[2], arg(3), cmd == "raw");
        if (cmd == "sheet" && args.size() >= 4)
            return cmdSheet(a, args[2], args[3], args.size() >= 5 ? std::max(1, std::stoi(args[4])) : 1 << 30);
        return usage();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}
