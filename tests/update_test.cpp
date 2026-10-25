// Updater building blocks on synthetic data: SHA-256, the JSON reader, the
// release information, version comparison, and gzip / tar reading (archives
// made by this test with stored DEFLATE blocks).

#include "formats/inflate.hpp"
#include "formats/json.hpp"
#include "formats/sha256.hpp"
#include "formats/tar.hpp"
#include "update/release.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace openday;

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

std::vector<std::uint8_t> bytes(const std::string& s) { return {s.begin(), s.end()}; }

void testSha256() {
    CHECK(formats::sha256Hex({}) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(formats::sha256Hex(bytes("abc")) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(formats::sha256Hex(bytes("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")) ==
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    // A million 'a', fed in uneven pieces.
    formats::Sha256 h;
    const std::vector<std::uint8_t> a(1000, 'a');
    for (std::size_t left = 1000000, step = 1; left > 0; step += 37) {
        const std::size_t n = std::min(left, step % 1000 + 1);
        h.update(std::span(a).first(n));
        left -= n;
    }
    CHECK(h.finishHex() == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    formats::Sha256 pieces;
    const std::vector<std::uint8_t> abc = bytes("abc");
    pieces.update(std::span(abc).first(1));
    pieces.update(std::span(abc).subspan(1));
    CHECK(pieces.finishHex() == formats::sha256Hex(abc));
}

void testJson() {
    using formats::JsonValue;
    const JsonValue v = JsonValue::parse(
        R"( {"a": [1, -2.5e1, true, false, null], "s": "x\"\\\/\né😀", "o": {"k": "v"}, "e": {}, "l": []} )");
    CHECK(v.type() == JsonValue::Type::Object);
    CHECK(v["a"].items().size() == 5);
    CHECK(v["a"].items()[0].number() == 1);
    CHECK(v["a"].items()[1].number() == -25);
    CHECK(v["a"].items()[2].boolean() && !v["a"].items()[3].boolean(true));
    CHECK(v["a"].items()[4].isNull());
    CHECK(v["s"].string() == "x\"\\/\n\xC3\xA9\xF0\x9F\x98\x80");
    CHECK(v["o"]["k"].string() == "v");
    CHECK(v["missing"].isNull() && v["missing"]["deeper"].isNull());
    CHECK(v["e"].type() == JsonValue::Type::Object && v["l"].items().empty());
    CHECK(v["s"].number(7) == 7); // wrong type: fallback
    CHECK(throws([] { JsonValue::parse("{\"a\": }"); }));
    CHECK(throws([] { JsonValue::parse("[1, 2"); }));
    CHECK(throws([] { JsonValue::parse("\"open"); }));
    CHECK(throws([] { JsonValue::parse("{} x"); }));
    CHECK(throws([] { JsonValue::parse("tru"); }));
    CHECK(throws([] { JsonValue::parse(std::string(100, '[') + std::string(100, ']')); }));
}

void testRelease() {
    const std::string json = R"({
        "tag_name": "v0.2.0", "name": "OpenDay v0.2.0", "draft": false, "prerelease": false,
        "html_url": "https://github.com/Tenax84/open-day/releases/tag/v0.2.0", "body": "notes",
        "assets": [
            {"name": "OpenDay-0.2.0-windows-x64.exe", "size": 1234,
             "browser_download_url": "https://example.invalid/w.exe",
             "digest": "sha256:ABCDEF0123456789abcdef0123456789abcdef0123456789abcdef0123456789"},
            {"name": "OpenDay-0.2.0-linux-x64.tar.gz", "size": 99, "browser_download_url": "https://example.invalid/l.tgz",
             "digest": null},
            {"name": "broken"}
        ]})";
    std::string error;
    const auto r = update::parseRelease(json, error);
    CHECK(r.has_value());
    if (!r) return;
    CHECK(r->tag == "v0.2.0" && r->version == "0.2.0");
    CHECK(r->pageUrl == "https://github.com/Tenax84/open-day/releases/tag/v0.2.0");
    CHECK(r->assets.size() == 2); // the asset without a URL is dropped
    const update::Asset* w = r->asset("OpenDay-0.2.0-windows-x64.exe");
    CHECK(w && w->size == 1234 && w->sha256 == "abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789");
    const update::Asset* l = r->asset(update::packageName("0.2.0", "linux", "x64"));
    CHECK(l && l->sha256.empty());
    CHECK(!update::parseRelease(R"({"message": "Not Found"})", error) && error == "GitHub: Not Found");
    CHECK(!update::parseRelease("not json", error));

    CHECK(update::packageName("v1.2.3", "macos", "arm64") == "OpenDay-1.2.3-macos-arm64.dmg");
    CHECK(update::packageName("1.2.3", "windows", "x64") == "OpenDay-1.2.3-windows-x64.exe");
    CHECK(update::latestReleaseUrl() == "https://api.github.com/repos/Tenax84/open-day/releases/latest");
}

void testVersions() {
    using update::compareVersions;
    CHECK(compareVersions("0.1.1", "0.1.1") == 0);
    CHECK(compareVersions("v0.1.2", "0.1.1") > 0);
    CHECK(compareVersions("0.1.9", "0.1.10") < 0);
    CHECK(compareVersions("1.0", "1.0.0") == 0);
    CHECK(compareVersions("1.0.1", "1.0") > 0);
    CHECK(compareVersions("2.0.0", "10.0.0") < 0);
    CHECK(compareVersions("1.0.0-rc1", "1.0.0") < 0);
    CHECK(compareVersions("1.0.0-rc2", "1.0.0-rc1") > 0);
    CHECK(compareVersions("1.0.0+build5", "1.0.0") == 0);
    CHECK(compareVersions("0.0.0-abc1234", "0.1.1") < 0);
}

void testSums() {
    const std::string sums = "0000000000000000000000000000000000000000000000000000000000000000  other.exe\r\n"
                             "ABCDEF0123456789abcdef0123456789abcdef0123456789abcdef0123456789 *OpenDay-1.0-linux-x64.tar.gz\n"
                             "garbage line\n";
    CHECK(update::sha256FromSums(sums, "OpenDay-1.0-linux-x64.tar.gz") ==
          "abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789");
    CHECK(update::sha256FromSums(sums, "other.exe") == std::string(64, '0'));
    CHECK(update::sha256FromSums(sums, "missing").empty());
}

// --- gzip / tar --------------------------------------------------------------------------

void put32(std::vector<std::uint8_t>& v, std::uint32_t x) {
    for (int i = 0; i < 4; ++i) v.push_back(static_cast<std::uint8_t>(x >> (8 * i)));
}

// One gzip member holding `data` in stored DEFLATE blocks.
std::vector<std::uint8_t> gzipStored(const std::vector<std::uint8_t>& data, bool withName) {
    std::vector<std::uint8_t> out = {0x1F, 0x8B, 8, static_cast<std::uint8_t>(withName ? 8 : 0), 0, 0, 0, 0, 0, 3};
    if (withName) {
        for (char c : std::string("a.tar")) out.push_back(static_cast<std::uint8_t>(c));
        out.push_back(0);
    }
    std::size_t pos = 0;
    do {
        const std::size_t n = std::min<std::size_t>(data.size() - pos, 65535);
        out.push_back(pos + n == data.size() ? 1 : 0); // BFINAL, BTYPE 00
        out.push_back(static_cast<std::uint8_t>(n));
        out.push_back(static_cast<std::uint8_t>(n >> 8));
        out.push_back(static_cast<std::uint8_t>(~n));
        out.push_back(static_cast<std::uint8_t>(~n >> 8));
        out.insert(out.end(), data.begin() + static_cast<std::ptrdiff_t>(pos), data.begin() + static_cast<std::ptrdiff_t>(pos + n));
        pos += n;
    } while (pos < data.size());
    put32(out, formats::crc32(data));
    put32(out, static_cast<std::uint32_t>(data.size()));
    return out;
}

void octal(std::uint8_t* p, std::size_t n, std::uint64_t v) {
    std::snprintf(reinterpret_cast<char*>(p), n, "%0*llo", static_cast<int>(n - 1), static_cast<unsigned long long>(v));
}

void tarMember(std::vector<std::uint8_t>& tar, const std::string& name, char type, const std::string& body,
               unsigned mode = 0644, const std::string& prefix = {}) {
    std::uint8_t h[512] = {};
    std::memcpy(h, name.data(), std::min<std::size_t>(name.size(), 100));
    octal(h + 100, 8, mode);
    octal(h + 108, 8, 0);
    octal(h + 116, 8, 0);
    octal(h + 124, 12, body.size());
    octal(h + 136, 12, 0);
    h[156] = static_cast<std::uint8_t>(type);
    std::memcpy(h + 257, "ustar", 6);
    std::memcpy(h + 263, "00", 2);
    std::memcpy(h + 345, prefix.data(), std::min<std::size_t>(prefix.size(), 155));
    std::memset(h + 148, ' ', 8);
    unsigned sum = 0;
    for (std::uint8_t b : h) sum += b;
    std::snprintf(reinterpret_cast<char*>(h + 148), 8, "%06o", sum);
    tar.insert(tar.end(), h, h + 512);
    tar.insert(tar.end(), body.begin(), body.end());
    tar.resize((tar.size() + 511) / 512 * 512, 0);
}

void testTar() {
    std::vector<std::uint8_t> tar;
    tarMember(tar, "OpenDay-1.0-linux-x64/", '5', "", 0755);
    tarMember(tar, "OpenDay-1.0-linux-x64/bin/openday", '0', "#!binary", 0755);
    const std::string longName = "OpenDay-1.0-linux-x64/share/doc/OpenDay/licenses/" + std::string(80, 'n') + ".txt";
    tarMember(tar, "././@LongLink", 'L', longName + std::string(1, '\0'));
    tarMember(tar, "truncated", '0', "long file");
    const std::string pax = "29 path=OpenDay/pax-name.txt\n";
    tarMember(tar, "PaxHeader", 'x', pax);
    tarMember(tar, "ignored", '0', "pax file");
    tarMember(tar, "file.txt", '0', "prefixed", 0600, "OpenDay-1.0-linux-x64/share");
    tarMember(tar, "OpenDay-1.0-linux-x64/bin/link", '2', "");
    tar.resize(tar.size() + 1024, 0);

    // Two gzip members (concatenated files are one gzip stream).
    const std::size_t half = tar.size() / 2;
    std::vector<std::uint8_t> gz = gzipStored({tar.begin(), tar.begin() + static_cast<std::ptrdiff_t>(half)}, true);
    const std::vector<std::uint8_t> second = gzipStored({tar.begin() + static_cast<std::ptrdiff_t>(half), tar.end()}, false);
    gz.insert(gz.end(), second.begin(), second.end());

    const std::vector<std::uint8_t> unpacked = formats::gunzip(gz);
    CHECK(unpacked == tar);
    const std::vector<formats::TarEntry> entries = formats::readTar(unpacked);
    CHECK(entries.size() == 6);
    if (entries.size() == 6) {
        CHECK(entries[0].path == "OpenDay-1.0-linux-x64" && entries[0].kind == formats::TarEntry::Kind::Directory);
        CHECK(entries[1].path == "OpenDay-1.0-linux-x64/bin/openday" && entries[1].mode == 0755);
        CHECK(entries[1].data == bytes("#!binary"));
        CHECK(entries[2].path == longName && entries[2].data == bytes("long file"));
        CHECK(entries[3].path == "OpenDay/pax-name.txt" && entries[3].data == bytes("pax file"));
        CHECK(entries[4].path == "OpenDay-1.0-linux-x64/share/file.txt" && entries[4].mode == 0600);
        CHECK(entries[5].kind == formats::TarEntry::Kind::Symlink);
    }

    std::vector<std::uint8_t> bad = gz;
    bad[bad.size() - 6] ^= 1; // CRC of the second member
    CHECK(throws([&] { formats::gunzip(bad); }));
    CHECK(throws([] { formats::gunzip(bytes("not gzip at all, really")); }));
    std::vector<std::uint8_t> badTar = tar;
    badTar[0] ^= 1; // header checksum
    CHECK(throws([&] { formats::readTar(badTar); }));
    CHECK(throws([&] { formats::gunzip(gzipStored(tar, false), 100); })); // output limit
}

} // namespace

int main() {
    testSha256();
    testJson();
    testRelease();
    testVersions();
    testSums();
    testTar();
    if (failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("update_test: all checks passed\n");
    return 0;
}
