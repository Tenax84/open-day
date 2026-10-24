// mdbtool: convert the game's Jet 3 databases to SQLite (the native
// counterpart of tools/mdb/mdb2sqlite.py, with the same output).
//
//   mdbtool [--game-dir DIR] OUT_DIR       convert a whole installation
//   mdbtool --file X.mdb [--file ...] OUT_DIR
//   mdbtool --list [--game-dir DIR | --file X.mdb ...]
//
// --game-dir defaults to $OPENDAY_GAME_DIR. An installation conversion reads
// the .MDB files of GLOBALS and SCENS and the .mdb members of the ZIP
// archives in SCENS and COLONIES, writes OUT_DIR/<folder>/.../<name>.sqlite
// and a stamp file (the game program uses the same code on first start).
// The generated files are derived from your own game data: keep them out
// of the repository.

#include "data/mdb_import.hpp"
#include "formats/mdb.hpp"

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace openday;

namespace {

int usage() {
    std::fprintf(stderr, "usage:\n"
                         "  mdbtool [--game-dir DIR] OUT_DIR\n"
                         "  mdbtool --file X.mdb [--file ...] OUT_DIR\n"
                         "  mdbtool --list [--game-dir DIR | --file X.mdb ...]\n");
    return 2;
}

std::vector<std::uint8_t> readFile(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open " + p.string());
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void listDb(const std::vector<std::uint8_t>& bytes, const std::string& label) {
    const formats::jet::Database db(bytes);
    std::vector<formats::jet::TableRef> user;
    std::vector<formats::jet::LinkRef> linked;
    db.tables(user, linked);
    std::printf("%s\n", label.c_str());
    for (const auto& t : user) {
        const formats::jet::Table table = db.readTable(t.tdefPage);
        std::printf("  %-40s %6zu rows, %zu columns\n", t.name.c_str(), table.rows.size(), table.def.columns.size());
    }
    for (const auto& l : linked) std::printf("  %-40s (linked)\n", l.name.c_str());
}

} // namespace

int main(int argc, char** argv) {
    fs::path gameDir, outDir;
    if (const char* env = std::getenv("OPENDAY_GAME_DIR")) gameDir = env;
    std::vector<fs::path> files;
    bool list = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--game-dir" && i + 1 < argc) gameDir = argv[++i];
        else if (a == "--file" && i + 1 < argc) files.emplace_back(argv[++i]);
        else if (a == "--list") list = true;
        else if (!a.empty() && a[0] != '-' && outDir.empty()) outDir = a;
        else return usage();
    }
    if (files.empty() && gameDir.empty()) {
        std::fprintf(stderr, "give --game-dir, set OPENDAY_GAME_DIR, or use --file\n");
        return 2;
    }
    if (!list && outDir.empty()) return usage();

    int failures = 0;
    try {
        if (!files.empty()) {
            for (const fs::path& f : files) {
                const std::string stem = f.stem().string();
                try {
                    const std::vector<std::uint8_t> bytes = readFile(f);
                    if (list) {
                        listDb(bytes, stem);
                        continue;
                    }
                    fs::create_directories(outDir);
                    const auto summary = data::convertMdb(bytes, outDir / (stem + ".sqlite"));
                    std::size_t rows = 0;
                    for (const auto& t : summary) rows += t.rows;
                    std::printf("%s: %zu tables, %zu rows\n", stem.c_str(), summary.size(), rows);
                } catch (const std::exception& e) {
                    ++failures;
                    std::fprintf(stderr, "%s: FAILED (%s)\n", stem.c_str(), e.what());
                }
            }
        } else if (list) {
            const data::InstallDatabases dbs(gameDir);
            for (std::size_t i = 0; i < dbs.size(); ++i) {
                try {
                    listDb(dbs.load(i), dbs.stem(i));
                } catch (const std::exception& e) {
                    ++failures;
                    std::fprintf(stderr, "%s: FAILED (%s)\n", dbs.stem(i).c_str(), e.what());
                }
            }
        } else {
            const data::ImportReport r = data::importInstallation(gameDir, outDir, [](std::size_t i, std::size_t n, const std::string& stem) {
                if (!stem.empty()) std::printf("[%3zu/%zu] %s\n", i + 1, n, stem.c_str());
            });
            for (const std::string& f : r.failures) std::fprintf(stderr, "FAILED: %s\n", f.c_str());
            std::printf("%zu databases converted into %s\n", r.converted, outDir.string().c_str());
            failures += static_cast<int>(r.failures.size());
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    return failures ? 1 : 0;
}
