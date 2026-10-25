#include "formats/inno_setup.hpp"

#include <algorithm>
#include <cstring>
#include <map>
#include <memory>
#include <optional>

#include "formats/byte_source.hpp"
#include "formats/game_path.hpp"
#include "formats/hash.hpp"
#include "formats/inflate.hpp"
#include "formats/lzma.hpp"
#include "formats/pe_resources.hpp"
#include "formats/sha256.hpp"

namespace openday::formats {
namespace {

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Versions

using Ver = std::uint32_t;
constexpr Ver V(unsigned a, unsigned b, unsigned c, unsigned d = 0) { return (a << 24) | (b << 16) | (c << 8) | d; }

struct Version {
    Ver value = 0;
    bool unicode = false;
    std::string text;

    bool operator>=(Ver v) const { return value >= v; }
    bool operator<(Ver v) const { return value < v; }
    bool operator==(Ver v) const { return value == v; }
};

std::string versionText(Ver v, bool unicode) {
    std::string s = std::to_string(v >> 24) + "." + std::to_string((v >> 16) & 255) + "." + std::to_string((v >> 8) & 255);
    if (v & 255) s += "." + std::to_string(v & 255);
    if (unicode) s += " (unicode)";
    return s;
}

// The setup data versions whose layouts are known (docs/formats/INNO.md).
constexpr Ver kKnownVersions[] = {
    V(5, 0, 0),  V(5, 0, 1),  V(5, 0, 3),  V(5, 0, 4),  V(5, 1, 0),  V(5, 1, 2),  V(5, 1, 7),
    V(5, 1, 10), V(5, 1, 13), V(5, 2, 0),  V(5, 2, 1),  V(5, 2, 3),  V(5, 2, 5),  V(5, 3, 0),
    V(5, 3, 3),  V(5, 3, 5),  V(5, 3, 6),  V(5, 3, 7),  V(5, 3, 8),  V(5, 3, 9),  V(5, 3, 10),
    V(5, 4, 2),  V(5, 5, 0),  V(5, 5, 6),  V(5, 5, 7),  V(5, 6, 0),  V(5, 6, 2),  V(6, 0, 0),
    V(6, 1, 0),  V(6, 3, 0),  V(6, 4, 0),  V(6, 4, 0, 1), V(6, 4, 2), V(6, 4, 3),
};
constexpr Ver kNewestSupported = V(6, 4, 3);

// Parses the 64-byte setup data id ("Inno Setup Setup Data (5.5.7) (u)").
Version parseVersionId(const std::uint8_t* id) {
    std::string s(reinterpret_cast<const char*>(id), 64);
    s.resize(std::strlen(s.c_str()));
    Version v;
    v.text = s;
    if (s.find("Inno Setup") == std::string::npos) throw InnoError("not an Inno Setup data header");
    // The largest "(a.b.c[.d])" group: "with ISX (x)" ids carry two.
    for (std::size_t p = s.find('('); p != std::string::npos; p = s.find('(', p + 1)) {
        unsigned parts[4] = {0, 0, 0, 0};
        std::size_t q = p + 1;
        int n = 0;
        while (n < 4 && q < s.size() && s[q] >= '0' && s[q] <= '9') {
            unsigned x = 0;
            while (q < s.size() && s[q] >= '0' && s[q] <= '9') x = x * 10 + static_cast<unsigned>(s[q++] - '0');
            parts[n++] = x;
            if (q < s.size() && s[q] == 'a') ++q; // "(2.0.6a)"
            if (q < s.size() && s[q] == '.') ++q;
            else break;
        }
        if (n >= 3) v.value = std::max(v.value, V(parts[0], parts[1], parts[2], parts[3]));
    }
    if (v.value == 0) throw InnoError("unrecognised Inno Setup version: " + s);
    v.unicode = v.value >= V(6, 3, 0) || s.find("(u)") != std::string::npos || s.find("(U)") != std::string::npos;
    return v;
}

// Layout variants to try, in order. Some releases (and some modified
// builds) kept an older data id while the layout changed.
std::vector<Ver> candidateVersions(Ver listed) {
    if (listed == V(5, 3, 10)) return {listed, V(5, 3, 10, 1)};
    if (listed == V(5, 4, 2)) return {listed, V(5, 4, 2, 1)};
    if (listed == V(5, 5, 0)) return {listed, V(5, 5, 0, 1)};
    if (listed == V(5, 5, 7)) return {listed, V(5, 5, 7, 1), V(5, 6, 0)};
    if (std::find(std::begin(kKnownVersions), std::end(kKnownVersions), listed) != std::end(kKnownVersions)) return {listed};
    // Unknown: the nearest known layout below it.
    Ver best = 0;
    for (const Ver k : kKnownVersions)
        if (k <= listed) best = k;
    return {best ? best : listed};
}

// ---------------------------------------------------------------------------
// Text

void appendUtf8(std::string& s, char32_t c) {
    if (c < 0x80) {
        s += static_cast<char>(c);
    } else if (c < 0x800) {
        s += static_cast<char>(0xC0 | (c >> 6));
        s += static_cast<char>(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
        s += static_cast<char>(0xE0 | (c >> 12));
        s += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        s += static_cast<char>(0x80 | (c & 0x3F));
    } else {
        s += static_cast<char>(0xF0 | (c >> 18));
        s += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
        s += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        s += static_cast<char>(0x80 | (c & 0x3F));
    }
}

std::string utf16le(const std::string& raw) {
    std::string s;
    for (std::size_t i = 0; i + 1 < raw.size(); i += 2) {
        char32_t c = static_cast<std::uint8_t>(raw[i]) | (static_cast<std::uint8_t>(raw[i + 1]) << 8);
        if (c >= 0xD800 && c < 0xDC00 && i + 3 < raw.size()) {
            const char32_t lo = static_cast<std::uint8_t>(raw[i + 2]) | (static_cast<std::uint8_t>(raw[i + 3]) << 8);
            if (lo >= 0xDC00 && lo < 0xE000) {
                c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00);
                i += 2;
            }
        }
        appendUtf8(s, c);
    }
    return s;
}

// Windows-1252 (the code page of the non-Unicode installers' Western
// languages); bytes 0x80..0x9F that have no mapping are kept as C1 controls.
std::string cp1252(const std::string& raw) {
    static const char16_t high[32] = {0x20AC, 0x81,   0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
                                      0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x8D,   0x017D, 0x8F,
                                      0x90,   0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
                                      0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x9D,   0x017E, 0x0178};
    std::string s;
    for (const char ch : raw) {
        const auto b = static_cast<std::uint8_t>(ch);
        appendUtf8(s, b >= 0x80 && b < 0xA0 ? high[b - 0x80] : b);
    }
    return s;
}

// ---------------------------------------------------------------------------
// Header parsing

// The layout did not match the version being tried.
struct Mismatch : std::runtime_error {
    using std::runtime_error::runtime_error;
};

class Parser {
public:
    Parser(ByteSource& src, const Version& v) : r_(src), v_(v) {}

    std::string raw() {
        const std::uint32_t n = r_.u32();
        if (n > (64u << 20)) throw Mismatch("implausible string length");
        std::string s(n, '\0');
        if (n) r_.bytes(reinterpret_cast<std::uint8_t*>(s.data()), n);
        return s;
    }
    // A String field (UTF-16 in Unicode builds) as UTF-8.
    std::string text() { return v_.unicode ? utf16le(raw()) : cp1252(raw()); }
    void skipStrings(int n) {
        for (int i = 0; i < n; ++i) raw();
    }
    std::uint8_t u8() { return r_.u8(); }
    std::uint16_t u16() { return r_.u16(); }
    std::uint32_t u32() { return r_.u32(); }
    std::uint64_t u64() { return r_.u64(); }
    void skip(std::size_t n) { r_.skip(n); }
    // An enumeration byte with `count` values.
    unsigned enumeration(unsigned count) {
        const unsigned x = r_.u8();
        if (x >= count) throw Mismatch("enumeration value out of range");
        return x;
    }
    // A Delphi set of `count` elements.
    std::vector<bool> set(std::size_t count) {
        std::size_t bytes = (count + 7) / 8;
        if (bytes == 3) bytes = 4;
        std::vector<bool> bits(count);
        for (std::size_t i = 0; i < bytes; ++i) {
            const std::uint8_t b = r_.u8();
            for (int k = 0; k < 8; ++k)
                if (i * 8 + k < count) bits[i * 8 + k] = (b >> k) & 1;
        }
        return bits;
    }
    void windowsVersions() { skip(20); } // MinVersion, OnlyBelowVersion

    struct Condition {
        std::string components, tasks, languages, check, afterInstall, beforeInstall;
    };
    Condition condition() {
        Condition c;
        c.components = text();
        c.tasks = text();
        c.languages = text();
        c.check = text();
        c.afterInstall = text();
        c.beforeInstall = text();
        return c;
    }

    const Version& v() const { return v_; }

private:
    Reader r_;
    const Version& v_;
};

struct Header {
    std::string appName, appPublisher, appPublisherUrl, appSupportUrl, appUpdatesUrl, baseFilename;
    std::uint32_t languages = 0, messages = 0, permissions = 0, types = 0, components = 0, tasks = 0, dirs = 0,
                  files = 0, dataEntries = 0, icons = 0, inis = 0, registry = 0, deletes = 0, uninstallDeletes = 0,
                  runs = 0, uninstallRuns = 0;
    std::uint32_t slicesPerDisk = 1;
    InnoData::Compression compression = InnoData::Compression::Stored;
    bool encryption = false;
    bool password = false;
};

Header readHeader(Parser& p) {
    const Version& v = p.v();
    Header h;
    h.appName = p.text();
    p.skipStrings(2); // versioned name, app id
    p.raw();          // copyright
    h.appPublisher = p.text();
    h.appPublisherUrl = p.text();
    if (v >= V(5, 1, 13)) p.raw(); // support phone
    h.appSupportUrl = p.text();
    h.appUpdatesUrl = p.text();
    p.skipStrings(3); // version, default dir, default group
    h.baseFilename = p.text();
    if (v < V(5, 2, 5)) p.skipStrings(3); // license, info before, info after
    p.skipStrings(7);                     // uninstall files dir, name, icon, mutex, user name, organisation, serial
    if (v < V(5, 2, 5)) p.raw();          // compiled code
    p.skipStrings(4);                     // readme, contact, comments, modify path
    if (v >= V(5, 3, 8)) p.raw();         // create uninstall registry key
    if (v >= V(5, 3, 10)) p.raw();        // uninstallable
    if (v >= V(5, 5, 0)) p.raw();         // close applications filter
    if (v >= V(5, 5, 6)) p.raw();         // setup mutex
    if (v >= V(5, 6, 1)) p.skipStrings(2); // changes environment, changes associations
    if (v >= V(6, 3, 0)) p.skipStrings(2); // architectures allowed, installed in 64-bit mode
    if (v >= V(6, 4, 2)) p.raw();          // close applications filter excludes
    if (v >= V(5, 2, 5)) p.skipStrings(3); // license, info before, info after
    if (v >= V(5, 2, 1) && v < V(5, 3, 10)) p.raw(); // uninstaller signature
    if (v >= V(5, 2, 5)) p.raw();                    // compiled code
    if (!v.unicode) p.skip(32);                      // lead bytes (set of AnsiChar)

    h.languages = p.u32();
    h.messages = p.u32();
    h.permissions = p.u32();
    h.types = p.u32();
    h.components = p.u32();
    h.tasks = p.u32();
    h.dirs = p.u32();
    h.files = p.u32();
    h.dataEntries = p.u32();
    h.icons = p.u32();
    h.inis = p.u32();
    h.registry = p.u32();
    h.deletes = p.u32();
    h.uninstallDeletes = p.u32();
    h.runs = p.u32();
    h.uninstallRuns = p.u32();

    p.windowsVersions();
    if (v < V(6, 4, 0, 1)) p.skip(8); // back colour 1 and 2
    if (v < V(5, 5, 7)) p.skip(4);    // image back colour
    if (v < V(5, 0, 4)) p.skip(4);    // small image back colour
    if (v >= V(6, 0, 0)) {
        p.enumeration(2); // wizard style
        p.skip(8);        // wizard resize percent x, y
    }
    if (v >= V(5, 5, 7)) p.enumeration(3); // image alpha format
    if (v >= V(6, 4, 0)) p.skip(4 + 44);   // password test; KDF salt, iterations, base nonce
    else if (v >= V(5, 3, 9)) p.skip(20 + 8); // SHA-1 password hash, salt
    else p.skip(16 + 8);                      // MD5 password hash, salt
    p.u64();                                  // extra disk space
    h.slicesPerDisk = p.u32();
    p.enumeration(3);                         // uninstall log mode
    p.enumeration(3);                         // dir exists warning
    p.enumeration(v >= V(5, 3, 7) ? 4 : 3);   // privileges required
    if (v >= V(5, 7, 0)) p.set(2);            // privileges override
    p.enumeration(3);                         // show language dialog
    p.enumeration(3);                         // language detection
    if (v >= V(5, 3, 9)) {
        static constexpr InnoData::Compression m[] = {InnoData::Compression::Stored, InnoData::Compression::Zlib,
                                                      InnoData::Compression::Bzip2, InnoData::Compression::Lzma1,
                                                      InnoData::Compression::Lzma2};
        h.compression = m[p.enumeration(5)];
    } else {
        static constexpr InnoData::Compression m[] = {InnoData::Compression::Stored, InnoData::Compression::Zlib,
                                                      InnoData::Compression::Bzip2, InnoData::Compression::Lzma1};
        h.compression = m[p.enumeration(4)];
    }
    if (v >= V(6, 3, 0)) {
        // architectures are expressions (strings above)
    } else if (v >= V(5, 6, 0)) {
        p.set(5);
        p.set(5);
    } else if (v >= V(5, 1, 0)) {
        p.set(4);
        p.set(4);
    }
    if (v >= V(5, 2, 1) && v < V(5, 3, 10)) p.skip(8); // signed uninstaller size, checksum
    if (v >= V(5, 3, 3)) {
        p.enumeration(3); // disable dir page
        p.enumeration(3); // disable program group page
    }
    if (v >= V(5, 5, 0)) p.u64(); // uninstall display size
    else if (v >= V(5, 3, 6)) p.u32();
    if (v == V(5, 3, 10, 1) || v == V(5, 4, 2, 1) || v == V(5, 5, 0, 1)) p.u8(); // modified builds

    // Options: the elements present in this version, in order.
    enum Opt { Other, PasswordOpt, EncryptionOpt };
    std::vector<Opt> opts;
    auto add = [&](Opt o = Other) { opts.push_back(o); };
    add();                                    // disable startup prompt
    if (v < V(5, 3, 10)) add();               // uninstallable
    add();                                    // create app dir
    if (v < V(5, 3, 3)) add();                // disable dir page
    if (v < V(5, 3, 3)) add();                // disable program group page
    add();                                    // allow no icons
    add();                                    // always restart
    add();                                    // always use personal group
    if (v < V(6, 4, 0, 1)) add(), add(), add(), add(); // window visible, caption, resizable, maximized
    add();                                    // enable dir doesn't exist warning
    add(PasswordOpt);
    add();                                    // allow root directory
    add();                                    // disable finished page
    if (v < V(5, 6, 1)) add();                // changes associations
    if (v < V(5, 3, 8)) add();                // create uninstall reg key
    add();                                    // use previous app dir
    if (v < V(6, 4, 0, 1)) add();             // back color horizontal
    add();                                    // use previous group
    add();                                    // update uninstall log app name
    add();                                    // use previous setup type
    for (int i = 0; i < 6; ++i) add();        // ready memo .. disable ready page
    add(), add();                             // always show dir / group on ready page
    add();                                    // allow UNC path
    add(), add();                             // user info page, use previous user info
    add();                                    // uninstall restart computer
    add();                                    // restart if needed by run
    add();                                    // show tasks tree lines
    add();                                    // allow cancel during install
    add();                                    // wizard image stretch
    add(), add();                             // append default dir / group name
    add(EncryptionOpt);
    if (v >= V(5, 0, 4) && v < V(5, 6, 1)) add(); // changes environment
    if (v >= V(5, 1, 7) && !v.unicode) add();     // show undisplayable languages
    if (v >= V(5, 1, 13)) add();                  // setup logging
    if (v >= V(5, 2, 1)) add();                   // signed uninstaller
    if (v >= V(5, 3, 8)) add();                   // use previous language
    if (v >= V(5, 3, 9)) add();                   // disable welcome page
    if (v >= V(5, 5, 0)) add(), add(), add();     // close / restart applications, allow network drive
    if (v >= V(5, 5, 7)) add();                   // force close applications
    if (v >= V(6, 0, 0)) add(), add(), add();     // app name has consts, use previous privileges, wizard resizable
    if (v >= V(6, 3, 0)) add();                   // uninstall logging
    const std::vector<bool> bits = p.set(opts.size());
    for (std::size_t i = 0; i < opts.size(); ++i) {
        if (opts[i] == PasswordOpt) h.password = bits[i];
        if (opts[i] == EncryptionOpt) h.encryption = bits[i];
    }
    return h;
}

void skipLanguage(Parser& p) {
    const Version& v = p.v();
    p.skipStrings(2); // name, language name
    if (v == V(5, 5, 7, 1)) p.raw();
    p.skipStrings(4); // dialog, title, welcome, copyright fonts
    p.raw();          // data
    p.skipStrings(3); // license, info before, info after
    p.u32();          // language id
    if (!v.unicode) p.u32();             // code page
    else if (v < V(5, 3, 0)) p.u32();
    p.skip(16);                          // font sizes
    if (v == V(5, 5, 7, 1)) p.u32();
    if (v >= V(5, 2, 3)) p.u8();         // right to left
}

void skipType(Parser& p) {
    p.skipStrings(4); // name, description, languages, check
    p.windowsVersions();
    p.set(1);
    p.enumeration(4);
    p.u64();
}

void skipComponent(Parser& p) {
    p.skipStrings(5); // name, description, types, languages, check
    p.u64();          // extra disk space
    p.u32();          // level
    p.u8();           // used
    p.windowsVersions();
    p.set(5);
    p.u64(); // size
}

void skipTask(Parser& p) {
    p.skipStrings(6); // name, description, group description, components, languages, check
    p.u32();          // level
    p.u8();           // used
    p.windowsVersions();
    p.set(5);
}

void skipDirectory(Parser& p) {
    p.raw(); // name
    p.condition();
    p.u32(); // attributes
    p.windowsVersions();
    p.u16(); // permission
    p.set(p.v() >= V(5, 2, 0) ? 5 : 3);
}

struct FileEntry {
    std::string destination;
    Parser::Condition condition;
    std::uint32_t location = 0;
    bool userFile = true;
};

FileEntry readFile(Parser& p) {
    const Version& v = p.v();
    FileEntry f;
    p.raw(); // source
    f.destination = p.text();
    p.raw(); // font name
    if (v >= V(5, 2, 5)) p.raw(); // strong assembly name
    f.condition = p.condition();
    p.windowsVersions();
    f.location = p.u32();
    p.u32(); // attributes
    p.u64(); // external size
    p.u16(); // permission
    std::size_t flags = 24;            // ConfirmOverwrite .. DontVerifyChecksum
    if (v >= V(5, 0, 3)) ++flags;      // UninsNoSharedFilePrompt
    if (v >= V(5, 1, 0)) ++flags;      // CreateAllSubDirs
    if (v >= V(5, 1, 2)) flags += 2;   // 32-bit, 64-bit
    if (v >= V(5, 2, 0)) flags += 3;   // ExternalSizePreset, Set/UnsetNtfsCompression
    if (v >= V(5, 2, 5)) ++flags;      // GacInstall
    p.set(flags);
    f.userFile = p.enumeration(2) == 0; // user file, uninstaller
    return f;
}

void skipIcon(Parser& p) {
    const Version& v = p.v();
    p.skipStrings(6); // name, file, parameters, working dir, icon file, comment
    p.condition();
    if (v >= V(5, 3, 5)) p.raw(); // app user model id
    if (v >= V(6, 1, 0)) p.skip(16);
    p.windowsVersions();
    p.u32(); // icon index
    p.u32(); // show command
    p.enumeration(3);
    p.u16(); // hotkey
    std::size_t flags = 3;
    if (v >= V(5, 0, 3) && v < V(6, 3, 0)) ++flags;
    if (v >= V(5, 4, 2)) ++flags;
    if (v >= V(5, 5, 0)) ++flags;
    if (v >= V(6, 1, 0)) ++flags;
    p.set(flags);
}

void skipIni(Parser& p) {
    p.skipStrings(4); // file, section, key, value
    p.condition();
    p.windowsVersions();
    p.set(5);
}

void skipRegistry(Parser& p) {
    const Version& v = p.v();
    p.skipStrings(3); // key, name, value
    p.condition();
    p.windowsVersions();
    p.u32(); // hive
    p.u16(); // permission
    p.enumeration(v >= V(5, 2, 5) ? 7 : 6);
    p.set(v >= V(5, 1, 0) ? 12 : 10);
}

void skipDelete(Parser& p) {
    p.raw();
    p.condition();
    p.windowsVersions();
    p.enumeration(3);
}

void skipRun(Parser& p) {
    const Version& v = p.v();
    p.skipStrings(5); // name, parameters, working dir, run once id, status message
    if (v >= V(5, 1, 13)) p.raw(); // verb
    p.raw();                       // description
    p.condition();
    p.windowsVersions();
    p.u32(); // show command
    p.enumeration(3);
    std::size_t flags = 7;
    if (v >= V(5, 1, 10)) flags += 2;
    if (v >= V(5, 2, 0)) ++flags;
    if (v >= V(6, 1, 0)) ++flags;
    if (v >= V(6, 3, 0)) ++flags;
    p.set(flags);
}

InnoData readData(Parser& p, const Header& h) {
    const Version& v = p.v();
    InnoData d;
    d.firstSlice = p.u32();
    d.lastSlice = p.u32();
    d.chunkOffset = p.u32();
    d.offset = p.u64();
    d.size = p.u64();
    d.chunkSize = p.u64();
    if (v >= V(6, 4, 0)) {
        d.checksumType = InnoData::Checksum::Sha256;
        d.checksum.resize(32);
    } else if (v >= V(5, 3, 9)) {
        d.checksumType = InnoData::Checksum::Sha1;
        d.checksum.resize(20);
    } else {
        d.checksumType = InnoData::Checksum::Md5;
        d.checksum.resize(16);
    }
    for (auto& b : d.checksum) b = p.u8();
    p.u64();       // time stamp
    p.skip(8);     // file version
    bool callFilter = false, encrypted = false, compressed = false;
    if (v >= V(6, 4, 3)) {
        const auto bits = p.set(5); // version info valid, UTC, call filter, encrypted, compressed
        callFilter = bits[2];
        encrypted = bits[3];
        compressed = bits[4];
    } else {
        // version info valid / not valid, UTC, uninstaller, call filter,
        // touch, encrypted, compressed, [solid break], [sign, sign once]
        std::size_t n = 8;
        if (v >= V(5, 1, 13)) ++n;
        if (v >= V(5, 5, 7) && v < V(6, 3, 0)) n += 2;
        const auto bits = p.set(n);
        callFilter = bits[4];
        encrypted = bits[6];
        compressed = bits[7];
        if (v >= V(6, 3, 0)) p.enumeration(4); // sign mode
    }
    d.compression = compressed ? h.compression : InnoData::Compression::Stored;
    d.encrypted = encrypted;
    if (callFilter) {
        if (v < V(5, 2, 0)) d.filter = InnoData::Filter::Call4108;
        else if (v < V(5, 3, 9)) d.filter = InnoData::Filter::Call5200;
        else d.filter = InnoData::Filter::Call5309;
    }
    return d;
}

// ---------------------------------------------------------------------------
// Header blocks: a CRC-protected header, then 4 KiB sub-blocks each with
// its own CRC, LZMA-compressed as a whole.

std::vector<std::uint8_t> readBlock(FileSource& file) {
    Reader r(file);
    const std::uint32_t crc = r.u32();
    std::uint8_t hdr[5];
    r.bytes(hdr, 5);
    if (crc32(hdr) != crc) throw Mismatch("setup header block CRC mismatch");
    const std::uint32_t stored = hdr[0] | (hdr[1] << 8) | (hdr[2] << 16) | (static_cast<std::uint32_t>(hdr[3]) << 24);
    const bool compressed = hdr[4] != 0;
    std::vector<std::uint8_t> data;
    std::uint32_t left = stored;
    std::uint8_t buf[4096];
    while (left > 0) {
        if (left < 5) throw InnoError("corrupt setup header block");
        const std::uint32_t subCrc = r.u32();
        left -= 4;
        const std::uint32_t n = std::min<std::uint32_t>(left, 4096);
        r.bytes(buf, n);
        if (crc32(std::span<const std::uint8_t>(buf, n)) != subCrc) throw InnoError("setup header CRC mismatch");
        data.insert(data.end(), buf, buf + n);
        left -= n;
    }
    if (!compressed) return data;
    SpanSource in(data);
    auto lzma = makeLzma1Decoder(in);
    try {
        return readAll(*lzma);
    } catch (const LzmaError&) {
        throw InnoError("corrupt setup header (LZMA)");
    }
}

// ---------------------------------------------------------------------------
// GOG Galaxy multi-part files (docs/formats/INNO.md "GOG Galaxy files")

// The arguments of a call such as "before_install('md5', 'name', 2)".
std::vector<std::string> callArguments(const std::string& code, const std::string& name) {
    std::vector<std::string> args;
    std::size_t p = code.find_first_not_of(" \t\r\n");
    if (p == std::string::npos || code.compare(p, name.size(), name) != 0) return {};
    p += name.size();
    p = code.find_first_not_of(" \t\r\n", p);
    if (p == std::string::npos || code[p] != '(') return {};
    ++p;
    for (;;) {
        p = code.find_first_not_of(" \t\r\n", p);
        if (p == std::string::npos) return {};
        std::string arg;
        if (code[p] == '\'') {
            ++p;
            for (;;) {
                const std::size_t q = code.find('\'', p);
                if (q == std::string::npos) return {};
                arg += code.substr(p, q - p);
                p = q + 1;
                if (p < code.size() && code[p] == '\'') {
                    arg += '\'';
                    ++p;
                } else {
                    break;
                }
            }
        } else {
            const std::size_t q = code.find_first_of(" \t\r\n,)", p);
            if (q == std::string::npos) return {};
            arg = code.substr(p, q - p);
            p = q;
        }
        args.push_back(arg);
        p = code.find_first_not_of(" \t\r\n", p);
        if (p == std::string::npos) return {};
        if (code[p] == ')') break;
        if (code[p] != ',') return {};
        ++p;
    }
    return args;
}

std::optional<std::uint64_t> number(const std::string& s) {
    if (s.empty() || s.size() > 19) return std::nullopt;
    std::uint64_t x = 0;
    for (const char c : s) {
        if (c < '0' || c > '9') return std::nullopt;
        x = x * 10 + static_cast<unsigned>(c - '0');
    }
    return x;
}

void mergeGalaxyParts(std::vector<FileEntry>& entries, std::vector<InnoData>& data, std::vector<InnoFile>& files) {
    InnoFile* start = nullptr;
    std::uint64_t remaining = 0;
    for (FileEntry& e : entries) {
        if (!e.userFile) continue;
        auto first = callArguments(e.condition.beforeInstall, "before_install");
        if (first.empty()) first = callArguments(e.condition.beforeInstall, "before_install_dependency");
        auto part = callArguments(e.condition.afterInstall, "after_install");
        if (part.empty()) part = callArguments(e.condition.afterInstall, "after_install_dependency");
        if (!first.empty()) {
            InnoFile f;
            f.destination = first.size() >= 2 && !first[1].empty() ? first[1] : e.destination;
            // A name without a constant is relative to the part's folder.
            if (!f.destination.empty() && f.destination.front() != '{') {
                const std::size_t slash = e.destination.rfind('\\');
                if (slash != std::string::npos) f.destination = e.destination.substr(0, slash + 1) + f.destination;
            }
            files.push_back(f);
            start = &files.back();
            remaining = first.size() >= 3 ? number(first[2]).value_or(1) : 1;
            if (remaining == 0) remaining = 1;
        }
        if (!part.empty() && start && remaining > 0 && part.size() >= 3 && e.location < data.size()) {
            const auto inflated = number(part[2]);
            if (!inflated) throw InnoError("malformed GOG Galaxy part size: " + part[2]);
            InnoData& d = data[e.location];
            d.galaxyPart = true;
            d.inflatedSize = *inflated;
            start->locations.push_back(e.location);
            start->size += *inflated;
            if (--remaining == 0) start = nullptr;
            continue;
        }
        start = nullptr;
        remaining = 0;
        if (!first.empty()) {
            // A start entry without part information: an ordinary file.
            files.back().locations.push_back(e.location);
            files.back().size = e.location < data.size() ? data[e.location].size : 0;
            continue;
        }
        InnoFile f;
        f.destination = e.destination;
        if (e.location < data.size()) {
            f.locations.push_back(e.location);
            f.size = data[e.location].size;
        }
        files.push_back(f);
    }
}

bool isGog(const Header& h) {
    auto has = [](const std::string& s, const char* needle) {
        std::string lower = s;
        for (char& c : lower)
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        return lower.find(needle) != std::string::npos;
    };
    return has(h.appPublisher, "gog.com") || has(h.appPublisherUrl, "www.gog.com") ||
           has(h.appSupportUrl, "www.gog.com") || has(h.appUpdatesUrl, "www.gog.com");
}

// ---------------------------------------------------------------------------
// The setup loader's offset table

struct Offsets {
    std::uint64_t header = 0; // setup-0 data (version id and header blocks)
    std::uint64_t data = 0;   // embedded setup-1 data, 0 if in external slices
};

// The end of the PE image (the largest section end), to read the
// resources without loading the appended setup data.
std::uint64_t peImageEnd(FileSource& f) {
    std::uint8_t h[4096];
    const std::size_t n = static_cast<std::size_t>(std::min<std::uint64_t>(f.size(), sizeof h));
    f.setRange(0, n);
    f.readExact(h, n);
    auto u16 = [&](std::size_t o) -> unsigned { return o + 2 <= n ? h[o] | (h[o + 1] << 8) : 0; };
    auto u32 = [&](std::size_t o) -> std::uint32_t { return o + 4 <= n ? u16(o) | (u16(o + 2) << 16) : 0; };
    if (n < 0x40 || h[0] != 'M' || h[1] != 'Z') return 0;
    const std::uint32_t pe = u32(0x3C);
    if (pe + 24 > n || u32(pe) != 0x00004550) return 0;
    const unsigned sections = u16(pe + 6);
    const std::size_t table = pe + 24 + u16(pe + 20);
    std::uint64_t end = 0;
    for (unsigned i = 0; i < sections; ++i) {
        const std::size_t s = table + i * 40u;
        end = std::max<std::uint64_t>(end, static_cast<std::uint64_t>(u32(s + 20)) + u32(s + 16));
    }
    return end;
}

std::optional<Offsets> parseOffsetTable(std::span<const std::uint8_t> t) {
    static const std::uint8_t kMagics[][12] = {
        {'r', 'D', 'l', 'P', 't', 'S', '0', '2', 0x87, 'e', 'V', 'x'},
        {'r', 'D', 'l', 'P', 't', 'S', '0', '4', 0x87, 'e', 'V', 'x'},
        {'r', 'D', 'l', 'P', 't', 'S', '0', '5', 0x87, 'e', 'V', 'x'},
        {'r', 'D', 'l', 'P', 't', 'S', '0', '6', 0x87, 'e', 'V', 'x'},
        {'r', 'D', 'l', 'P', 't', 'S', '0', '7', 0x87, 'e', 'V', 'x'},
        {'r', 'D', 'l', 'P', 't', 'S', 0xcd, 0xe6, 0xd7, '{', 0x0b, '*'},
        {'n', 'S', '5', 'W', '7', 'd', 'T', 0x83, 0xaa, 0x1b, 0x0f, 'j'},
    };
    if (t.size() < 12) return std::nullopt;
    int kind = -1;
    for (int i = 0; i < 7; ++i)
        if (std::memcmp(t.data(), kMagics[i], 12) == 0) kind = i;
    if (kind < 0) return std::nullopt;
    auto u32 = [&](std::size_t o) -> std::uint64_t {
        if (o + 4 > t.size()) throw InnoError("truncated setup loader table");
        return t[o] | (t[o + 1] << 8) | (t[o + 2] << 16) | (static_cast<std::uint32_t>(t[o + 3]) << 24);
    };
    auto u64 = [&](std::size_t o) { return u32(o) | (u32(o + 4) << 32); };
    Offsets out;
    if (kind <= 1) throw InnoError("Inno Setup installers older than version 4.0.3 are not supported");
    if (kind <= 4) {
        // magic, total size, exe offset, [compressed size < 4.1.6], exe size,
        // exe CRC, header offset, data offset
        std::size_t o = 12 + 4 + 4 + (kind <= 3 ? 4 : 0) + 4 + 4;
        out.header = u32(o);
        out.data = u32(o + 4);
        return out;
    }
    const std::uint64_t revision = u32(12);
    if (revision == 1) {
        // revision, total size, exe offset, exe size, exe CRC, header, data
        out.header = u32(32);
        out.data = u32(36);
    } else if (revision == 2) {
        // revision, total size (64), exe offset (64), exe size, exe CRC,
        // header (64), data (64)
        out.header = u64(40);
        out.data = u64(48);
    } else {
        throw InnoError("unknown Inno Setup loader revision " + std::to_string(revision));
    }
    return out;
}

std::optional<Offsets> findOffsets(FileSource& f) {
    // Before 5.1.5: a pointer at 0x30 ("Inno", offset, ~offset).
    if (f.size() >= 0x3C) {
        std::uint8_t h[12];
        f.setRange(0x30, 12);
        f.readExact(h, 12);
        auto le = [&](int o) {
            return static_cast<std::uint32_t>(h[o] | (h[o + 1] << 8) | (h[o + 2] << 16) |
                                              (static_cast<std::uint32_t>(h[o + 3]) << 24));
        };
        if (std::memcmp(h, "Inno", 4) == 0 && le(4) == ~le(8) && le(4) + 64u <= f.size()) {
            std::vector<std::uint8_t> t(64);
            f.setRange(le(4), 64);
            f.readExact(t.data(), 64);
            if (auto o = parseOffsetTable(t)) return o;
        }
    }
    // 5.1.5 and later: resource RT_RCDATA #11111.
    const std::uint64_t end = peImageEnd(f);
    if (end == 0 || end > f.size()) return std::nullopt;
    std::vector<std::uint8_t> image(static_cast<std::size_t>(end));
    f.setRange(0, end);
    f.readExact(image.data(), image.size());
    try {
        const auto res = loadResources(image, 10);
        if (const auto it = res.find(11111); it != res.end()) return parseOffsetTable(it->second);
    } catch (const PeError&) {
    }
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// Data: slices and chunks

// Reads consecutive slices as one stream (a chunk may continue in the next
// setup-N.bin).
class SliceSource : public ByteSource {
public:
    SliceSource(const fs::path& setup, std::uint64_t embeddedOffset, std::vector<std::string> baseNames,
                std::uint32_t slicesPerDisk)
        : setup_(setup), embedded_(embeddedOffset), bases_(std::move(baseNames)), perDisk_(slicesPerDisk) {}

    void seek(std::uint32_t slice, std::uint64_t offset) {
        open(slice);
        const std::uint64_t pos = embedded_ + offset;
        if (pos > end_) throw InnoError("chunk outside its slice");
        file_->setRange(pos, end_ - pos);
    }

    std::size_t read(std::uint8_t* out, std::size_t n) override {
        std::size_t got = file_ ? file_->read(out, n) : 0;
        if (got == 0 && n > 0 && embedded_ == 0) {
            open(slice_ + 1);
            got = file_->read(out, n);
        }
        return got;
    }

private:
    std::string sliceName(const std::string& base, std::uint32_t slice) const {
        if (perDisk_ <= 1) return base + "-" + std::to_string(slice + 1) + ".bin";
        return base + "-" + std::to_string(slice / perDisk_ + 1) + static_cast<char>('a' + slice % perDisk_) + ".bin";
    }

    void open(std::uint32_t slice) {
        if (file_ && slice == slice_) return;
        if (embedded_ != 0) {
            if (slice != 0) throw InnoError("an installer with embedded data has a single slice");
            file_ = std::make_unique<FileSource>(setup_);
            slice_ = 0;
            end_ = file_->size();
            return;
        }
        const fs::path dir = setup_.parent_path();
        for (const std::string& base : bases_) {
            if (base.empty()) continue;
            const fs::path p = findGameFile(dir, sliceName(base, slice));
            std::error_code ec;
            if (!fs::exists(p, ec)) continue;
            auto f = std::make_unique<FileSource>(p);
            std::uint8_t magic[8];
            Reader r(*f);
            r.bytes(magic, 8);
            if (std::memcmp(magic, "Rar!", 4) == 0)
                throw InnoError("this installer keeps its files in a RAR archive (" + p.filename().string() +
                                "), an older GOG format that OpenDay cannot read; install the game with it instead");
            if (std::memcmp(magic, "idska32\x1a", 8) != 0 && std::memcmp(magic, "idska16\x1a", 8) != 0)
                throw InnoError("not an Inno Setup data file: " + p.filename().string());
            const std::uint64_t size = r.u32();
            if (size > f->size() || size < 12) throw InnoError("corrupt Inno Setup data file: " + p.filename().string());
            file_ = std::move(f);
            file_->setRange(12, size - 12);
            slice_ = slice;
            end_ = size;
            return;
        }
        throw InnoError("missing installer data file " + sliceName(bases_.front(), slice) +
                        " (it must be next to the installer)");
    }

    fs::path setup_;
    std::uint64_t embedded_;
    std::vector<std::string> bases_;
    std::uint32_t perDisk_;
    std::unique_ptr<FileSource> file_;
    std::uint32_t slice_ = 0;
    std::uint64_t end_ = 0;
};

// A zlib stream (2-byte header, Deflate data, Adler-32), inflated at once.
std::vector<std::uint8_t> inflateZlib(std::span<const std::uint8_t> z, std::size_t expected) {
    if (z.size() < 2 || (z[0] & 0x0F) != 8 || ((z[0] << 8) | z[1]) % 31 != 0)
        throw InnoError("corrupt zlib stream");
    try {
        return inflate(z.subspan(2), expected);
    } catch (const InflateError& e) {
        throw InnoError(std::string("corrupt zlib stream: ") + e.what());
    }
}

// A zlib chunk: read and inflated when first used.
class ZlibChunk : public ByteSource {
public:
    ZlibChunk(ByteSource& in, std::size_t expected) : in_(in), expected_(expected) {}
    std::size_t read(std::uint8_t* out, std::size_t n) override {
        if (!loaded_) {
            const std::vector<std::uint8_t> z = readAll(in_);
            data_ = inflateZlib(z, expected_);
            loaded_ = true;
        }
        SpanSource s(std::span<const std::uint8_t>(data_).subspan(pos_));
        const std::size_t got = s.read(out, n);
        pos_ += got;
        return got;
    }

private:
    ByteSource& in_;
    std::size_t expected_;
    std::vector<std::uint8_t> data_;
    std::size_t pos_ = 0;
    bool loaded_ = false;
};

// The x86 call/jump address filters (docs/formats/INNO.md "Call filter").
void unfilter4108(std::vector<std::uint8_t>& d) {
    std::uint32_t addr = 0, offset = 5;
    int left = 0;
    for (auto& b : d) {
        if (left == 0) {
            if (b == 0xE8 || b == 0xE9) {
                addr = ~offset + 1;
                left = 4;
            }
        } else {
            addr += b;
            b = static_cast<std::uint8_t>(addr);
            addr >>= 8;
            --left;
        }
        ++offset;
    }
}

void unfilter5200(std::vector<std::uint8_t>& d, bool flipHighByte) {
    std::size_t i = 0;
    const std::size_t n = d.size();
    while (i < n) {
        const std::uint8_t b = d[i++];
        if (b != 0xE8 && b != 0xE9) continue;
        if (0x10000 - ((i - 1) % 0x10000) < 5) continue; // no address across a 64 KiB block
        if (i + 4 > n) break;
        std::uint8_t* a = d.data() + i;
        i += 4;
        if (a[3] != 0x00 && a[3] != 0xFF) continue;
        std::uint32_t rel = a[0] | (a[1] << 8) | (a[2] << 16);
        rel -= static_cast<std::uint32_t>(i) & 0xFFFFFF;
        a[0] = static_cast<std::uint8_t>(rel);
        a[1] = static_cast<std::uint8_t>(rel >> 8);
        a[2] = static_cast<std::uint8_t>(rel >> 16);
        if (flipHighByte && (rel & 0x800000)) a[3] = static_cast<std::uint8_t>(~a[3]);
    }
}

// Incremental checksum of one stored file.
class Checker {
public:
    explicit Checker(InnoData::Checksum type) : type_(type) {}
    void update(std::span<const std::uint8_t> d) {
        switch (type_) {
        case InnoData::Checksum::Md5: md5_.update(d); break;
        case InnoData::Checksum::Sha1: sha1_.update(d); break;
        case InnoData::Checksum::Sha256: sha256_.update(d); break;
        default: break;
        }
    }
    bool matches(const std::vector<std::uint8_t>& expected) {
        auto eq = [&](const auto& digest) {
            return expected.size() == digest.size() && std::equal(digest.begin(), digest.end(), expected.begin());
        };
        switch (type_) {
        case InnoData::Checksum::Md5: return eq(md5_.finish());
        case InnoData::Checksum::Sha1: return eq(sha1_.finish());
        case InnoData::Checksum::Sha256: return eq(sha256_.finish());
        default: return true;
        }
    }

private:
    InnoData::Checksum type_;
    Md5 md5_;
    Sha1 sha1_;
    Sha256 sha256_;
};

} // namespace

bool InnoSetup::probe(const fs::path& path) {
    try {
        FileSource f(path);
        return findOffsets(f).has_value();
    } catch (const std::exception&) {
        return false;
    }
}

InnoSetup InnoSetup::open(const fs::path& path) {
    FileSource file(path);
    const std::optional<Offsets> offsets = findOffsets(file);
    if (!offsets) throw InnoError("not an Inno Setup installer: " + path.filename().string());
    if (offsets->header + 64 > file.size()) throw InnoError("truncated installer: " + path.filename().string());

    std::uint8_t id[64];
    file.setRange(offsets->header, 64);
    file.readExact(id, 64);
    const Version listed = parseVersionId(id);
    if (listed < V(5, 0, 0))
        throw InnoError("Inno Setup " + versionText(listed.value, listed.unicode) + " installers are not supported");
    if (listed.value > kNewestSupported)
        throw InnoError("Inno Setup " + versionText(listed.value, listed.unicode) +
                        " installers are not supported yet (newest supported: 6.4.3)");

    std::string firstError;
    for (const Ver candidate : candidateVersions(listed.value)) {
        Version v = listed;
        v.value = candidate;
        try {
            file.setRange(offsets->header + 64, file.size() - offsets->header - 64);
            const std::vector<std::uint8_t> block1 = readBlock(file);
            const std::vector<std::uint8_t> block2 = readBlock(file);

            SpanSource s1(block1);
            Parser p(s1, v);
            const Header h = readHeader(p);
            for (std::uint32_t i = 0; i < h.languages; ++i) skipLanguage(p);
            for (std::uint32_t i = 0; i < h.messages; ++i) {
                p.skipStrings(2);
                p.u32();
            }
            for (std::uint32_t i = 0; i < h.permissions; ++i) p.raw();
            for (std::uint32_t i = 0; i < h.types; ++i) skipType(p);
            for (std::uint32_t i = 0; i < h.components; ++i) skipComponent(p);
            for (std::uint32_t i = 0; i < h.tasks; ++i) skipTask(p);
            for (std::uint32_t i = 0; i < h.dirs; ++i) skipDirectory(p);
            std::vector<FileEntry> entries;
            for (std::uint32_t i = 0; i < h.files; ++i) entries.push_back(readFile(p));
            for (std::uint32_t i = 0; i < h.icons; ++i) skipIcon(p);
            for (std::uint32_t i = 0; i < h.inis; ++i) skipIni(p);
            for (std::uint32_t i = 0; i < h.registry; ++i) skipRegistry(p);
            for (std::uint32_t i = 0; i < h.deletes + h.uninstallDeletes; ++i) skipDelete(p);
            for (std::uint32_t i = 0; i < h.runs + h.uninstallRuns; ++i) skipRun(p);
            // Wizard images (large, small), decompressor and decryption DLLs.
            for (int k = 0; k < 2; ++k) p.skipStrings(v >= V(5, 6, 0) ? static_cast<int>(p.u32()) : 1);
            if (h.compression == InnoData::Compression::Bzip2 || h.compression == InnoData::Compression::Zlib) p.raw();
            if (h.encryption && v < V(6, 4, 0)) p.raw();
            std::uint8_t extra;
            if (s1.read(&extra, 1) != 0) throw Mismatch("data after the setup header");

            SpanSource s2(block2);
            Parser p2(s2, v);
            std::vector<InnoData> data;
            for (std::uint32_t i = 0; i < h.dataEntries; ++i) data.push_back(readData(p2, h));
            if (s2.read(&extra, 1) != 0) throw Mismatch("data after the file locations");
            for (const FileEntry& e : entries)
                if (e.userFile && e.location >= data.size() && e.location != 0xFFFFFFFFu)
                    throw Mismatch("file location out of range");

            InnoSetup setup;
            setup.path_ = path;
            setup.version_ = versionText(v.value, v.unicode);
            setup.appName_ = h.appName;
            setup.baseFilename_ = h.baseFilename;
            setup.dataOffset_ = offsets->data;
            setup.slicesPerDisk_ = h.slicesPerDisk ? h.slicesPerDisk : 1;
            if (isGog(h)) {
                mergeGalaxyParts(entries, data, setup.files_);
            } else {
                for (const FileEntry& e : entries) {
                    if (!e.userFile || e.location >= data.size()) continue;
                    InnoFile f;
                    f.destination = e.destination;
                    f.locations.push_back(e.location);
                    f.size = data[e.location].size;
                    setup.files_.push_back(f);
                }
            }
            setup.data_ = std::move(data);
            return setup;
        } catch (const Mismatch& e) {
            if (firstError.empty()) firstError = e.what();
        } catch (const StreamError& e) {
            if (firstError.empty()) firstError = e.what();
        }
    }
    throw InnoError("cannot read the Inno Setup " + versionText(listed.value, listed.unicode) + " header (" +
                    firstError + ")");
}

void InnoSetup::extract(const std::vector<std::size_t>& files, const Sink& sink, const Progress& progress) const {
    // Which files receive each stored file, and at which offset.
    struct Target {
        std::size_t file;
        std::uint64_t offset;
    };
    std::map<std::uint32_t, std::vector<Target>> targets;
    for (const std::size_t fi : files) {
        if (fi >= files_.size()) throw InnoError("file index out of range");
        std::uint64_t off = 0;
        for (const std::uint32_t loc : files_[fi].locations) {
            targets[loc].push_back({fi, off});
            off += data_[loc].galaxyPart ? data_[loc].inflatedSize : data_[loc].size;
        }
    }

    // Chunks in file order; the stored files of each by offset.
    struct ChunkKey {
        std::uint32_t slice;
        std::uint64_t offset;
        bool operator<(const ChunkKey& o) const { return slice != o.slice ? slice < o.slice : offset < o.offset; }
    };
    std::map<ChunkKey, std::vector<std::uint32_t>> chunks;
    std::uint64_t total = 0;
    for (const auto& [loc, t] : targets) {
        const InnoData& d = data_[loc];
        if (d.encrypted) throw InnoError("the installer is encrypted (password protected)");
        chunks[{d.firstSlice, d.chunkOffset}].push_back(loc);
    }
    for (auto& [key, locs] : chunks) {
        std::sort(locs.begin(), locs.end(), [&](std::uint32_t a, std::uint32_t b) { return data_[a].offset < data_[b].offset; });
        const InnoData& last = data_[locs.back()];
        total += last.offset + last.size;
    }

    const std::string stem = path_.stem().string();
    SliceSource slices(path_, dataOffset_, {stem, baseFilename_, "setup"}, slicesPerDisk_);
    std::uint64_t done = 0;
    std::vector<std::uint8_t> buf(1 << 20);
    for (const auto& [key, locs] : chunks) {
        const InnoData& first = data_[locs.front()];
        slices.seek(key.slice, key.offset);
        std::uint8_t magic[4];
        slices.readExact(magic, 4, "installer data");
        if (std::memcmp(magic, "zlb\x1a", 4) != 0) throw InnoError("corrupt installer data (chunk magic)");
        LimitedSource raw(slices, first.chunkSize);
        const InnoData& lastEntry = data_[locs.back()];
        std::unique_ptr<ByteSource> decoder;
        ByteSource* in = &raw;
        switch (first.compression) {
        case InnoData::Compression::Stored: break;
        case InnoData::Compression::Zlib:
            decoder = std::make_unique<ZlibChunk>(raw, static_cast<std::size_t>(lastEntry.offset + lastEntry.size));
            in = decoder.get();
            break;
        case InnoData::Compression::Lzma1:
            decoder = makeLzma1Decoder(raw);
            in = decoder.get();
            break;
        case InnoData::Compression::Lzma2:
            decoder = makeLzma2Decoder(raw);
            in = decoder.get();
            break;
        case InnoData::Compression::Bzip2: throw InnoError("bzip2-compressed installers are not supported");
        }

        std::uint64_t pos = 0;
        try {
            for (const std::uint32_t loc : locs) {
                const InnoData& d = data_[loc];
                if (d.offset < pos) throw InnoError("overlapping files in the installer data");
                in->skip(d.offset - pos, "installer data");
                done += d.offset - pos;
                pos = d.offset;
                const std::vector<Target>& to = targets.at(loc);
                Checker check(d.checksumType);
                const bool whole = d.filter != InnoData::Filter::None || d.galaxyPart;
                std::vector<std::uint8_t> all;
                std::uint64_t left = d.size, at = 0;
                while (left > 0) {
                    const std::size_t n = static_cast<std::size_t>(std::min<std::uint64_t>(left, buf.size()));
                    in->readExact(buf.data(), n, "installer data");
                    if (whole) {
                        all.insert(all.end(), buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(n));
                    } else {
                        check.update(std::span<const std::uint8_t>(buf.data(), n));
                        for (const Target& t : to) sink(t.file, t.offset + at, buf.data(), n);
                    }
                    at += n;
                    left -= n;
                    done += n;
                    if (progress) progress(done, total);
                }
                pos += d.size;
                if (whole) {
                    if (d.filter == InnoData::Filter::Call4108) unfilter4108(all);
                    else if (d.filter != InnoData::Filter::None) unfilter5200(all, d.filter == InnoData::Filter::Call5309);
                    check.update(all);
                }
                if (!check.matches(d.checksum)) throw InnoError("checksum mismatch in the installer data");
                if (whole) {
                    if (d.galaxyPart) all = inflateZlib(all, static_cast<std::size_t>(d.inflatedSize));
                    if (d.galaxyPart && all.size() != d.inflatedSize) throw InnoError("GOG Galaxy part has the wrong size");
                    for (const Target& t : to) sink(t.file, t.offset, all.data(), all.size());
                }
            }
        } catch (const LzmaError& e) {
            throw InnoError(std::string("corrupt installer data: ") + e.what());
        } catch (const StreamError& e) {
            throw InnoError(std::string("installer data: ") + e.what());
        }
    }
}

} // namespace openday::formats
