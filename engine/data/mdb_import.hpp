// Converts the game's Jet 3 databases to SQLite (docs/formats/MDB.md).
//
// The output is the same as that of tools/mdb/mdb2sqlite.py: one .sqlite
// file per database with every user table under its original name and
// column names, plus `_mdb_columns` (original Jet column types) and
// `_mdb_links` (tables linked from another database). The game directory
// is only read, never written.
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace openday::formats {
class ZipArchive;
}

namespace openday::data {

// Bumped whenever the converter's output changes; a data directory with an
// older stamp is converted again.
constexpr int kImportVersion = 1;
// Name of the stamp file written into the output directory after a
// successful conversion.
constexpr const char* kImportStampFile = "openday-data.stamp";

struct TableSummary {
    std::string name;
    std::size_t rows = 0;
};

// Converts one database (file contents) into `out` (replaced atomically
// via out + ".tmp"). Throws formats::jet::JetError or DbError.
std::vector<TableSummary> convertMdb(std::span<const std::uint8_t> mdb, const std::filesystem::path& out);

// The databases of an installation: the .mdb files of GLOBALS and SCENS and
// the .mdb members of SCENS\SCEN1.ZIP, SCEN2.ZIP, DEATHM.ZIP and
// COLONIES\COLONIES.ZIP. Folder and file names are matched
// case-insensitively. Each database has an output stem such as
// "GLOBALS/GLOBAL" or "SCENS/SCEN1/s1-1-1" ('/'-separated).
class InstallDatabases {
public:
    explicit InstallDatabases(const std::filesystem::path& gameDir);
    ~InstallDatabases();
    InstallDatabases(InstallDatabases&&) noexcept;
    InstallDatabases& operator=(InstallDatabases&&) noexcept;

    std::size_t size() const { return items_.size(); }
    const std::string& stem(std::size_t i) const { return items_[i].stem; }
    std::vector<std::uint8_t> load(std::size_t i) const;

private:
    struct Item {
        std::string stem;
        std::filesystem::path file; // plain file, or
        int archive = -1;           // index into archives_
        std::size_t entry = 0;
    };
    std::vector<Item> items_;
    std::vector<std::unique_ptr<formats::ZipArchive>> archives_;
};

// Describes the source files (names and sizes) so that a changed or
// different installation is detected. Empty if no database file was found.
std::string sourceFingerprint(const std::filesystem::path& gameDir);

// True if `outDir` holds a complete conversion of this installation made
// by the current converter version.
bool isImportCurrent(const std::filesystem::path& gameDir, const std::filesystem::path& outDir);

struct ImportReport {
    std::size_t converted = 0;
    std::vector<std::string> failures; // "stem: reason"
};

// Converts every database of the installation into outDir/<stem>.sqlite
// and, if all succeeded, writes the stamp file. `progress` (optional) is
// called before each database with (index, total, stem) and once at the end
// with (total, total, ""). Throws if outDir lies inside gameDir.
ImportReport importInstallation(const std::filesystem::path& gameDir, const std::filesystem::path& outDir,
                                const std::function<void(std::size_t, std::size_t, const std::string&)>& progress = {});

} // namespace openday::data
