// Jet 3 reader and MDB -> SQLite converter tests on a synthetic database
// built here (docs/formats/MDB.md). With --game-dir <dir> --compare <dir>
// it additionally converts a local installation and compares it with a
// conversion made by tools/mdb/mdb2sqlite.py (optional, never in CI).

#include "data/mdb_import.hpp"
#include "data/sqlite_db.hpp"
#include "formats/binary.hpp"
#include "formats/mdb.hpp"
#include "formats/zip.hpp"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

using namespace openday;
using namespace openday::formats;
namespace fs = std::filesystem;

namespace {

int failures = 0;

#define CHECK(cond)                                                                       \
    do {                                                                                  \
        if (!(cond)) {                                                                    \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                                   \
        }                                                                                 \
    } while (0)

using Bytes = std::vector<std::uint8_t>;

Bytes text(const std::string& s) { return {s.begin(), s.end()}; }
Bytes u32(std::uint32_t v) {
    Bytes b;
    binary::putU32(b, v);
    return b;
}
Bytes u16(std::uint16_t v) {
    Bytes b;
    binary::putU16(b, v);
    return b;
}
Bytes f64(double d) {
    std::uint64_t v;
    std::memcpy(&v, &d, 8);
    Bytes b;
    for (int i = 0; i < 8; ++i) b.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
    return b;
}
Bytes f32(float f) {
    std::uint32_t v;
    std::memcpy(&v, &f, 4);
    return u32(v);
}
std::uint32_t rowPtr(std::uint32_t page, std::uint32_t row) { return page << 8 | row; }

// --- a minimal Jet 3 writer ---------------------------------------------

struct ColSpec {
    std::string name;
    std::uint8_t type;
    bool fixed;
    std::uint16_t fixedOffset; // fixed columns
    std::uint16_t length;
    std::uint16_t varIndex;    // variable columns
};

// A cell: absent (NULL; for Boolean columns: false) or the stored bytes.
struct Cell {
    bool present = false;
    Bytes data;
};
Cell val(Bytes b) { return {true, std::move(b)}; }
Cell null() { return {}; }

class JetBuilder {
public:
    explicit JetBuilder(std::size_t pages) : file_(pages * jet::kPageSize, 0) {
        const char magic[] = "Standard Jet DB";
        file_[1] = 1;
        std::memcpy(file_.data() + 4, magic, 15);
    }

    void tdef(std::uint32_t page, const std::vector<ColSpec>& cols, std::uint32_t rows) {
        Bytes p(jet::kPageSize, 0);
        p[0] = 0x02;
        p[1] = 0x01;
        put(p, 0x0C, u32(rows));
        std::uint16_t numVar = 0;
        for (const ColSpec& c : cols) numVar = static_cast<std::uint16_t>(numVar + (c.fixed ? 0 : 1));
        put(p, 0x17, u16(numVar));
        put(p, 0x19, u16(static_cast<std::uint16_t>(cols.size())));
        put(p, 0x1F, u32(0)); // no real indexes
        std::size_t pos = 0x2B;
        // Column records in reverse order: the reader sorts by column number.
        for (std::size_t k = cols.size(); k-- > 0;) {
            const ColSpec& c = cols[k];
            p[pos] = c.type;
            put(p, pos + 1, u16(static_cast<std::uint16_t>(k)));
            put(p, pos + 3, u16(c.varIndex));
            p[pos + 13] = c.fixed ? 0x01 : 0x02;
            put(p, pos + 14, u16(c.fixedOffset));
            put(p, pos + 16, u16(c.length));
            pos += 18;
        }
        for (std::size_t k = cols.size(); k-- > 0;) {
            p[pos++] = static_cast<std::uint8_t>(cols[k].name.size());
            put(p, pos, text(cols[k].name));
            pos += cols[k].name.size();
        }
        setPage(page, p);
    }

    // Rows are (bytes, offset flags).
    void dataPage(std::uint32_t page, std::uint32_t owner, const std::vector<std::pair<Bytes, std::uint16_t>>& rows) {
        Bytes p(jet::kPageSize, 0);
        p[0] = 0x01;
        p[1] = 0x01;
        put(p, 4, u32(owner));
        put(p, 8, u16(static_cast<std::uint16_t>(rows.size())));
        std::size_t end = jet::kPageSize;
        for (std::size_t i = 0; i < rows.size(); ++i) {
            const std::size_t start = end - rows[i].first.size();
            put(p, start, rows[i].first);
            put(p, 10 + i * 2, u16(static_cast<std::uint16_t>(start | rows[i].second)));
            end = start;
        }
        setPage(page, p);
    }

    const Bytes& bytes() const { return file_; }

private:
    Bytes file_;
    static void put(Bytes& p, std::size_t at, const Bytes& b) { std::copy(b.begin(), b.end(), p.begin() + static_cast<std::ptrdiff_t>(at)); }
    void setPage(std::uint32_t n, const Bytes& p) { std::copy(p.begin(), p.end(), file_.begin() + static_cast<std::ptrdiff_t>(n * jet::kPageSize)); }
};

// Encodes one row in the Jet 3 layout (MDB.md "Row layout"), with the jump
// table for rows longer than 256 bytes.
Bytes makeRow(const std::vector<ColSpec>& cols, const std::vector<Cell>& cells) {
    const std::size_t n = cols.size();
    std::size_t fixedSize = 0;
    std::size_t varCount = 0;
    for (const ColSpec& c : cols) {
        if (c.type == jet::Bool) continue;
        if (c.fixed) fixedSize = std::max<std::size_t>(fixedSize, c.fixedOffset + c.length);
        else ++varCount;
    }
    Bytes row(1 + fixedSize, 0);
    row[0] = static_cast<std::uint8_t>(n);
    const std::size_t maskLen = (n + 7) / 8;
    Bytes mask(maskLen, 0);
    for (std::size_t i = 0; i < n; ++i) {
        if (cells[i].present) mask[i / 8] = static_cast<std::uint8_t>(mask[i / 8] | (1 << (i % 8)));
        if (cols[i].type != jet::Bool && cols[i].fixed && cells[i].present)
            std::copy(cells[i].data.begin(), cells[i].data.end(), row.begin() + 1 + cols[i].fixedOffset);
    }
    if (varCount == 0) {
        row.insert(row.end(), mask.begin(), mask.end());
        return row;
    }
    std::vector<std::size_t> offsets(varCount + 1);
    for (std::size_t v = 0; v < varCount; ++v) {
        offsets[v] = row.size();
        for (std::size_t i = 0; i < n; ++i)
            if (!cols[i].fixed && cols[i].type != jet::Bool && cols[i].varIndex == v && cells[i].present)
                row.insert(row.end(), cells[i].data.begin(), cells[i].data.end());
    }
    offsets[varCount] = row.size();
    std::size_t jumps = 0;
    for (;;) {
        const std::size_t total = row.size() + varCount + 1 + jumps + 1 + maskLen;
        const std::size_t want = (total - 1) / 256;
        if (want == jumps) break;
        jumps = want;
    }
    if (row.size() < jumps * 256) throw std::runtime_error("test row would need a dummy jump entry");
    Bytes trailer;
    for (std::size_t i = varCount + 1; i-- > 0;) trailer.push_back(static_cast<std::uint8_t>(offsets[i] & 0xFF));
    for (std::size_t j = jumps; j-- > 0;) {
        std::size_t first = 0;
        while (offsets[first] < (j + 1) * 256) ++first;
        trailer.push_back(static_cast<std::uint8_t>(first));
    }
    trailer.push_back(static_cast<std::uint8_t>(varCount));
    row.insert(row.end(), trailer.begin(), trailer.end());
    row.insert(row.end(), mask.begin(), mask.end());
    return row;
}

// Memo / OLE descriptors (MDB.md "Memo and OLE values").
Bytes longInline(const Bytes& value) {
    Bytes b = u32(0x80000000u | static_cast<std::uint32_t>(value.size()));
    const Bytes rest = u32(0);
    b.insert(b.end(), rest.begin(), rest.end());
    b.insert(b.end(), rest.begin(), rest.end());
    b.insert(b.end(), value.begin(), value.end());
    return b;
}
Bytes longRef(std::uint32_t flags, std::size_t length, std::uint32_t ptr) {
    Bytes b = u32(flags | static_cast<std::uint32_t>(length));
    const Bytes p = u32(ptr), z = u32(0);
    b.insert(b.end(), p.begin(), p.end());
    b.insert(b.end(), z.begin(), z.end());
    return b;
}

// Pages: 0 header, 2 catalog TDEF, 3 catalog rows, 4 "Units" TDEF, 5 its
// rows, 6 long values, 7 "alpha" TDEF, 8 its rows.
const std::vector<ColSpec> kCatalog = {
    {"Id", jet::Long, true, 0, 4, 0},          {"ParentId", jet::Long, true, 4, 4, 0}, {"Name", jet::Text, false, 0, 255, 0},
    {"Type", jet::Int, true, 8, 2, 0},         {"Flags", jet::Long, true, 10, 4, 0},   {"ForeignName", jet::Text, false, 0, 255, 1},
};
const std::vector<ColSpec> kUnits = {
    {"Counter", jet::Long, true, 0, 4, 0},   {"Active", jet::Bool, true, 0, 0, 0},    {"Kind", jet::Byte, true, 4, 1, 0},
    {"Small", jet::Int, true, 5, 2, 0},      {"Cost", jet::Double, true, 7, 8, 0},    {"Rate", jet::Float, true, 15, 4, 0},
    {"Price", jet::Money, true, 19, 8, 0},   {"Name", jet::Text, false, 0, 255, 0},   {"Notes", jet::Memo, false, 0, 0, 1},
    {"Picture", jet::Ole, false, 0, 0, 2},   {"Raw", jet::Binary, false, 0, 16, 3},
};
const std::vector<ColSpec> kAlpha = {{"Id", jet::Long, true, 0, 4, 0}, {"Flag", jet::Bool, true, 0, 0, 0}};

std::string longName() {
    std::string s;
    for (int i = 0; i < 30; ++i) s += "Name" + std::to_string(1000 + i) + ",";
    return s; // 300 bytes: the row needs a jump table
}

Bytes buildDatabase() {
    JetBuilder b(9);
    auto catalogRow = [](std::int32_t id, std::int32_t parent, const std::string& name, std::int16_t type, std::uint32_t flags,
                         const std::string& foreign) {
        return makeRow(kCatalog, {val(u32(static_cast<std::uint32_t>(id))), val(u32(static_cast<std::uint32_t>(parent))), val(text(name)),
                                  val(u16(static_cast<std::uint16_t>(type))), val(u32(flags)),
                                  foreign.empty() ? null() : val(text(foreign))});
    };
    b.tdef(2, kCatalog, 6);
    b.dataPage(3, 2,
               {{catalogRow(0x0F000001, 0x0F000000, "Tables", 3, 0, ""), 0},
                {catalogRow(2, 0x0F000001, "MSysObjects", 1, 0x80000002u, ""), 0},
                {catalogRow(4, 0x0F000001, "Units", 1, 0, ""), 0},
                {catalogRow(7, 0x0F000001, "alpha", 1, 0, ""), 0},
                {catalogRow(9, 0x0F000001, "Hidden", 1, 0x80000000u, ""), 0},
                {catalogRow(10, 0x0F000001, "Player Types", 6, 0, "Player Types"), 0},
                {catalogRow(11, 0x0F000002, "Some Form", 1, 0, ""), 0}});

    // "Pánzer €" in Windows-1252.
    const Bytes cpName = {'P', 0xE1, 'n', 'z', 'e', 'r', ' ', 0x80, 0x81};
    const Bytes picture = {1, 2, 3, 4, 5, 6, 7};
    b.tdef(4, kUnits, 3);
    const Bytes rowA = makeRow(kUnits, {val(u32(1)), val({}), val({200}), val(u16(static_cast<std::uint16_t>(-5))), val(f64(2.5)),
                                        val(f32(0.1f)), val(u32(123456789)), val(cpName), val(longInline(text("short memo"))),
                                        val(longRef(0x40000000u, picture.size(), rowPtr(6, 0))), val({})});
    Bytes priceC = u32(static_cast<std::uint32_t>(-15000));
    priceC.insert(priceC.end(), {0xFF, 0xFF, 0xFF, 0xFF});
    const Bytes rowC = makeRow(kUnits, {val(u32(3)), null(), null(), null(), null(), null(), val(priceC), val(text(longName())),
                                        val(longRef(0, 10, rowPtr(6, 1))), null(), val({0xAB, 0xCD})});
    const Bytes deleted = makeRow(kUnits, {val(u32(99)), null(), null(), null(), null(), null(), null(), null(), null(), null(), null()});
    // Row 0 is an overflow pointer to row 2; row 3 is deleted.
    b.dataPage(5, 4, {{u32(rowPtr(5, 2)), 0x4000}, {rowA, 0}, {rowC, 0}, {deleted, 0x8000}});

    // Long values: row 0 the OLE value (plus a byte beyond its length),
    // rows 1 and 2 a memo chain "first|" + "second".
    Bytes ole = picture;
    ole.push_back(0xEE);
    Bytes chain1 = u32(rowPtr(6, 2));
    for (char c : std::string("first|")) chain1.push_back(static_cast<std::uint8_t>(c));
    Bytes chain2 = u32(0);
    for (char c : std::string("second")) chain2.push_back(static_cast<std::uint8_t>(c));
    b.dataPage(6, 0, {{ole, 0}, {chain1, 0}, {chain2, 0}});

    b.tdef(7, kAlpha, 2);
    b.dataPage(8, 7, {{makeRow(kAlpha, {val(u32(1)), val({})}), 0}, {makeRow(kAlpha, {val(u32(2)), null()}), 0}});
    return b.bytes();
}

void testReader(const Bytes& file) {
    const jet::Database db(file);
    std::vector<jet::TableRef> user;
    std::vector<jet::LinkRef> linked;
    db.tables(user, linked);
    CHECK(user.size() == 2);
    if (user.size() == 2) {
        CHECK(user[0].name == "alpha" && user[0].tdefPage == 7);
        CHECK(user[1].name == "Units" && user[1].tdefPage == 4);
    }
    CHECK(linked.size() == 1 && linked[0].name == "Player Types" && linked[0].foreignName == "Player Types");

    const jet::Table units = db.readTable(4);
    CHECK(units.def.columns.size() == kUnits.size());
    CHECK(units.def.columns[0].name == "Counter" && units.def.columns[10].name == "Raw");
    CHECK(units.rows.size() == 2);
    if (units.rows.size() != 2) return;
    // The overflow row comes first (row 0 points at it), and only once.
    const auto& c = units.rows[0];
    const auto& a = units.rows[1];
    CHECK(c[0].integer == 3 && a[0].integer == 1);
    CHECK(a[1].kind == jet::Value::Kind::Integer && a[1].integer == 1);
    CHECK(c[1].integer == 0);
    CHECK(a[2].integer == 200);
    CHECK(a[3].integer == -5);
    CHECK(a[4].kind == jet::Value::Kind::Real && a[4].real == 2.5);
    CHECK(a[5].real == static_cast<double>(0.1f));
    CHECK(a[6].real == 12345.6789);
    CHECK(a[7].kind == jet::Value::Kind::Text && a[7].text == "P\xC3\xA1nzer \xE2\x82\xAC\xEF\xBF\xBD");
    CHECK(a[8].text == "short memo");
    CHECK(a[9].kind == jet::Value::Kind::Blob && a[9].blob == Bytes({1, 2, 3, 4, 5, 6, 7}));
    CHECK(a[10].kind == jet::Value::Kind::Blob && a[10].blob.empty());
    CHECK(c[2].kind == jet::Value::Kind::Null && c[4].kind == jet::Value::Kind::Null);
    CHECK(c[6].real == -1.5);
    CHECK(c[7].text == longName());
    CHECK(c[8].text == "first|seco");
    CHECK(c[9].kind == jet::Value::Kind::Null);
    CHECK(c[10].blob == Bytes({0xAB, 0xCD}));

    const jet::Table alpha = db.readTable(7);
    CHECK(alpha.rows.size() == 2 && alpha.rows[0][1].integer == 1 && alpha.rows[1][1].integer == 0);

    Bytes bad = file;
    bad[0x14] = 1;
    bool threw = false;
    try {
        jet::Database x(bad);
    } catch (const jet::JetError&) {
        threw = true;
    }
    CHECK(threw);
}

void checkConverted(const fs::path& path) {
    const data::SqliteDb db(path);
    std::vector<std::string> types;
    db.forEach("SELECT typeof(Counter), typeof(Active), typeof(Cost), typeof(Name), typeof(Notes), typeof(Picture), "
               "typeof(Raw), typeof(Kind) FROM Units ORDER BY rowid",
               [&](const data::Row& r) {
                   std::string s;
                   for (int i = 0; i < 8; ++i) s += r.text(i) + ",";
                   types.push_back(s);
               });
    CHECK(types.size() == 2);
    if (types.size() == 2) {
        CHECK(types[0] == "integer,integer,null,text,text,null,blob,null,");
        CHECK(types[1] == "integer,integer,real,text,text,blob,blob,integer,");
    }
    std::string cols;
    db.forEach("SELECT table_name, position, column_name, jet_type, jet_length FROM _mdb_columns ORDER BY rowid",
               [&](const data::Row& r) {
                   cols += r.text(0) + "." + std::to_string(r.integer(1)) + "." + r.text(2) + ":" + r.text(3) + "/" +
                           std::to_string(r.integer(4)) + " ";
               });
    CHECK(cols.rfind("alpha.0.Id:INT32/4 alpha.1.Flag:BOOLEAN/0 Units.0.Counter:INT32/4 ", 0) == 0);
    CHECK(cols.find("Units.6.Price:MONEY/8 ") != std::string::npos);
    std::string links;
    db.forEach("SELECT table_name, foreign_name FROM _mdb_links", [&](const data::Row& r) { links += r.text(0) + "=" + r.text(1); });
    CHECK(links == "Player Types=Player Types");
    std::string sql;
    db.forEach("SELECT sql FROM sqlite_master WHERE name = 'Units'", [&](const data::Row& r) { sql = r.text(0); });
    CHECK(sql == "CREATE TABLE \"Units\" (\"Counter\" INTEGER, \"Active\" INTEGER, \"Kind\" INTEGER, \"Small\" INTEGER, "
                 "\"Cost\" REAL, \"Rate\" REAL, \"Price\" REAL, \"Name\" TEXT, \"Notes\" TEXT, \"Picture\" BLOB, \"Raw\" BLOB)");
}

void testConvert(const Bytes& file) {
    const fs::path dir = fs::temp_directory_path() / "openday_mdb_test";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const auto summary = data::convertMdb(file, dir / "test.sqlite");
    CHECK(summary.size() == 2 && summary[1].name == "Units" && summary[1].rows == 2);
    checkConverted(dir / "test.sqlite");
    // Converting again replaces the file.
    data::convertMdb(file, dir / "test.sqlite");
    checkConverted(dir / "test.sqlite");
    fs::remove_all(dir);
}

// A whole synthetic installation: plain .MDB files and ZIP members, with
// lower-case folder names as innoextract may produce them.
void testInstall(const Bytes& file) {
    const fs::path root = fs::temp_directory_path() / "openday_mdb_install";
    fs::remove_all(root);
    const fs::path game = root / "game", out = root / "data";
    fs::create_directories(game / "globals");
    fs::create_directories(game / "scens");
    std::ofstream(game / "globals" / "GLOBAL.MDB", std::ios::binary).write(reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(file.size()));
    const Bytes zip = writeZip({{"s1-1-1.mdb", file, {}}, {"s1-1-1.ldb", text("lock"), {}}});
    std::ofstream(game / "scens" / "scen1.zip", std::ios::binary).write(reinterpret_cast<const char*>(zip.data()), static_cast<std::streamsize>(zip.size()));

    const data::InstallDatabases dbs(game);
    CHECK(dbs.size() == 2);
    if (dbs.size() == 2) {
        CHECK(dbs.stem(0) == "GLOBALS/GLOBAL");
        CHECK(dbs.stem(1) == "SCENS/SCEN1/s1-1-1");
    }
    CHECK(!data::isImportCurrent(game, out));
    std::size_t calls = 0;
    const data::ImportReport r = data::importInstallation(game, out, [&](std::size_t, std::size_t, const std::string&) { ++calls; });
    CHECK(r.failures.empty() && r.converted == 2 && calls == 3);
    CHECK(fs::exists(out / "GLOBALS" / "GLOBAL.sqlite"));
    CHECK(fs::exists(out / "SCENS" / "SCEN1" / "s1-1-1.sqlite"));
    CHECK(data::isImportCurrent(game, out));
    // A changed source file makes the conversion outdated.
    std::ofstream(game / "globals" / "GAMEINFO.MDB", std::ios::binary) << "x";
    CHECK(!data::isImportCurrent(game, out));

    bool refused = false;
    try {
        data::importInstallation(game, game / "data");
    } catch (const std::exception&) {
        refused = true;
    }
    CHECK(refused);
    fs::remove_all(root);
}

// Compares two SQLite files table by table (values and storage classes).
bool sameDatabase(const fs::path& a, const fs::path& b, std::string& why) {
    const data::SqliteDb da(a), db(b);
    std::vector<std::string> tables, other;
    da.forEach("SELECT name || '|' || sql FROM sqlite_master ORDER BY rowid", [&](const data::Row& r) { tables.push_back(r.text(0)); });
    db.forEach("SELECT name || '|' || sql FROM sqlite_master ORDER BY rowid", [&](const data::Row& r) { other.push_back(r.text(0)); });
    if (tables != other) {
        why = "schema differs";
        return false;
    }
    for (const std::string& t : tables) {
        const std::string name = t.substr(0, t.find('|'));
        // One text per row: storage class and exact value of every column.
        std::string expr;
        da.forEach("PRAGMA table_info(" + data::quoteIdent(name) + ")", [&](const data::Row& r) {
            const std::string c = data::quoteIdent(r.text(1));
            expr += (expr.empty() ? "" : " || '|' || ") + std::string("typeof(") + c + ") || ':' || CASE WHEN typeof(" + c +
                    ") = 'real' THEN printf('%!.17g', " + c + ") ELSE hex(" + c + ") END";
        });
        auto dump = [&](const data::SqliteDb& d) {
            std::string s;
            if (expr.empty()) return s;
            d.forEach("SELECT " + expr + " FROM " + data::quoteIdent(name) + " ORDER BY rowid",
                      [&](const data::Row& r) { s += r.text(0) + "\n"; });
            return s;
        };
        if (dump(da) != dump(db)) {
            why = "table " + name + " differs";
            return false;
        }
    }
    return true;
}

int compareInstall(const fs::path& gameDir, const fs::path& reference) {
    const fs::path out = fs::temp_directory_path() / "openday_mdb_compare";
    fs::remove_all(out);
    const data::ImportReport r = data::importInstallation(gameDir, out);
    CHECK(r.failures.empty());
    for (const std::string& f : r.failures) std::fprintf(stderr, "  %s\n", f.c_str());
    std::size_t compared = 0;
    for (const auto& e : fs::recursive_directory_iterator(out)) {
        if (e.path().extension() != ".sqlite") continue;
        const fs::path ref = reference / fs::relative(e.path(), out);
        std::string why;
        if (!fs::exists(ref)) {
            std::fprintf(stderr, "missing in reference: %s\n", ref.string().c_str());
            ++failures;
        } else if (!sameDatabase(e.path(), ref, why)) {
            std::fprintf(stderr, "%s: %s\n", e.path().string().c_str(), why.c_str());
            ++failures;
        }
        ++compared;
    }
    std::printf("compared %zu databases with %s\n", compared, reference.string().c_str());
    CHECK(compared == r.converted);
    fs::remove_all(out);
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    fs::path gameDir, reference;
    for (int i = 1; i + 1 < argc; i += 2) {
        const std::string a = argv[i];
        if (a == "--game-dir") gameDir = argv[i + 1];
        else if (a == "--compare") reference = argv[i + 1];
    }
    try {
        const Bytes file = buildDatabase();
        testReader(file);
        testConvert(file);
        testInstall(file);
        if (!gameDir.empty() && !reference.empty()) compareInstall(gameDir, reference);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "exception: %s\n", e.what());
        ++failures;
    }
    if (failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("mdb_test: all checks passed\n");
    return 0;
}
