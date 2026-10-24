// Read-only reader for the game's Jet 3 (Access 97) databases.
// See docs/formats/MDB.md ("Jet 3 layout summary"). It is a C++ port of the
// reader in tools/mdb/mdb2sqlite.py and decodes every value exactly as that
// tool does, so both converters produce the same SQLite content.
#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace openday::formats::jet {

struct JetError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

constexpr std::size_t kPageSize = 2048;

enum ColumnType : std::uint8_t {
    Bool = 1,
    Byte = 2,
    Int = 3,
    Long = 4,
    Money = 5,
    Float = 6,
    Double = 7,
    DateTime = 8,
    Binary = 9,
    Text = 10,
    Ole = 11,
    Memo = 12,
    Guid = 15,
    Numeric = 16,
};

// Jet type name as written to `_mdb_columns.jet_type` ("TYPE<n>" if unknown).
std::string jetTypeName(std::uint8_t type);
// Declared SQLite column type for a Jet type (INTEGER, REAL, TEXT or BLOB).
const char* sqliteTypeName(std::uint8_t type);

struct Column {
    std::string name; // UTF-8
    std::uint8_t type = 0;
    std::uint16_t number = 0;
    std::uint16_t varIndex = 0;
    std::uint8_t flags = 0;
    std::uint16_t fixedOffset = 0;
    std::uint16_t length = 0;

    bool isFixed() const { return (flags & 0x01) != 0; }
};

struct TableDef {
    std::uint32_t rowCount = 0;
    std::vector<Column> columns; // sorted by column number
    bool hasVar = true;          // rows carry a variable-column section
};

// One decoded value. Text is UTF-8 (converted from Windows-1252).
struct Value {
    enum class Kind { Null, Integer, Real, Text, Blob };
    Kind kind = Kind::Null;
    std::int64_t integer = 0;
    double real = 0;
    std::string text;
    std::vector<std::uint8_t> blob;

    static Value makeInt(std::int64_t v) { Value x; x.kind = Kind::Integer; x.integer = v; return x; }
    static Value makeReal(double v) { Value x; x.kind = Kind::Real; x.real = v; return x; }
};

struct TableRef {
    std::string name;
    std::uint32_t tdefPage = 0;
};

struct LinkRef {
    std::string name;
    std::string foreignName;
};

struct Table {
    TableDef def;
    std::vector<std::vector<Value>> rows;
};

class Database {
public:
    // Keeps a reference to `data`; the bytes must outlive the Database.
    explicit Database(std::span<const std::uint8_t> data);

    TableDef tableDef(std::uint32_t page) const;
    // Raw bytes of every live row of the table whose definition is at `page`.
    std::vector<std::vector<std::uint8_t>> rawRows(std::uint32_t page) const;
    std::vector<Value> decodeRow(std::span<const std::uint8_t> raw, const TableDef& def) const;

    // User tables (sorted by lower-case name) and linked tables (catalog
    // order) of the "Tables" container.
    void tables(std::vector<TableRef>& user, std::vector<LinkRef>& linked) const;
    Table readTable(std::uint32_t page) const;

private:
    std::span<const std::uint8_t> data_;
    std::size_t numPages_ = 0;
    // Data pages by owning table-definition page, filled on first use.
    mutable std::map<std::uint32_t, std::vector<std::uint32_t>> owners_;
    mutable bool ownersScanned_ = false;

    std::span<const std::uint8_t> page(std::uint32_t n) const;
    std::vector<std::uint8_t> readRowPointer(std::uint32_t ptr) const;
    std::vector<std::uint8_t> longValue(std::span<const std::uint8_t> field) const;
    const std::vector<std::uint32_t>& pagesOwnedBy(std::uint32_t owner) const;
};

// Windows-1252 -> UTF-8. Bytes without a mapping (0x81, 0x8D, 0x8F, 0x90,
// 0x9D) become U+FFFD, like Python's "replace" error handler.
std::string cp1252ToUtf8(std::span<const std::uint8_t> bytes);

} // namespace openday::formats::jet
