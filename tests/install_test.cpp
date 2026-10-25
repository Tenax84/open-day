// Tests of the installation-media readers on synthetic data: an ISO 9660
// image (primary and Joliet trees, raw 2352-byte sectors) and an Inno Setup
// installer (stored chunk, GOG Galaxy parts) are written here, then read
// back through formats/iso9660, formats/inno_setup and data/game_source.
// No game file is involved.

#include "data/game_source.hpp"
#include "formats/hash.hpp"
#include "formats/inflate.hpp"
#include "formats/inno_setup.hpp"
#include "formats/iso9660.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

using namespace openday;
using namespace openday::formats;
namespace fs = std::filesystem;
using Bytes = std::vector<std::uint8_t>;

namespace {

int failures = 0;

#define CHECK(cond)                                                                       \
    do {                                                                                  \
        if (!(cond)) {                                                                    \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                                   \
        }                                                                                 \
    } while (0)

template <typename F>
bool throws(F&& f) {
    try {
        f();
    } catch (const std::exception&) {
        return true;
    }
    return false;
}

Bytes text(const std::string& s) { return {s.begin(), s.end()}; }

Bytes pattern(std::size_t n, std::uint32_t seed) {
    Bytes b(n);
    for (auto& x : b) {
        seed = seed * 1664525u + 1013904223u;
        x = static_cast<std::uint8_t>(seed >> 24);
    }
    return b;
}

void writeFile(const fs::path& p, const Bytes& b) {
    std::ofstream out(p, std::ios::binary);
    out.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
}

Bytes readFile(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return Bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void put16(Bytes& b, std::size_t at, std::uint16_t v) {
    b[at] = static_cast<std::uint8_t>(v);
    b[at + 1] = static_cast<std::uint8_t>(v >> 8);
}
void put32(Bytes& b, std::size_t at, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) b[at + i] = static_cast<std::uint8_t>(v >> (8 * i));
}
void put32be(Bytes& b, std::size_t at, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) b[at + i] = static_cast<std::uint8_t>(v >> (24 - 8 * i));
}
void app16(Bytes& b, std::uint16_t v) {
    b.push_back(static_cast<std::uint8_t>(v));
    b.push_back(static_cast<std::uint8_t>(v >> 8));
}
void app32(Bytes& b, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) b.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
}
void app64(Bytes& b, std::uint64_t v) {
    app32(b, static_cast<std::uint32_t>(v));
    app32(b, static_cast<std::uint32_t>(v >> 32));
}

// ---------------------------------------------------------------------------
// A minimal ISO 9660 writer: one block per directory, primary names in
// upper case with ";1", Joliet names in UCS-2.

struct IsoFile {
    std::string path; // "LIBDAY/GLOBALS/GLOBAL.MDB"
    Bytes data;
};

Bytes buildIso(const std::vector<IsoFile>& files, bool joliet) {
    constexpr std::size_t B = 2048;
    // Directories ("" is the root), parents first.
    std::vector<std::string> dirs = {""};
    for (const IsoFile& f : files) {
        std::string p = f.path;
        for (std::size_t s = p.find('/'); s != std::string::npos; s = p.find('/', s + 1)) {
            const std::string d = p.substr(0, s);
            if (std::find(dirs.begin(), dirs.end(), d) == dirs.end()) dirs.push_back(d);
        }
    }
    auto parentOf = [](const std::string& p) { return p.rfind('/') == std::string::npos ? std::string() : p.substr(0, p.rfind('/')); };
    auto nameOf = [](const std::string& p) { return p.substr(p.rfind('/') == std::string::npos ? 0 : p.rfind('/') + 1); };

    const std::uint32_t firstDir = 19;
    const int trees = joliet ? 2 : 1;
    std::map<std::string, std::uint32_t> dirBlock[2];
    std::uint32_t next = firstDir;
    for (int t = 0; t < trees; ++t)
        for (const std::string& d : dirs) dirBlock[t][d] = next++;
    std::vector<std::uint32_t> fileBlock;
    for (const IsoFile& f : files) {
        fileBlock.push_back(next);
        next += static_cast<std::uint32_t>((f.data.size() + B - 1) / B);
        if (f.data.empty()) ++next;
    }
    Bytes img(next * B, 0);

    auto record = [&](Bytes& out, std::uint32_t extent, std::uint32_t size, bool dir, const Bytes& name) {
        std::size_t len = 33 + name.size();
        if (len % 2) ++len;
        const std::size_t at = out.size();
        out.resize(at + len, 0);
        out[at] = static_cast<std::uint8_t>(len);
        put32(out, at + 2, extent);
        put32be(out, at + 6, extent);
        put32(out, at + 10, size);
        put32be(out, at + 14, size);
        out[at + 25] = dir ? 2 : 0;
        put16(out, at + 28, 1);
        out[at + 32] = static_cast<std::uint8_t>(name.size());
        std::copy(name.begin(), name.end(), out.begin() + static_cast<std::ptrdiff_t>(at + 33));
    };
    auto encode = [&](const std::string& n, bool j, bool file) {
        std::string s = n;
        if (!j)
            for (char& c : s) c = static_cast<char>(c >= 'a' && c <= 'z' ? c - 32 : c);
        if (file) s += ";1";
        Bytes b;
        for (const char c : s) {
            if (j) b.push_back(0);
            b.push_back(static_cast<std::uint8_t>(c));
        }
        return b;
    };
    for (int t = 0; t < trees; ++t) {
        for (const std::string& d : dirs) {
            Bytes rec;
            record(rec, dirBlock[t][d], B, true, Bytes{0});
            record(rec, dirBlock[t][d.empty() ? d : parentOf(d)], B, true, Bytes{1});
            for (const std::string& sub : dirs)
                if (!sub.empty() && parentOf(sub) == d) record(rec, dirBlock[t][sub], B, true, encode(nameOf(sub), t == 1, false));
            for (std::size_t i = 0; i < files.size(); ++i)
                if (parentOf(files[i].path) == d)
                    record(rec, fileBlock[i], static_cast<std::uint32_t>(files[i].data.size()), false,
                           encode(nameOf(files[i].path), t == 1, true));
            if (rec.size() > B) throw std::runtime_error("test directory too large");
            std::copy(rec.begin(), rec.end(), img.begin() + static_cast<std::ptrdiff_t>(dirBlock[t][d] * B));
        }
        // Volume descriptor: 16 primary, 17 Joliet.
        const std::size_t vd = (16 + t) * B;
        img[vd] = static_cast<std::uint8_t>(t == 0 ? 1 : 2);
        std::memcpy(img.data() + vd + 1, "CD001", 5);
        img[vd + 6] = 1;
        const std::string vol = "SYNTH_DISC";
        for (std::size_t i = 0; i < 32; ++i) {
            if (t == 0) img[vd + 40 + i] = i < vol.size() ? static_cast<std::uint8_t>(vol[i]) : ' ';
            else if (i % 2 == 1) img[vd + 40 + i] = i / 2 < vol.size() ? static_cast<std::uint8_t>(vol[i / 2]) : ' ';
        }
        if (t == 1) std::memcpy(img.data() + vd + 88, "%/E", 3);
        Bytes root;
        record(root, dirBlock[t][""], B, true, Bytes{0});
        std::copy(root.begin(), root.end(), img.begin() + static_cast<std::ptrdiff_t>(vd + 156));
    }
    const std::size_t term = (16 + trees) * B;
    img[term] = 255;
    std::memcpy(img.data() + term + 1, "CD001", 5);
    for (std::size_t i = 0; i < files.size(); ++i)
        std::copy(files[i].data.begin(), files[i].data.end(), img.begin() + static_cast<std::ptrdiff_t>(fileBlock[i] * B));
    return img;
}

// The same image with raw 2352-byte mode-1 sectors (as in a .bin/.cue pair).
Bytes rawSectors(const Bytes& iso) {
    Bytes out;
    for (std::size_t i = 0; i < iso.size() / 2048; ++i) {
        const std::uint8_t sync[16] = {0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0, 0, 2, 0, 1};
        out.insert(out.end(), sync, sync + 16);
        out.insert(out.end(), iso.begin() + static_cast<std::ptrdiff_t>(i * 2048),
                   iso.begin() + static_cast<std::ptrdiff_t>((i + 1) * 2048));
        out.resize(out.size() + 288, 0);
    }
    return out;
}

// ---------------------------------------------------------------------------
// A minimal Inno Setup 5.5.7 (Unicode) installer: the old 0x30 loader
// pointer, both header blocks stored, one stored chunk.

void appString(Bytes& b, const std::string& s) { // UTF-16LE (ASCII only)
    app32(b, static_cast<std::uint32_t>(s.size() * 2));
    for (const char c : s) app16(b, static_cast<std::uint8_t>(c));
}

Bytes block(const Bytes& data) {
    Bytes stored;
    for (std::size_t at = 0; at < data.size(); at += 4096) {
        const std::size_t n = std::min<std::size_t>(4096, data.size() - at);
        app32(stored, crc32(std::span<const std::uint8_t>(data.data() + at, n)));
        stored.insert(stored.end(), data.begin() + static_cast<std::ptrdiff_t>(at),
                      data.begin() + static_cast<std::ptrdiff_t>(at + n));
    }
    Bytes head;
    app32(head, static_cast<std::uint32_t>(stored.size()));
    head.push_back(0); // not compressed
    Bytes out;
    app32(out, crc32(head));
    out.insert(out.end(), head.begin(), head.end());
    out.insert(out.end(), stored.begin(), stored.end());
    return out;
}

struct InnoTestFile {
    std::string destination;
    Bytes data;
    std::string beforeInstall, afterInstall;
};

// A zlib stream with stored Deflate blocks.
Bytes zlibStored(const Bytes& data) {
    Bytes z = {0x78, 0x01};
    std::size_t at = 0;
    do {
        const std::size_t n = std::min<std::size_t>(65535, data.size() - at);
        z.push_back(at + n == data.size() ? 1 : 0);
        app16(z, static_cast<std::uint16_t>(n));
        app16(z, static_cast<std::uint16_t>(~n));
        z.insert(z.end(), data.begin() + static_cast<std::ptrdiff_t>(at), data.begin() + static_cast<std::ptrdiff_t>(at + n));
        at += n;
    } while (at < data.size());
    app32(z, 0); // Adler-32, not checked
    return z;
}

Bytes buildInno(const std::vector<InnoTestFile>& files, const std::string& publisher) {
    Bytes h;
    const std::vector<std::string> strings = {
        "Synthetic Game", "Synthetic Game 1.0", "SynthId", "", publisher, "", "", "", "", "1.0",
        "{pf}\\Synthetic", "Synthetic", "setup_synthetic",                     // .. base file name
        "", "", "", "", "", "", "",                                            // uninstall .. serial
        "", "", "", "",                                                        // readme .. modify path
        "", "", "", "",                                                        // reg key, uninstallable, close filter, mutex
        "", "", "", ""};                                                       // license, info before/after, compiled code
    for (const std::string& s : strings) appString(h, s);
    const std::uint32_t counts[16] = {0, 0, 0, 0, 0, 0, 0, static_cast<std::uint32_t>(files.size()),
                                      static_cast<std::uint32_t>(files.size()), 0, 0, 0, 0, 0, 0, 0};
    for (const std::uint32_t c : counts) app32(h, c);
    h.resize(h.size() + 20 + 8);   // windows versions, back colours
    h.push_back(0);                // alpha format
    h.resize(h.size() + 20 + 8);   // password hash, salt
    app64(h, 0);                   // extra disk space
    app32(h, 1);                   // slices per disk
    h.insert(h.end(), {1, 0, 0});  // log mode, dir exists warning, privileges
    h.insert(h.end(), {0, 0});     // language dialog, detection
    h.push_back(0);                // compression: stored
    h.insert(h.end(), {2, 2});     // architectures
    h.insert(h.end(), {0, 0});     // disable dir / group page
    app64(h, 0);                   // uninstall display size
    h.resize(h.size() + 6);        // 47 options
    for (const InnoTestFile& f : files) {
        appString(h, "source");
        appString(h, f.destination);
        appString(h, "");
        appString(h, "");
        for (const std::string& s : {std::string(), std::string(), std::string(), std::string(), f.afterInstall, f.beforeInstall})
            appString(h, s);
        h.resize(h.size() + 20);
        app32(h, static_cast<std::uint32_t>(&f - files.data())); // location
        app32(h, 0);
        app64(h, 0);
        app16(h, 0xFFFF);
        app32(h, 0); // 32 options
        h.push_back(0);
    }
    appString(h, ""); // wizard image
    appString(h, ""); // small wizard image

    // One stored chunk holding every file.
    Bytes chunk;
    Bytes locations;
    for (const InnoTestFile& f : files) {
        app32(locations, 0);
        app32(locations, 0);
        app32(locations, 0); // chunk offset
        app64(locations, chunk.size());
        app64(locations, f.data.size());
        locations.resize(locations.size() + 8); // chunk size, patched below
        Sha1 sha;
        sha.update(f.data);
        const auto digest = sha.finish();
        locations.insert(locations.end(), digest.begin(), digest.end());
        app64(locations, 0x01D0000000000000ull); // time stamp
        app64(locations, 0);                     // version
        app16(locations, 0);                     // options: not compressed
        chunk.insert(chunk.end(), f.data.begin(), f.data.end());
    }
    const std::size_t entrySize = 4 + 4 + 4 + 8 + 8 + 8 + 20 + 8 + 8 + 2;
    for (std::size_t i = 0; i < files.size(); ++i) {
        const std::size_t at = i * entrySize + 28;
        for (int k = 0; k < 8; ++k) locations[at + k] = static_cast<std::uint8_t>(static_cast<std::uint64_t>(chunk.size()) >> (8 * k));
    }

    Bytes setup(0x100, 0);
    setup[0] = 'M';
    setup[1] = 'Z';
    const std::uint32_t table = 0x40;
    std::memcpy(setup.data() + 0x30, "Inno", 4);
    put32(setup, 0x34, table);
    put32(setup, 0x38, ~table);
    const std::uint8_t magic[12] = {'r', 'D', 'l', 'P', 't', 'S', '0', '7', 0x87, 'e', 'V', 'x'};
    std::memcpy(setup.data() + table, magic, 12);
    const std::uint32_t headerAt = 0x100;
    Bytes id(64, 0);
    const std::string idText = "Inno Setup Setup Data (5.5.7) (u)";
    std::memcpy(id.data(), idText.data(), idText.size());
    setup.insert(setup.end(), id.begin(), id.end());
    const Bytes b1 = block(h), b2 = block(locations);
    setup.insert(setup.end(), b1.begin(), b1.end());
    setup.insert(setup.end(), b2.begin(), b2.end());
    const std::uint32_t dataAt = static_cast<std::uint32_t>(setup.size());
    setup.insert(setup.end(), {'z', 'l', 'b', 0x1a});
    setup.insert(setup.end(), chunk.begin(), chunk.end());
    // magic, total size, exe offset, exe size, exe CRC, header, data
    put32(setup, table + 12, static_cast<std::uint32_t>(setup.size()));
    put32(setup, table + 28, headerAt);
    put32(setup, table + 32, dataAt);
    return setup;
}

void testSafePaths() {
    CHECK(data::safeRelativePath("IMGS\\TERRAINS.DBI") == "IMGS/TERRAINS.DBI");
    CHECK(data::safeRelativePath("a/b/c") == "a/b/c");
    CHECK(data::safeRelativePath("../evil").empty());
    CHECK(data::safeRelativePath("a\\..\\..\\evil").empty());
    CHECK(data::safeRelativePath("/abs").empty());
    CHECK(data::safeRelativePath("C:\\Windows").empty());
    CHECK(data::safeRelativePath("a//b").empty());
    CHECK(data::safeRelativePath("").empty());
}

void testIso(const fs::path& dir) {
    const Bytes mdb = pattern(5000, 1), pal = pattern(1024, 2), dbi = pattern(70000, 3), exe = pattern(3000, 4);
    const std::vector<IsoFile> files = {
        {"LIBDAY/GLOBALS/GLOBAL.MDB", mdb}, {"LIBDAY/INTERF/PAL.BMP", pal}, {"LIBDAY/IMGS/TERRAINS.DBI", dbi},
        {"LIBDAY/IMGS/EMPTY.DBI", {}},      {"SETUP/LIBDAY.EXE", exe},      {"README.TXT", text("hello")},
    };
    for (const bool joliet : {true, false}) {
        for (const bool raw : {false, true}) {
            Bytes img = buildIso(files, joliet);
            if (raw) img = rawSectors(img);
            const fs::path p = dir / (std::string(joliet ? "j" : "p") + (raw ? "-raw.bin" : ".iso"));
            writeFile(p, img);
            CHECK(IsoImage::probe(p));
            CHECK(data::detectSource(p) == data::SourceKind::DiscImage);
            const IsoImage iso = IsoImage::open(p);
            CHECK(iso.volumeName() == "SYNTH_DISC");
            std::map<std::string, const IsoEntry*> byPath;
            for (const IsoEntry& e : iso.entries()) byPath[e.path] = &e;
            // Joliet keeps the names as written; the primary tree is upper case.
            const std::string terrains = "LIBDAY/IMGS/TERRAINS.DBI";
            CHECK(byPath.count(terrains) == 1);
            CHECK(byPath.count("LIBDAY") == 1 && byPath["LIBDAY"]->directory);
            if (byPath.count(terrains)) CHECK(iso.read(*byPath[terrains]) == dbi);
            if (byPath.count("LIBDAY/IMGS/EMPTY.DBI")) CHECK(iso.read(*byPath["LIBDAY/IMGS/EMPTY.DBI"]).empty());

            const data::GameSource src = data::inspectSource(p);
            CHECK(src.kind == data::SourceKind::DiscImage);
            std::map<std::string, std::uint64_t> listed;
            for (const data::SourceFile& f : src.files) listed[f.path] = f.size;
            CHECK(listed.size() == 5);              // the game folder plus the program found elsewhere
            CHECK(listed["EXE/LIBDAY.EXE"] == 3000); // moved into EXE
            CHECK(listed.count("README.TXT") == 0);  // outside the game folder

            const fs::path game = dir / ("game-" + p.filename().string());
            data::installFromSource(p, game);
            CHECK(readFile(game / "GLOBALS" / "GLOBAL.MDB") == mdb);
            CHECK(readFile(game / "INTERF" / "PAL.BMP") == pal);
            CHECK(readFile(game / "IMGS" / "TERRAINS.DBI") == dbi);
            CHECK(fs::exists(game / "IMGS" / "EMPTY.DBI") && fs::file_size(game / "IMGS" / "EMPTY.DBI") == 0);
            CHECK(readFile(game / "EXE" / "LIBDAY.EXE") == exe);
            CHECK(!fs::exists(dir / ("game-" + p.filename().string() + ".partial")));
            // A second copy into the same, now filled folder is refused.
            CHECK(throws([&] { data::installFromSource(p, game); }));
        }
    }
    // A disc without the game.
    const fs::path other = dir / "other.iso";
    writeFile(other, buildIso({{"DATA/FILE.TXT", text("x")}}, true));
    CHECK(throws([&] { data::inspectSource(other); }));
    // Not a disc image at all.
    const fs::path junk = dir / "junk.bin";
    writeFile(junk, pattern(100000, 9));
    CHECK(!IsoImage::probe(junk));
    CHECK(data::detectSource(junk) == data::SourceKind::Unknown);
    CHECK(throws([&] { data::inspectSource(junk); }));
}

void testInno(const fs::path& dir) {
    const Bytes mdb = pattern(9000, 11), pal = pattern(1024, 12);
    const Bytes ff = pattern(150000, 13);
    const Bytes part1(ff.begin(), ff.begin() + 100000), part2(ff.begin() + 100000, ff.end());
    const Bytes z1 = zlibStored(part1), z2 = zlibStored(part2);
    const std::vector<InnoTestFile> files = {
        {"{app}\\GLOBALS\\GLOBAL.MDB", mdb, "", ""},
        {"{app}\\INTERF\\PAL.BMP", pal, "", ""},
        {"{tmp}\\helper.dll", text("not a game file"), "", ""},
        {"{app}\\0123abcd", z1, "before_install('00112233445566778899aabbccddeeff', 'ANMSUNIT\\ANMSUNIT.FF', 2)",
         "after_install('0123abcd', " + std::to_string(z1.size()) + ", " + std::to_string(part1.size()) + ")"},
        {"{app}\\4567cdef", z2, "", "after_install('4567cdef', " + std::to_string(z2.size()) + ", " + std::to_string(part2.size()) + ")"},
    };

    // As a GOG installer: the two parts form one file.
    const fs::path gog = dir / "setup_synthetic.exe";
    writeFile(gog, buildInno(files, "GOG.com"));
    CHECK(InnoSetup::probe(gog));
    CHECK(data::detectSource(gog) == data::SourceKind::Installer);
    const InnoSetup setup = InnoSetup::open(gog);
    CHECK(setup.version() == "5.5.7 (unicode)");
    CHECK(setup.appName() == "Synthetic Game");
    CHECK(setup.files().size() == 4);
    if (setup.files().size() == 4) {
        CHECK(setup.files()[3].destination == "{app}\\ANMSUNIT\\ANMSUNIT.FF");
        CHECK(setup.files()[3].locations.size() == 2);
        CHECK(setup.files()[3].size == ff.size());
    }
    const fs::path game = dir / "game-inno";
    data::installFromSource(gog, game);
    CHECK(readFile(game / "GLOBALS" / "GLOBAL.MDB") == mdb);
    CHECK(readFile(game / "INTERF" / "PAL.BMP") == pal);
    CHECK(readFile(game / "ANMSUNIT" / "ANMSUNIT.FF") == ff);
    CHECK(!fs::exists(game / "helper.dll") && !fs::exists(game / "0123abcd"));

    // Without the GOG publisher the parts stay separate files.
    const fs::path plain = dir / "setup_plain.exe";
    writeFile(plain, buildInno(files, "Somebody"));
    CHECK(InnoSetup::open(plain).files().size() == 5);

    // A damaged file fails its checksum.
    Bytes bad = buildInno(files, "GOG.com");
    bad[bad.size() - 10] ^= 0xFF;
    const fs::path damaged = dir / "setup_damaged.exe";
    writeFile(damaged, bad);
    CHECK(throws([&] { data::installFromSource(damaged, dir / "game-damaged"); }));
    CHECK(!fs::exists(dir / "game-damaged.partial"));

    // A damaged header block fails its CRC.
    Bytes badHeader = buildInno(files, "GOG.com");
    badHeader[0x100 + 64 + 9 + 4 + 20] ^= 0xFF;
    const fs::path badHead = dir / "setup_badheader.exe";
    writeFile(badHead, badHeader);
    CHECK(throws([&] { InnoSetup::open(badHead); }));

    // An installer of something else.
    const fs::path notGame = dir / "setup_other.exe";
    writeFile(notGame, buildInno({{"{app}\\readme.txt", text("x"), "", ""}}, "GOG.com"));
    CHECK(throws([&] { data::inspectSource(notGame); }));
}

} // namespace

int main() {
    const fs::path dir = fs::temp_directory_path() / "openday_install_test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    try {
        testSafePaths();
        testIso(dir);
        testInno(dir);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "unexpected exception: %s\n", e.what());
        ++failures;
    }
    fs::remove_all(dir, ec);
    if (failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("install_test: all checks passed\n");
    return 0;
}
