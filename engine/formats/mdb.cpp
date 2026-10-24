#include "formats/mdb.hpp"

#include <algorithm>
#include <bit>
#include <set>
#include <utility>

namespace openday::formats::jet {
namespace {

using Bytes = std::span<const std::uint8_t>;

constexpr std::uint16_t kRowDeleted = 0x8000;
constexpr std::uint16_t kRowOverflow = 0x4000;
constexpr std::uint16_t kRowOffsetMask = 0x1FFF;
constexpr std::uint32_t kTablesContainer = 0x0F000001;

// The helpers below follow Python's indexing rules (negative indexes count
// from the end, slices clamp) so that odd rows decode exactly as in
// tools/mdb/mdb2sqlite.py.
std::uint8_t at(Bytes b, std::ptrdiff_t i) {
    const auto n = static_cast<std::ptrdiff_t>(b.size());
    if (i < 0) i += n;
    if (i < 0 || i >= n) throw JetError("index out of range");
    return b[static_cast<std::size_t>(i)];
}

Bytes slice(Bytes b, std::ptrdiff_t from, std::ptrdiff_t to) {
    const auto n = static_cast<std::ptrdiff_t>(b.size());
    auto norm = [n](std::ptrdiff_t i) { return i < 0 ? std::max<std::ptrdiff_t>(0, i + n) : std::min(i, n); };
    from = norm(from);
    to = norm(to);
    if (to <= from) return {};
    return b.subspan(static_cast<std::size_t>(from), static_cast<std::size_t>(to - from));
}

std::ptrdiff_t floorDiv(std::ptrdiff_t a, std::ptrdiff_t b) {
    std::ptrdiff_t q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) --q;
    return q;
}

std::uint64_t readLe(Bytes b, std::size_t off, std::size_t n) {
    if (off + n > b.size()) throw JetError("unpack requires a buffer of " + std::to_string(n) + " bytes");
    std::uint64_t v = 0;
    for (std::size_t i = 0; i < n; ++i) v |= static_cast<std::uint64_t>(b[off + i]) << (8 * i);
    return v;
}

std::uint16_t u16(Bytes b, std::size_t off) { return static_cast<std::uint16_t>(readLe(b, off, 2)); }
std::uint32_t u32(Bytes b, std::size_t off) { return static_cast<std::uint32_t>(readLe(b, off, 4)); }

// struct.unpack('<x', v[:n]): the value must have at least n bytes.
std::uint64_t exact(Bytes v, std::size_t n) {
    if (v.size() < n) throw JetError("unpack requires a buffer of " + std::to_string(n) + " bytes");
    return readLe(v, 0, n);
}

struct Bounds {
    std::size_t start = 0, end = 0;
    std::uint16_t flags = 0;
};

Bounds rowBounds(Bytes pg, std::size_t row) {
    const std::uint16_t nrows = u16(pg, 8);
    if (row >= nrows) throw JetError("row " + std::to_string(row) + " not on page");
    const std::uint16_t raw = u16(pg, 10 + row * 2);
    Bounds b;
    b.start = raw & kRowOffsetMask;
    b.end = kPageSize;
    // Rows are stored from the end of the page towards the start: a row
    // ends where the previous non-empty row starts.
    for (std::size_t prev = row; prev-- > 0;) {
        const std::size_t p = u16(pg, 10 + prev * 2) & kRowOffsetMask;
        if (p) {
            b.end = p;
            break;
        }
    }
    b.flags = static_cast<std::uint16_t>(raw & ~kRowOffsetMask);
    return b;
}

std::vector<std::uint8_t> toVector(Bytes b) { return {b.begin(), b.end()}; }

// Variable column offsets (Jet 3: one byte each, stored backwards, plus a
// jump table that adds 256 for every 256-byte boundary).
std::vector<std::ptrdiff_t> varOffsets(Bytes raw, std::ptrdiff_t rowEnd, std::ptrdiff_t maskLen, std::ptrdiff_t varCount) {
    std::ptrdiff_t numJumps = floorDiv(static_cast<std::ptrdiff_t>(raw.size()) - 1, 256);
    const std::ptrdiff_t colPtr = rowEnd - maskLen - numJumps - 1;
    if (floorDiv(colPtr - varCount, 256) < numJumps) --numJumps;
    std::vector<std::ptrdiff_t> offsets;
    std::ptrdiff_t jumps = 0;
    for (std::ptrdiff_t i = 0; i < varCount + 1; ++i) {
        while (jumps < numJumps && i == at(raw, rowEnd - maskLen - jumps - 1)) ++jumps;
        offsets.push_back(at(raw, colPtr - i) + jumps * 256);
    }
    return offsets;
}

const Value* field(const std::vector<Column>& cols, const std::vector<Value>& row, const char* name) {
    // dict(zip(names, values)): the last column of a name wins.
    for (std::size_t i = cols.size(); i-- > 0;)
        if (cols[i].name == name) return &row[i];
    return nullptr;
}

std::int64_t intOr0(const Value* v) { return v && v->kind == Value::Kind::Integer ? v->integer : 0; }
std::string textOrEmpty(const Value* v) { return v && v->kind == Value::Kind::Text ? v->text : std::string(); }

std::string asciiLower(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

} // namespace

std::string jetTypeName(std::uint8_t type) {
    switch (type) {
    case Bool: return "BOOLEAN";
    case Byte: return "BYTE";
    case Int: return "INT16";
    case Long: return "INT32";
    case Money: return "MONEY";
    case Float: return "FLOAT";
    case Double: return "DOUBLE";
    case DateTime: return "DATETIME";
    case Binary: return "BINARY";
    case Text: return "TEXT";
    case Ole: return "OLE";
    case Memo: return "MEMO";
    case Guid: return "GUID";
    case Numeric: return "NUMERIC";
    default: return "TYPE" + std::to_string(type);
    }
}

const char* sqliteTypeName(std::uint8_t type) {
    switch (type) {
    case Bool:
    case Byte:
    case Int:
    case Long: return "INTEGER";
    case Money:
    case Float:
    case Double:
    case DateTime: return "REAL";
    case Text:
    case Memo: return "TEXT";
    default: return "BLOB";
    }
}

std::string cp1252ToUtf8(std::span<const std::uint8_t> bytes) {
    // 0x80..0x9F; 0 = no mapping.
    static constexpr char16_t kHigh[32] = {0x20AC, 0,      0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
                                           0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0,      0x017D, 0,
                                           0,      0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
                                           0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0,      0x017E, 0x0178};
    std::string out;
    out.reserve(bytes.size());
    for (const std::uint8_t b : bytes) {
        char32_t c = b;
        if (b >= 0x80 && b < 0xA0) c = kHigh[b - 0x80] ? kHigh[b - 0x80] : 0xFFFD;
        if (c < 0x80) {
            out += static_cast<char>(c);
        } else if (c < 0x800) {
            out += static_cast<char>(0xC0 | (c >> 6));
            out += static_cast<char>(0x80 | (c & 0x3F));
        } else {
            out += static_cast<char>(0xE0 | (c >> 12));
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (c & 0x3F));
        }
    }
    return out;
}

Database::Database(std::span<const std::uint8_t> data) : data_(data) {
    static constexpr char kMagic[] = "Standard Jet DB";
    if (data.size() < kPageSize || data[0] != 0 || data[1] != 1 || data[2] != 0 || data[3] != 0)
        throw JetError("not a Jet database");
    if (!std::equal(kMagic, kMagic + 15, data.begin() + 4)) throw JetError("not a Jet database");
    if (data[0x14] != 0) throw JetError("not a Jet 3 database (version byte " + std::to_string(data[0x14]) + ")");
    numPages_ = data.size() / kPageSize;
}

std::span<const std::uint8_t> Database::page(std::uint32_t n) const {
    if (n == 0 || n >= numPages_) throw JetError("page " + std::to_string(n) + " out of range");
    return data_.subspan(static_cast<std::size_t>(n) * kPageSize, kPageSize);
}

std::vector<std::uint8_t> Database::readRowPointer(std::uint32_t ptr) const {
    const Bytes pg = page(ptr >> 8);
    const Bounds b = rowBounds(pg, ptr & 0xFF);
    return toVector(slice(pg, static_cast<std::ptrdiff_t>(b.start), static_cast<std::ptrdiff_t>(b.end)));
}

TableDef Database::tableDef(std::uint32_t pgno) const {
    const Bytes first = page(pgno);
    if (first[0] != 0x02) throw JetError("page " + std::to_string(pgno) + " is not a table definition");
    std::vector<std::uint8_t> buf(first.begin(), first.end());
    std::uint32_t next = u32(first, 4);
    std::set<std::uint32_t> seen{pgno};
    while (next && !seen.count(next)) {
        seen.insert(next);
        const Bytes p = page(next);
        buf.insert(buf.end(), p.begin() + 8, p.end());
        next = u32(p, 4);
    }
    const Bytes b = buf;

    TableDef def;
    def.rowCount = u32(b, 0x0C);
    const std::uint16_t numVarCols = u16(b, 0x17);
    const std::uint16_t numCols = u16(b, 0x19);
    const std::uint32_t numRealIdx = u32(b, 0x1F);
    std::size_t pos = 0x2B + static_cast<std::size_t>(numRealIdx) * 8;
    std::vector<Column> cols(numCols);
    for (Column& c : cols) {
        c.type = static_cast<std::uint8_t>(readLe(b, pos, 1));
        c.number = u16(b, pos + 1);
        c.varIndex = u16(b, pos + 3);
        c.flags = static_cast<std::uint8_t>(readLe(b, pos + 13, 1));
        c.fixedOffset = u16(b, pos + 14);
        c.length = u16(b, pos + 16);
        pos += 18;
    }
    for (Column& c : cols) {
        const std::size_t n = at(b, static_cast<std::ptrdiff_t>(pos));
        c.name = cp1252ToUtf8(slice(b, static_cast<std::ptrdiff_t>(pos + 1), static_cast<std::ptrdiff_t>(pos + 1 + n)));
        pos += 1 + n;
    }
    std::stable_sort(cols.begin(), cols.end(), [](const Column& a, const Column& c) { return a.number < c.number; });
    def.columns = std::move(cols);
    def.hasVar = numVarCols > 0;
    return def;
}

const std::vector<std::uint32_t>& Database::pagesOwnedBy(std::uint32_t owner) const {
    if (!ownersScanned_) {
        ownersScanned_ = true;
        for (std::size_t n = 1; n < numPages_; ++n) {
            const std::size_t off = n * kPageSize;
            if (data_[off] == 0x01 && data_[off + 1] == 0x01)
                owners_[u32(data_, off + 4)].push_back(static_cast<std::uint32_t>(n));
        }
    }
    static const std::vector<std::uint32_t> none;
    const auto it = owners_.find(owner);
    return it == owners_.end() ? none : it->second;
}

std::vector<std::vector<std::uint8_t>> Database::rawRows(std::uint32_t tdefPage) const {
    struct Pending {
        bool pointer = false;
        std::uint32_t ptr = 0;
        std::uint32_t page = 0, row = 0;
        Bytes bytes;
    };
    std::vector<Pending> rows;
    std::set<std::pair<std::uint32_t, std::uint32_t>> targets;
    for (const std::uint32_t n : pagesOwnedBy(tdefPage)) {
        const Bytes pg = page(n);
        const std::uint16_t nrows = u16(pg, 8);
        for (std::uint32_t r = 0; r < nrows; ++r) {
            const Bounds b = rowBounds(pg, r);
            if ((b.flags & kRowDeleted) || b.start == 0) continue;
            Pending p;
            if (b.flags & kRowOverflow) {
                p.pointer = true;
                p.ptr = u32(pg, b.start);
                targets.insert({p.ptr >> 8, p.ptr & 0xFF});
            } else {
                p.page = n;
                p.row = r;
                p.bytes = slice(pg, static_cast<std::ptrdiff_t>(b.start), static_cast<std::ptrdiff_t>(b.end));
            }
            rows.push_back(p);
        }
    }
    std::vector<std::vector<std::uint8_t>> out;
    out.reserve(rows.size());
    for (const Pending& p : rows) {
        if (p.pointer) out.push_back(readRowPointer(p.ptr));
        else if (!targets.count({p.page, p.row})) out.push_back(toVector(p.bytes));
    }
    return out;
}

std::vector<std::uint8_t> Database::longValue(std::span<const std::uint8_t> f) const {
    if (f.size() < 12) return {};
    const std::uint32_t word = u32(f, 0);
    const std::uint32_t kind = word & 0xC0000000u;
    const std::size_t length = word & 0x3FFFFFFFu;
    std::uint32_t ptr = u32(f, 4);
    if (kind == 0x80000000u) // stored inline after the 12-byte header
        return toVector(slice(f, 12, static_cast<std::ptrdiff_t>(12 + length)));
    if (kind == 0x40000000u) { // one row on another page
        std::vector<std::uint8_t> v = readRowPointer(ptr);
        if (v.size() > length) v.resize(length);
        return v;
    }
    std::vector<std::uint8_t> out; // chain of rows, each starting with a next pointer
    std::set<std::uint32_t> seen;
    while (ptr && !seen.count(ptr) && out.size() < length) {
        seen.insert(ptr);
        const std::vector<std::uint8_t> chunk = readRowPointer(ptr);
        ptr = u32(chunk, 0);
        out.insert(out.end(), chunk.begin() + 4, chunk.end());
    }
    if (out.size() > length) out.resize(length);
    return out;
}

std::vector<Value> Database::decodeRow(std::span<const std::uint8_t> raw, const TableDef& def) const {
    const auto len = static_cast<std::ptrdiff_t>(raw.size());
    const std::ptrdiff_t rowCols = at(raw, 0);
    const std::ptrdiff_t maskLen = (rowCols + 7) / 8;
    const Bytes mask = slice(raw, len - maskLen, len);
    const std::ptrdiff_t rowEnd = len - 1;
    const std::ptrdiff_t varCount = def.hasVar ? at(raw, rowEnd - maskLen) : 0;
    std::vector<std::ptrdiff_t> offsets;
    if (varCount) offsets = varOffsets(raw, rowEnd, maskLen, varCount);

    std::vector<Value> values;
    values.reserve(def.columns.size());
    for (const Column& c : def.columns) {
        const bool present = c.number < rowCols && (at(mask, c.number / 8) & (1 << (c.number % 8))) != 0;
        if (c.type == Bool) {
            values.push_back(Value::makeInt(present ? 1 : 0));
            continue;
        }
        if (!present) {
            values.emplace_back();
            continue;
        }
        Bytes v;
        if (c.isFixed()) {
            const std::ptrdiff_t o = 1 + c.fixedOffset;
            v = slice(raw, o, o + c.length);
        } else {
            if (c.varIndex >= varCount) {
                values.emplace_back();
                continue;
            }
            v = slice(raw, offsets[c.varIndex], offsets[c.varIndex + 1u]);
        }

        Value out;
        switch (c.type) {
        case Byte: out = Value::makeInt(at(v, 0)); break;
        case Int: out = Value::makeInt(static_cast<std::int16_t>(exact(v, 2))); break;
        case Long: out = Value::makeInt(static_cast<std::int32_t>(exact(v, 4))); break;
        case Money: out = Value::makeReal(static_cast<double>(static_cast<std::int64_t>(exact(v, 8))) / 10000.0); break;
        case Float: out = Value::makeReal(std::bit_cast<float>(static_cast<std::uint32_t>(exact(v, 4)))); break;
        case Double:
        case DateTime: out = Value::makeReal(std::bit_cast<double>(exact(v, 8))); break;
        case Text:
            out.kind = Value::Kind::Text;
            out.text = cp1252ToUtf8(v);
            break;
        case Memo:
            out.kind = Value::Kind::Text;
            out.text = cp1252ToUtf8(longValue(v));
            break;
        case Ole:
            out.kind = Value::Kind::Blob;
            out.blob = longValue(v);
            break;
        default:
            out.kind = Value::Kind::Blob;
            out.blob = toVector(v);
            break;
        }
        values.push_back(std::move(out));
    }
    return values;
}

void Database::tables(std::vector<TableRef>& user, std::vector<LinkRef>& linked) const {
    user.clear();
    linked.clear();
    const TableDef catalog = tableDef(2); // MSysObjects
    for (const std::vector<std::uint8_t>& raw : rawRows(2)) {
        const std::vector<Value> r = decodeRow(raw, catalog);
        const std::string name = textOrEmpty(field(catalog.columns, r, "Name"));
        if (name.rfind("MSys", 0) == 0 || intOr0(field(catalog.columns, r, "ParentId")) != kTablesContainer) continue;
        const Value* type = field(catalog.columns, r, "Type");
        const std::int64_t t = type && type->kind == Value::Kind::Integer ? type->integer : -1;
        const auto flags = static_cast<std::uint32_t>(intOr0(field(catalog.columns, r, "Flags")));
        if (t == 1 && !(flags & 0x80000002u))
            user.push_back({name, static_cast<std::uint32_t>(intOr0(field(catalog.columns, r, "Id"))) & 0x00FFFFFFu});
        else if (t == 6)
            linked.push_back({name, textOrEmpty(field(catalog.columns, r, "ForeignName"))});
    }
    std::stable_sort(user.begin(), user.end(),
                     [](const TableRef& a, const TableRef& b) { return asciiLower(a.name) < asciiLower(b.name); });
}

Table Database::readTable(std::uint32_t page) const {
    Table t;
    t.def = tableDef(page);
    for (const std::vector<std::uint8_t>& raw : rawRows(page)) t.rows.push_back(decodeRow(raw, t.def));
    return t;
}

} // namespace openday::formats::jet
