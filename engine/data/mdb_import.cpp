#include "data/mdb_import.hpp"

#include <sqlite3.h>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <sstream>
#include <system_error>
#include <utility>

#include "data/sqlite_db.hpp"
#include "formats/game_path.hpp"
#include "formats/mdb.hpp"
#include "formats/zip.hpp"

namespace openday::data {
namespace fs = std::filesystem;
namespace jet = formats::jet;

namespace {

std::string utf8(const fs::path& p) {
    const std::u8string u = p.u8string();
    return std::string(u.begin(), u.end());
}

fs::path fromUtf8(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }

bool endsWithIgnoreCase(const std::string& s, const char* suffix) {
    const std::string_view suf(suffix);
    return s.size() >= suf.size() && formats::sameNameIgnoreCase(std::string_view(s).substr(s.size() - suf.size()), suf);
}

// The extension is everything from the last '.' (os.path.splitext).
std::string dropExtension(const std::string& name) {
    const auto dot = name.rfind('.');
    return dot == std::string::npos || dot == 0 ? name : name.substr(0, dot);
}

// Finds a direct child of `parent` by name, ignoring case.
fs::path findChild(const fs::path& parent, const char* name) {
    if (parent.empty()) return {};
    const fs::path p = formats::findGameFile(parent, name);
    std::error_code ec;
    return fs::exists(p, ec) ? p : fs::path();
}

// Sorted names of the .mdb files directly in `dir`.
std::vector<std::string> mdbFiles(const fs::path& dir) {
    std::vector<std::string> out;
    std::error_code ec;
    if (dir.empty() || !fs::is_directory(dir, ec)) return out;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string name = utf8(it->path().filename());
        if (endsWithIgnoreCase(name, ".mdb")) out.push_back(name);
    }
    std::sort(out.begin(), out.end());
    return out;
}

constexpr const char* kFolders[] = {"GLOBALS", "SCENS"};
constexpr std::pair<const char*, const char*> kArchives[] = {
    {"SCENS", "SCEN1.ZIP"}, {"SCENS", "SCEN2.ZIP"}, {"SCENS", "DEATHM.ZIP"}, {"COLONIES", "COLONIES.ZIP"}};

struct Statement {
    sqlite3_stmt* stmt = nullptr;
    Statement(sqlite3* db, const std::string& sql) {
        if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
            throw DbError(std::string(sqlite3_errmsg(db)) + " in: " + sql);
    }
    ~Statement() { sqlite3_finalize(stmt); }
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    void bindText(int i, const std::string& s) { sqlite3_bind_text(stmt, i, s.data(), static_cast<int>(s.size()), SQLITE_TRANSIENT); }
    void bindInt(int i, std::int64_t v) { sqlite3_bind_int64(stmt, i, v); }
    void bind(int i, const jet::Value& v) {
        switch (v.kind) {
        case jet::Value::Kind::Null: sqlite3_bind_null(stmt, i); break;
        case jet::Value::Kind::Integer: sqlite3_bind_int64(stmt, i, v.integer); break;
        case jet::Value::Kind::Real: sqlite3_bind_double(stmt, i, v.real); break;
        case jet::Value::Kind::Text: bindText(i, v.text); break;
        case jet::Value::Kind::Blob:
            // An empty blob must stay an empty blob, not NULL.
            if (v.blob.empty()) sqlite3_bind_zeroblob(stmt, i, 0);
            else sqlite3_bind_blob(stmt, i, v.blob.data(), static_cast<int>(v.blob.size()), SQLITE_TRANSIENT);
            break;
        }
    }
    void step(sqlite3* db) {
        if (sqlite3_step(stmt) != SQLITE_DONE) throw DbError(sqlite3_errmsg(db));
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);
    }
};

void exec(sqlite3* db, const std::string& sql) {
    char* err = nullptr;
    if (sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err) != SQLITE_OK) {
        const std::string msg = err ? err : "unknown error";
        sqlite3_free(err);
        throw DbError(msg + " in: " + sql);
    }
}

std::string readStamp(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    return in ? std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>()) : std::string();
}

std::string stampText(const std::string& fingerprint) {
    return "openday-data " + std::to_string(kImportVersion) + "\n" + fingerprint;
}

} // namespace

std::vector<TableSummary> convertMdb(std::span<const std::uint8_t> mdb, const fs::path& out) {
    const jet::Database db(mdb);
    std::vector<jet::TableRef> user;
    std::vector<jet::LinkRef> linked;
    db.tables(user, linked);

    fs::path tmp = out;
    tmp += ".tmp";
    std::error_code ec;
    fs::remove(tmp, ec);
    std::vector<TableSummary> summary;
    {
        sqlite3* con = nullptr;
        if (sqlite3_open_v2(utf8(tmp).c_str(), &con, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK) {
            sqlite3_close(con);
            throw DbError("cannot open " + utf8(tmp));
        }
        struct Closer {
            sqlite3* c;
            ~Closer() { sqlite3_close(c); }
        } closer{con};
        // A temporary file that is renamed when complete: no journal needed.
        exec(con, "PRAGMA journal_mode = OFF");
        exec(con, "PRAGMA synchronous = OFF");
        exec(con, "BEGIN");
        exec(con, "CREATE TABLE _mdb_columns (table_name TEXT, position INTEGER, column_name TEXT, jet_type TEXT, jet_length INTEGER)");
        exec(con, "CREATE TABLE _mdb_links (table_name TEXT, foreign_name TEXT)");
        {
            Statement ins(con, "INSERT INTO _mdb_links VALUES (?, ?)");
            for (const jet::LinkRef& l : linked) {
                ins.bindText(1, l.name);
                ins.bindText(2, l.foreignName);
                ins.step(con);
            }
        }
        Statement colIns(con, "INSERT INTO _mdb_columns VALUES (?, ?, ?, ?, ?)");
        for (const jet::TableRef& ref : user) {
            const jet::Table t = db.readTable(ref.tdefPage);
            std::string create = "CREATE TABLE " + quoteIdent(ref.name) + " (";
            std::string insert = "INSERT INTO " + quoteIdent(ref.name) + " VALUES (";
            for (std::size_t i = 0; i < t.def.columns.size(); ++i) {
                const jet::Column& c = t.def.columns[i];
                create += (i ? ", " : "") + quoteIdent(c.name) + " " + jet::sqliteTypeName(c.type);
                insert += i ? ",?" : "?";
            }
            exec(con, create + ")");
            for (std::size_t i = 0; i < t.def.columns.size(); ++i) {
                const jet::Column& c = t.def.columns[i];
                colIns.bindText(1, ref.name);
                colIns.bindInt(2, static_cast<std::int64_t>(i));
                colIns.bindText(3, c.name);
                colIns.bindText(4, jet::jetTypeName(c.type));
                colIns.bindInt(5, c.length);
                colIns.step(con);
            }
            if (!t.rows.empty()) {
                Statement ins(con, insert + ")");
                for (const std::vector<jet::Value>& row : t.rows) {
                    for (std::size_t i = 0; i < row.size(); ++i) ins.bind(static_cast<int>(i + 1), row[i]);
                    ins.step(con);
                }
            }
            summary.push_back({ref.name, t.rows.size()});
        }
        exec(con, "COMMIT");
    }
    fs::rename(tmp, out); // replaces an existing file
    return summary;
}

InstallDatabases::InstallDatabases(const fs::path& gameDir) {
    for (const char* folder : kFolders) {
        const fs::path dir = findChild(gameDir, folder);
        for (const std::string& name : mdbFiles(dir)) {
            Item it;
            it.stem = std::string(folder) + "/" + dropExtension(name);
            it.file = dir / fromUtf8(name);
            items_.push_back(std::move(it));
        }
    }
    for (const auto& [folder, archive] : kArchives) {
        const fs::path zip = findChild(findChild(gameDir, folder), archive);
        if (zip.empty()) continue;
        archives_.push_back(std::make_unique<formats::ZipArchive>(formats::ZipArchive::open(zip)));
        const auto& entries = archives_.back()->entries();
        std::vector<std::size_t> order(entries.size());
        for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
        auto lower = [](std::string s) {
            for (char& c : s)
                if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
            return s;
        };
        std::stable_sort(order.begin(), order.end(),
                         [&](std::size_t a, std::size_t b) { return lower(entries[a].name) < lower(entries[b].name); });
        for (const std::size_t i : order) {
            const std::string& full = entries[i].name;
            const auto slash = full.find_last_of("/\\");
            const std::string base = slash == std::string::npos ? full : full.substr(slash + 1);
            if (!endsWithIgnoreCase(base, ".mdb")) continue;
            Item it;
            it.stem = std::string(folder) + "/" + dropExtension(archive) + "/" + dropExtension(base);
            it.archive = static_cast<int>(archives_.size() - 1);
            it.entry = i;
            items_.push_back(std::move(it));
        }
    }
}

InstallDatabases::~InstallDatabases() = default;
InstallDatabases::InstallDatabases(InstallDatabases&&) noexcept = default;
InstallDatabases& InstallDatabases::operator=(InstallDatabases&&) noexcept = default;

std::vector<std::uint8_t> InstallDatabases::load(std::size_t i) const {
    const Item& it = items_[i];
    if (it.archive >= 0) {
        const formats::ZipArchive& z = *archives_[static_cast<std::size_t>(it.archive)];
        return z.read(z.entries()[it.entry]);
    }
    std::ifstream in(it.file, std::ios::binary);
    if (!in) throw DbError("cannot open " + utf8(it.file));
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

std::string sourceFingerprint(const fs::path& gameDir) {
    std::vector<std::string> lines;
    std::error_code ec;
    auto add = [&](const std::string& rel, const fs::path& p) {
        const auto size = fs::file_size(p, ec);
        lines.push_back(rel + " " + std::to_string(ec ? 0 : size));
    };
    for (const char* folder : kFolders) {
        const fs::path dir = findChild(gameDir, folder);
        for (const std::string& name : mdbFiles(dir)) add(std::string(folder) + "/" + name, dir / fromUtf8(name));
    }
    for (const auto& [folder, archive] : kArchives) {
        const fs::path zip = findChild(findChild(gameDir, folder), archive);
        if (!zip.empty()) add(std::string(folder) + "/" + archive, zip);
    }
    std::string out;
    for (const std::string& l : lines) out += l + "\n";
    return out;
}

bool isImportCurrent(const fs::path& gameDir, const fs::path& outDir) {
    std::error_code ec;
    if (!fs::exists(outDir / "GLOBALS" / "GLOBAL.sqlite", ec)) return false;
    const std::string fp = sourceFingerprint(gameDir);
    return !fp.empty() && readStamp(outDir / kImportStampFile) == stampText(fp);
}

ImportReport importInstallation(const fs::path& gameDir, const fs::path& outDir,
                                const std::function<void(std::size_t, std::size_t, const std::string&)>& progress) {
    std::error_code ec;
    auto normal = [&ec](const fs::path& p) {
        fs::path n = fs::weakly_canonical(fs::absolute(p), ec);
        if (n.has_relative_path() && n.filename().empty()) n = n.parent_path(); // trailing separator
        return n;
    };
    {
        const fs::path game = normal(gameDir), out = normal(outDir);
        auto g = game.begin();
        auto o = out.begin();
        while (g != game.end() && o != out.end() && formats::sameNameIgnoreCase(utf8(*g), utf8(*o))) ++g, ++o;
        if (g == game.end()) throw DbError("refusing to write inside the game directory");
    }

    const std::string fingerprint = sourceFingerprint(gameDir);
    if (fingerprint.empty()) throw DbError("no game databases found in " + utf8(gameDir));
    const InstallDatabases dbs(gameDir);
    fs::remove(outDir / kImportStampFile, ec);

    ImportReport report;
    for (std::size_t i = 0; i < dbs.size(); ++i) {
        if (progress) progress(i, dbs.size(), dbs.stem(i));
        const fs::path target = outDir / fromUtf8(dbs.stem(i) + ".sqlite");
        try {
            fs::create_directories(target.parent_path());
            const std::vector<std::uint8_t> bytes = dbs.load(i);
            convertMdb(bytes, target);
            ++report.converted;
        } catch (const std::exception& e) {
            report.failures.push_back(dbs.stem(i) + ": " + e.what());
        }
    }
    if (progress) progress(dbs.size(), dbs.size(), std::string());
    if (report.failures.empty()) {
        std::ofstream stamp(outDir / kImportStampFile, std::ios::binary);
        stamp << stampText(fingerprint);
        if (!stamp) report.failures.push_back(std::string(kImportStampFile) + ": cannot write");
    }
    return report;
}

} // namespace openday::data
