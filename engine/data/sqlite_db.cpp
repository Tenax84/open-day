#include "data/sqlite_db.hpp"

#include <sqlite3.h>

#include <utility>

namespace openday::data {

bool Row::isNull(int column) const { return sqlite3_column_type(stmt_, column) == SQLITE_NULL; }

std::int64_t Row::integer(int column) const { return sqlite3_column_int64(stmt_, column); }

double Row::real(int column) const { return sqlite3_column_double(stmt_, column); }

std::string Row::text(int column) const {
    const unsigned char* t = sqlite3_column_text(stmt_, column);
    return t ? std::string(reinterpret_cast<const char*>(t), static_cast<std::size_t>(sqlite3_column_bytes(stmt_, column)))
             : std::string();
}

std::vector<std::uint8_t> Row::blob(int column) const {
    const auto* b = static_cast<const std::uint8_t*>(sqlite3_column_blob(stmt_, column));
    const int n = sqlite3_column_bytes(stmt_, column);
    return b && n > 0 ? std::vector<std::uint8_t>(b, b + n) : std::vector<std::uint8_t>();
}

SqliteDb::SqliteDb(const std::filesystem::path& path, Mode mode) {
    const int flags = mode == Mode::ReadOnly ? SQLITE_OPEN_READONLY : (SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE);
    const std::u8string u8 = path.u8string();
    const std::string utf8(u8.begin(), u8.end());
    if (sqlite3_open_v2(utf8.c_str(), &db_, flags, nullptr) != SQLITE_OK) {
        const std::string msg = db_ ? sqlite3_errmsg(db_) : "out of memory";
        sqlite3_close(db_);
        db_ = nullptr;
        throw DbError("cannot open " + utf8 + ": " + msg);
    }
}

SqliteDb::~SqliteDb() { sqlite3_close(db_); }

SqliteDb::SqliteDb(SqliteDb&& other) noexcept : db_(std::exchange(other.db_, nullptr)) {}

SqliteDb& SqliteDb::operator=(SqliteDb&& other) noexcept {
    if (this != &other) {
        sqlite3_close(db_);
        db_ = std::exchange(other.db_, nullptr);
    }
    return *this;
}

void SqliteDb::exec(const std::string& sql) {
    char* err = nullptr;
    if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err) != SQLITE_OK) {
        const std::string msg = err ? err : "unknown error";
        sqlite3_free(err);
        throw DbError(msg + " in: " + sql);
    }
}

void SqliteDb::forEach(const std::string& sql, const std::function<void(const Row&)>& fn) const {
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
        throw DbError(std::string(sqlite3_errmsg(db_)) + " in: " + sql);
    int rc;
    const Row row(stmt);
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) fn(row);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) throw DbError(std::string(sqlite3_errmsg(db_)) + " in: " + sql);
}

bool SqliteDb::hasTable(const std::string& name) const {
    bool found = false;
    std::string escaped;
    for (char c : name) {
        escaped += c;
        if (c == '\'') escaped += '\'';
    }
    forEach("SELECT 1 FROM sqlite_master WHERE type = 'table' AND name = '" + escaped + "' COLLATE NOCASE",
            [&](const Row&) { found = true; });
    return found;
}

std::string quoteIdent(const std::string& name) {
    std::string q = "\"";
    for (char c : name) {
        q += c;
        if (c == '"') q += '"';
    }
    return q + "\"";
}

} // namespace openday::data
