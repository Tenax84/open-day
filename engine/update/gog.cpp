#include "update/gog.hpp"

#include <algorithm>
#include <cctype>

#include "formats/json.hpp"

namespace openday::update::gog {
namespace {

// The client GOG Galaxy signs in as, which GOG's sign-in page accepts, and
// its published secret, sent with the code.
constexpr std::string_view kClientId = "46899977096215655";
constexpr std::string_view kClientSecret = "9d85c43b1482497dbbce61f6e4aa173a433796eeae2ca8c5f6129f2dc4de46d9";
// Where GOG's sign-in page ends, with the code in its query.
constexpr std::string_view kLanding = "https://embed.gog.com/on_login_success?origin=client";
constexpr std::string_view kCodeName = "code=";
// The fewest characters of a code; GOG's are much longer.
constexpr std::size_t kLeastCodeLength = 16;
constexpr std::size_t kMostNameLength = 160;
// Hosts the installer's address may lead to.
constexpr std::string_view kDownloadHosts[] = {".gog.com", ".gog-cdn.com", ".gogcdn.net", ".gog-statics.com"};

bool codeCharacter(char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '-' || c == '_'; }

int hexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

std::optional<formats::JsonValue> parse(std::string_view json) {
    try {
        return formats::JsonValue::parse(json);
    } catch (const formats::JsonError&) {
        return std::nullopt;
    }
}

} // namespace

std::string escaped(std::string_view text) {
    constexpr char kHex[] = "0123456789ABCDEF";
    std::string out;
    for (const char c : text) {
        const auto byte = static_cast<unsigned char>(c);
        if (std::isalnum(byte) != 0 || c == '-' || c == '.' || c == '_' || c == '~') {
            out.push_back(c);
        } else {
            out.push_back('%');
            out.push_back(kHex[byte >> 4]);
            out.push_back(kHex[byte & 0xF]);
        }
    }
    return out;
}

std::string signInAddress() {
    return "https://auth.gog.com/auth?client_id=" + std::string(kClientId) + "&redirect_uri=" + escaped(kLanding) +
           "&response_type=code&layout=client2";
}

std::string_view landingAddress() { return kLanding; }

std::optional<std::string> loginCode(std::string_view text) {
    const auto blank = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    while (!text.empty() && blank(text.front())) text.remove_prefix(1);
    while (!text.empty() && blank(text.back())) text.remove_suffix(1);
    std::string_view code = text;
    if (const std::size_t query = text.find('?'); query != std::string_view::npos) {
        std::string_view rest = text.substr(query + 1);
        code = {};
        while (!rest.empty()) {
            const std::size_t end = rest.find_first_of("&#");
            const std::string_view pair = rest.substr(0, end);
            if (pair.starts_with(kCodeName)) {
                code = pair.substr(kCodeName.size());
                break;
            }
            if (end == std::string_view::npos || rest[end] == '#') break;
            rest.remove_prefix(end + 1);
        }
    }
    if (code.size() < kLeastCodeLength || !std::all_of(code.begin(), code.end(), codeCharacter)) return std::nullopt;
    return std::string(code);
}

std::string tokenAddress(std::string_view code) {
    return "https://auth.gog.com/token?client_id=" + std::string(kClientId) + "&client_secret=" +
           std::string(kClientSecret) + "&grant_type=authorization_code&code=" + escaped(code) +
           "&redirect_uri=" + escaped(kLanding);
}

std::string accessToken(std::string_view json) {
    const auto value = parse(json);
    return value ? (*value)["access_token"].string() : std::string();
}

std::string productAddress() { return "https://api.gog.com/products/" + std::string(kProduct) + "?expand=downloads"; }

std::vector<InstallerFile> windowsInstaller(std::string_view productJson) {
    const auto value = parse(productJson);
    if (!value) return {};
    const auto& installers = (*value)["downloads"]["installers"].items();
    for (const bool english : {true, false}) {
        for (const formats::JsonValue& installer : installers) {
            if (installer["os"].string() != "windows") continue;
            if (english && installer["language"].string() != "en") continue;
            std::vector<InstallerFile> files;
            for (const formats::JsonValue& file : installer["files"].items()) {
                const std::string& link = file["downlink"].string();
                const double size = file["size"].number(-1);
                if (!link.starts_with("https://api.gog.com/") || size < 0) {
                    files.clear();
                    break;
                }
                files.push_back({link, static_cast<std::uint64_t>(size)});
            }
            if (!files.empty()) return files;
        }
    }
    return {};
}

std::string secureLink(std::string_view json) {
    const auto value = parse(json);
    if (!value) return {};
    const std::string& link = (*value)["downlink"].string();
    return link.starts_with("https://") ? link : std::string();
}

std::string fileNameOf(std::string_view address) {
    if (const std::size_t query = address.find_first_of("?#"); query != std::string_view::npos)
        address = address.substr(0, query);
    const std::size_t slash = address.rfind('/');
    if (slash == std::string_view::npos) return {};
    const std::string_view last = address.substr(slash + 1);
    std::string name;
    for (std::size_t at = 0; at < last.size(); ++at) {
        char c = last[at];
        if (c == '%' && at + 2 < last.size()) {
            const int high = hexValue(last[at + 1]);
            const int low = hexValue(last[at + 2]);
            if (high < 0 || low < 0) return {};
            c = static_cast<char>(high * 16 + low);
            at += 2;
        }
        const bool plain = std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '.' || c == '_' || c == '-' ||
                           c == '(' || c == ')';
        if (!plain) return {};
        name.push_back(c);
    }
    if (name.empty() || name.front() == '.' || name.size() > kMostNameLength) return {};
    return name;
}

bool downloadHost(std::string_view address) {
    constexpr std::string_view kScheme = "https://";
    if (!address.starts_with(kScheme)) return false;
    address.remove_prefix(kScheme.size());
    std::string host(address.substr(0, address.find_first_of("/?#")));
    if (const std::size_t colon = host.find(':'); colon != std::string::npos) host.resize(colon);
    std::transform(host.begin(), host.end(), host.begin(),
                   [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
    host.insert(host.begin(), '.');
    return std::any_of(std::begin(kDownloadHosts), std::end(kDownloadHosts),
                       [&host](std::string_view suffix) { return host.ends_with(suffix); });
}

} // namespace openday::update::gog
