// installtool: look into the game's installation media and copy the game
// files out of them (the game program does the same when the user picks an
// installer or a CD image instead of a game folder).
//
//   installtool info FILE              what the file is, and the game files in it
//   installtool install FILE GAME_DIR  copy the game files into GAME_DIR
//   installtool inno-list FILE         every file of an Inno Setup installer
//   installtool inno-extract FILE DIR  every file, under DIR/{app}/..., DIR/{tmp}/...
//
// The extracted files come from your own copy of the game: keep them out of
// the repository.

#include "data/game_source.hpp"
#include "formats/inno_setup.hpp"

#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace openday;

namespace {

int usage() {
    std::fprintf(stderr, "usage:\n"
                         "  installtool info FILE\n"
                         "  installtool install FILE GAME_DIR\n"
                         "  installtool inno-list FILE\n"
                         "  installtool inno-extract FILE DIR\n");
    return 2;
}

fs::path utf8Path(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }

int info(const fs::path& file) {
    const data::GameSource src = data::inspectSource(file);
    std::printf("%s\n", src.description.c_str());
    std::uint64_t total = 0;
    for (const data::SourceFile& f : src.files) {
        std::printf("%12llu  %s\n", static_cast<unsigned long long>(f.size), f.path.c_str());
        total += f.size;
    }
    std::printf("%zu files, %llu bytes\n", src.files.size(), static_cast<unsigned long long>(total));
    return 0;
}

int install(const fs::path& file, const fs::path& dir) {
    int lastPercent = -1;
    data::installFromSource(file, dir, [&](std::uint64_t done, std::uint64_t total, const std::string&) {
        const int percent = total ? static_cast<int>(done * 100 / total) : 100;
        if (percent != lastPercent && percent % 10 == 0) std::printf("%d%%\n", percent);
        lastPercent = percent;
    });
    std::printf("installed into %s\n", dir.string().c_str());
    return 0;
}

int innoList(const fs::path& file) {
    const formats::InnoSetup setup = formats::InnoSetup::open(file);
    std::printf("Inno Setup %s: %s\n", setup.version().c_str(), setup.appName().c_str());
    for (const formats::InnoFile& f : setup.files())
        std::printf("%12llu  %s\n", static_cast<unsigned long long>(f.size), f.destination.c_str());
    return 0;
}

int innoExtract(const fs::path& file, const fs::path& dir) {
    const formats::InnoSetup setup = formats::InnoSetup::open(file);
    std::vector<std::size_t> all;
    std::map<std::size_t, fs::path> paths;
    for (std::size_t i = 0; i < setup.files().size(); ++i) {
        std::string dest = setup.files()[i].destination;
        for (char& c : dest)
            if (c == ':') c = '_';
        const std::string rel = data::safeRelativePath(dest);
        if (rel.empty()) continue;
        const fs::path p = dir / utf8Path(rel);
        fs::create_directories(p.parent_path());
        std::ofstream(p, std::ios::binary | std::ios::trunc);
        paths[i] = p;
        all.push_back(i);
    }
    std::fstream out;
    std::size_t current = static_cast<std::size_t>(-1);
    setup.extract(all, [&](std::size_t f, std::uint64_t off, const std::uint8_t* d, std::size_t n) {
        if (f != current) {
            out.close();
            out.clear();
            out.open(paths.at(f), std::ios::binary | std::ios::in | std::ios::out);
            current = f;
        }
        out.seekp(static_cast<std::streamoff>(off));
        out.write(reinterpret_cast<const char*>(d), static_cast<std::streamsize>(n));
    });
    std::printf("%zu files extracted\n", all.size());
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) return usage();
    const std::string cmd = argv[1];
    try {
        if (cmd == "info" && argc == 3) return info(utf8Path(argv[2]));
        if (cmd == "install" && argc == 4) return install(utf8Path(argv[2]), utf8Path(argv[3]));
        if (cmd == "inno-list" && argc == 3) return innoList(utf8Path(argv[2]));
        if (cmd == "inno-extract" && argc == 4) return innoExtract(utf8Path(argv[2]), utf8Path(argv[3]));
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    return usage();
}
