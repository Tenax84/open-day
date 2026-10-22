// Minimal read-only SQLite access for databases produced by
// tools/mdb/mdb2sqlite.py.
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>

struct sqlite3;
struct sqlite3_stmt;

namespace openday::data {

struct DbError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// One result row; valid only inside the forEach callback.
class Row {
public:
    explicit Row(sqlite3_stmt* stmt) : stmt_(stmt) {}
    bool isNull(int column) const;
    std::int64_t integer(int column) const;
    double real(int column) const;
    std::string text(int column) const;

private:
    sqlite3_stmt* stmt_;
};

class SqliteDb {
public:
    enum class Mode { ReadOnly, ReadWriteCreate };

    explicit SqliteDb(const std::filesystem::path& path, Mode mode = Mode::ReadOnly);
    ~SqliteDb();
    SqliteDb(const SqliteDb&) = delete;
    SqliteDb& operator=(const SqliteDb&) = delete;
    SqliteDb(SqliteDb&& other) noexcept;
    SqliteDb& operator=(SqliteDb&& other) noexcept;

    // Runs a statement without results (used to build test fixtures).
    void exec(const std::string& sql);
    // Runs a query and calls fn for every row.
    void forEach(const std::string& sql, const std::function<void(const Row&)>& fn) const;
    bool hasTable(const std::string& name) const;

private:
    sqlite3* db_ = nullptr;
};

// Quotes an identifier for SQL ("Terrain Types" -> "\"Terrain Types\"").
std::string quoteIdent(const std::string& name);

} // namespace openday::data
