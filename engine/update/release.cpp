#include "update/release.hpp"

#include <cctype>

#include "formats/json.hpp"

namespace openday::update {
namespace {

bool isHex64(std::string_view s) {
    if (s.size() != 64) return false;
    for (char c : s)
        if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
    return true;
}

std::string lower(std::string_view s) {
    std::string out(s);
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

std::string_view stripV(std::string_view v) {
    if (!v.empty() && (v.front() == 'v' || v.front() == 'V')) v.remove_prefix(1);
    return v;
}

} // namespace

std::string latestReleaseUrl() { return std::string("https://api.github.com/repos/") + kRepository + "/releases/latest"; }

const Asset* Release::asset(std::string_view assetName) const {
    for (const Asset& a : assets)
        if (a.name == assetName) return &a;
    return nullptr;
}

std::optional<Release> parseRelease(std::string_view json, std::string& error) {
    formats::JsonValue root;
    try {
        root = formats::JsonValue::parse(json);
    } catch (const formats::JsonError& e) {
        error = e.what();
        return std::nullopt;
    }
    Release r;
    r.tag = root["tag_name"].string();
    if (r.tag.empty()) {
        // An error answer: {"message": "Not Found", ...}
        const std::string& message = root["message"].string();
        error = message.empty() ? "no release information" : "GitHub: " + message;
        return std::nullopt;
    }
    r.version = std::string(stripV(r.tag));
    r.name = root["name"].string();
    r.pageUrl = root["html_url"].string();
    r.notes = root["body"].string();
    r.draft = root["draft"].boolean();
    r.prerelease = root["prerelease"].boolean();
    for (const formats::JsonValue& a : root["assets"].items()) {
        Asset asset;
        asset.name = a["name"].string();
        asset.url = a["browser_download_url"].string();
        asset.size = static_cast<std::uint64_t>(a["size"].number());
        const std::string& digest = a["digest"].string();
        if (digest.rfind("sha256:", 0) == 0 && isHex64(std::string_view(digest).substr(7)))
            asset.sha256 = lower(std::string_view(digest).substr(7));
        if (!asset.name.empty() && !asset.url.empty()) r.assets.push_back(std::move(asset));
    }
    return r;
}

int compareVersions(std::string_view a, std::string_view b) {
    a = stripV(a);
    b = stripV(b);
    auto split = [](std::string_view v, std::string_view& suffix) {
        const std::size_t dash = v.find_first_of("-+");
        suffix = dash == std::string_view::npos ? std::string_view() : v.substr(dash + 1);
        if (dash != std::string_view::npos && v[dash] == '+') suffix = {}; // build metadata does not rank
        return v.substr(0, dash);
    };
    std::string_view sa, sb;
    std::string_view na = split(a, sa), nb = split(b, sb);
    while (!na.empty() || !nb.empty()) {
        auto next = [](std::string_view& v) {
            unsigned long long n = 0;
            std::size_t i = 0;
            for (; i < v.size() && v[i] != '.'; ++i)
                if (std::isdigit(static_cast<unsigned char>(v[i]))) n = n * 10 + static_cast<unsigned>(v[i] - '0');
            v.remove_prefix(i < v.size() ? i + 1 : i);
            return n;
        };
        const unsigned long long x = next(na), y = next(nb);
        if (x != y) return x < y ? -1 : 1;
    }
    if (sa.empty() != sb.empty()) return sa.empty() ? 1 : -1;
    const int c = sa.compare(sb);
    return c < 0 ? -1 : c > 0 ? 1 : 0;
}

std::string_view platformOs() {
#if defined(_WIN32)
    return "windows";
#elif defined(__APPLE__)
    return "macos";
#else
    return "linux";
#endif
}

std::string_view platformArch() {
#if defined(__aarch64__) || defined(_M_ARM64)
    return "arm64";
#elif defined(__x86_64__) || defined(_M_X64)
    return "x64";
#elif defined(__i386__) || defined(_M_IX86)
    return "x86";
#else
    return "unknown";
#endif
}

std::string packageName(std::string_view version, std::string_view os, std::string_view arch) {
    const char* ext = os == "windows" ? ".exe" : os == "macos" ? ".dmg" : ".tar.gz";
    return "OpenDay-" + std::string(stripV(version)) + "-" + std::string(os) + "-" + std::string(arch) + ext;
}

std::string sha256FromSums(std::string_view sums, std::string_view fileName) {
    while (!sums.empty()) {
        const std::size_t eol = sums.find('\n');
        std::string_view line = sums.substr(0, eol);
        sums.remove_prefix(eol == std::string_view::npos ? sums.size() : eol + 1);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (line.size() < 66 || !isHex64(line.substr(0, 64)) || line[64] != ' ') continue;
        std::string_view name = line.substr(65);
        if (!name.empty() && (name.front() == ' ' || name.front() == '*')) name.remove_prefix(1); // text / binary mode mark
        if (name == fileName) return lower(line.substr(0, 64));
    }
    return {};
}

} // namespace openday::update
