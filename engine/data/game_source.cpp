#include "data/game_source.hpp"

#include <algorithm>
#include <fstream>
#include <map>
#include <optional>
#include <system_error>

#include "formats/game_path.hpp"
#include "formats/inno_setup.hpp"
#include "formats/iso9660.hpp"

namespace openday::data {
namespace {

namespace fs = std::filesystem;

std::string lower(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

bool startsWithIgnoreCase(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() && lower(s.substr(0, prefix.size())) == lower(prefix);
}

fs::path utf8Path(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }

std::string utf8(const fs::path& p) {
    const std::u8string u = p.u8string();
    return std::string(u.begin(), u.end());
}

// Files every game folder has (setup.cpp validGameDir checks the same).
constexpr const char* kMarkers[] = {"globals/global.mdb", "interf/pal.bmp"};

bool hasMarkers(const std::vector<SourceFile>& files) {
    for (const char* m : kMarkers) {
        const bool found = std::any_of(files.begin(), files.end(), [&](const SourceFile& f) { return lower(f.path) == m; });
        if (!found) return false;
    }
    return true;
}

// Where each game file comes from in the medium.
struct Plan {
    GameSource info;
    std::vector<std::size_t> origin; // index into the Inno files or the ISO entries, per info.files
};

Plan planInstaller(const formats::InnoSetup& setup) {
    Plan plan;
    plan.info.kind = SourceKind::Installer;
    plan.info.description = "Inno Setup " + setup.version() + " installer" +
                            (setup.appName().empty() ? std::string() : " of \"" + setup.appName() + "\"");
    std::map<std::string, std::size_t> seen; // a later entry for the same file replaces an earlier one
    for (std::size_t i = 0; i < setup.files().size(); ++i) {
        const formats::InnoFile& f = setup.files()[i];
        if (!startsWithIgnoreCase(f.destination, "{app}\\")) continue;
        const std::string rel = safeRelativePath(f.destination.substr(6));
        if (rel.empty()) continue;
        if (const auto it = seen.find(lower(rel)); it != seen.end()) {
            plan.info.files[it->second] = {rel, f.size};
            plan.origin[it->second] = i;
            continue;
        }
        seen[lower(rel)] = plan.info.files.size();
        plan.info.files.push_back({rel, f.size});
        plan.origin.push_back(i);
    }
    if (!hasMarkers(plan.info.files))
        throw SourceError("this installer does not contain the game files (GLOBALS\\GLOBAL.MDB, INTERF\\PAL.BMP)");
    return plan;
}

Plan planDisc(const formats::IsoImage& iso) {
    const auto& entries = iso.entries();
    // The game folder: the shallowest folder with GLOBALS/GLOBAL.MDB and
    // INTERF/PAL.BMP below it.
    std::optional<std::string> root;
    for (const formats::IsoEntry& e : entries) {
        const std::string l = lower(e.path);
        const std::string marker = kMarkers[0];
        if (e.directory || !(l == marker || (l.size() > marker.size() && l.ends_with("/" + marker)))) continue;
        const std::string prefix = e.path.substr(0, e.path.size() - marker.size());
        const bool hasPalette = std::any_of(entries.begin(), entries.end(), [&](const formats::IsoEntry& x) {
            return lower(x.path) == lower(prefix) + kMarkers[1];
        });
        if (hasPalette && (!root || prefix.size() < root->size())) root = prefix;
    }
    if (!root)
        throw SourceError("this disc image does not contain the game folders (GLOBALS, INTERF, IMGS); if the CD "
                          "holds only a compressed setup, install the game on Windows and use that folder");

    Plan plan;
    plan.info.kind = SourceKind::DiscImage;
    plan.info.description = "disc image" + (iso.volumeName().empty() ? std::string() : " \"" + iso.volumeName() + "\"");
    bool haveExe = false;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const formats::IsoEntry& e = entries[i];
        if (e.directory || !startsWithIgnoreCase(e.path, *root)) continue;
        const std::string rel = safeRelativePath(e.path.substr(root->size()));
        if (rel.empty()) continue;
        if (lower(rel) == "exe/libday.exe") haveExe = true;
        plan.info.files.push_back({rel, e.size});
        plan.origin.push_back(i);
    }
    // The program and its language library may sit elsewhere on the disc.
    if (!haveExe) {
        for (std::size_t i = 0; i < entries.size(); ++i) {
            const formats::IsoEntry& e = entries[i];
            if (e.directory) continue;
            const std::string name = lower(e.path.substr(e.path.rfind('/') + 1));
            const bool program = name == "libday.exe";
            const bool language = name.starts_with("libday") && name.ends_with(".dll");
            if (!program && !language) continue;
            plan.info.files.push_back({"EXE/" + e.path.substr(e.path.rfind('/') + 1), e.size});
            plan.origin.push_back(i);
        }
    }
    return plan;
}

// Writes pieces into the files of the partial folder.
class Writer {
public:
    explicit Writer(const fs::path& dir) : dir_(dir) {}

    void create(const std::string& rel) {
        const fs::path p = dir_ / utf8Path(rel);
        std::error_code ec;
        fs::create_directories(p.parent_path(), ec);
        std::ofstream out(p, std::ios::binary | std::ios::trunc);
        if (!out) throw SourceError("cannot create " + utf8(p));
    }

    void write(const std::string& rel, std::uint64_t offset, const std::uint8_t* data, std::size_t n) {
        if (rel != current_) {
            file_.close();
            file_.clear();
            file_.open(dir_ / utf8Path(rel), std::ios::binary | std::ios::in | std::ios::out);
            if (!file_) throw SourceError("cannot write " + rel);
            current_ = rel;
        }
        file_.seekp(static_cast<std::streamoff>(offset));
        file_.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(n));
        if (!file_) throw SourceError("cannot write " + rel + " (disk full?)");
    }

    void close() {
        file_.close();
        current_.clear();
    }

private:
    fs::path dir_;
    std::fstream file_;
    std::string current_;
};

} // namespace

std::string safeRelativePath(const std::string& path) {
    std::string out;
    std::size_t pos = 0;
    if (path.empty() || path[0] == '/' || path[0] == '\\' || path.find(':') != std::string::npos) return {};
    while (pos <= path.size()) {
        std::size_t end = path.find_first_of("/\\", pos);
        if (end == std::string::npos) end = path.size();
        const std::string part = path.substr(pos, end - pos);
        pos = end + 1;
        if (part.empty() || part == "." || part == "..") return {};
        if (!out.empty()) out += '/';
        out += part;
    }
    return out;
}

SourceKind detectSource(const fs::path& file) {
    std::error_code ec;
    if (!fs::is_regular_file(file, ec)) return SourceKind::Unknown;
    if (formats::IsoImage::probe(file)) return SourceKind::DiscImage;
    if (formats::InnoSetup::probe(file)) return SourceKind::Installer;
    return SourceKind::Unknown;
}

GameSource inspectSource(const fs::path& file) {
    try {
        switch (detectSource(file)) {
        case SourceKind::Installer: return planInstaller(formats::InnoSetup::open(file)).info;
        case SourceKind::DiscImage: return planDisc(formats::IsoImage::open(file)).info;
        case SourceKind::Unknown: break;
        }
    } catch (const SourceError&) {
        throw;
    } catch (const std::exception& e) {
        throw SourceError(e.what());
    }
    throw SourceError("not a GOG installer (Inno Setup) or a CD image: " + utf8(file.filename()));
}

void installFromSource(const fs::path& file, const fs::path& gameDir, const InstallProgress& progress) {
    std::error_code ec;
    if (fs::exists(gameDir, ec) && !fs::is_empty(gameDir, ec))
        throw SourceError("the target folder is not empty: " + utf8(gameDir));
    fs::path partial = gameDir;
    partial += ".partial";
    fs::remove_all(partial, ec);
    fs::create_directories(partial, ec);
    if (ec) throw SourceError("cannot create " + utf8(partial) + ": " + ec.message());

    try {
        Writer writer(partial);
        const SourceKind kind = detectSource(file);
        if (kind == SourceKind::Installer) {
            const formats::InnoSetup setup = formats::InnoSetup::open(file);
            const Plan plan = planInstaller(setup);
            std::map<std::size_t, std::string> target; // Inno file index -> relative path
            for (std::size_t i = 0; i < plan.info.files.size(); ++i) {
                writer.create(plan.info.files[i].path);
                target[plan.origin[i]] = plan.info.files[i].path;
            }
            std::vector<std::size_t> wanted;
            for (const auto& [index, rel] : target) wanted.push_back(index);
            std::string lastName;
            setup.extract(
                wanted,
                [&](std::size_t f, std::uint64_t off, const std::uint8_t* d, std::size_t n) {
                    lastName = target.at(f);
                    writer.write(lastName, off, d, n);
                },
                [&](std::uint64_t done, std::uint64_t total) {
                    if (progress) progress(done, total, lastName);
                });
        } else if (kind == SourceKind::DiscImage) {
            const formats::IsoImage iso = formats::IsoImage::open(file);
            const Plan plan = planDisc(iso);
            std::uint64_t total = 0, done = 0;
            for (const SourceFile& f : plan.info.files) total += f.size;
            for (std::size_t i = 0; i < plan.info.files.size(); ++i) {
                const std::string& rel = plan.info.files[i].path;
                writer.create(rel);
                std::uint64_t at = 0;
                iso.read(iso.entries()[plan.origin[i]], [&](const std::uint8_t* d, std::size_t n) {
                    writer.write(rel, at, d, n);
                    at += n;
                    done += n;
                    if (progress) progress(done, total, rel);
                });
            }
        } else {
            throw SourceError("not a GOG installer (Inno Setup) or a CD image: " + utf8(file.filename()));
        }
        writer.close();
    } catch (const SourceError&) {
        fs::remove_all(partial, ec);
        throw;
    } catch (const std::exception& e) {
        fs::remove_all(partial, ec);
        throw SourceError(e.what());
    }

    fs::remove(gameDir, ec); // an empty folder
    fs::rename(partial, gameDir, ec);
    if (ec) throw SourceError("cannot rename " + utf8(partial) + ": " + ec.message());
}

} // namespace openday::data
