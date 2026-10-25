// The GitHub release information the updater works with: parsing the
// "latest release" answer of the GitHub REST API, comparing versions and
// choosing the package for this platform (docs/updates.md).
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace openday::update {

// The repository the releases come from.
inline constexpr const char* kRepository = "Tenax84/open-day";
// GET https://api.github.com/repos/<kRepository>/releases/latest
std::string latestReleaseUrl();

struct Asset {
    std::string name;
    std::string url;      // browser_download_url
    std::uint64_t size = 0;
    std::string sha256;   // from the asset's "digest" (lower-case hex); empty if GitHub gave none
};

struct Release {
    std::string tag;     // "v0.1.2"
    std::string version; // "0.1.2" (the tag without its leading 'v')
    std::string name;
    std::string pageUrl; // html_url
    std::string notes;   // body (Markdown)
    bool draft = false, prerelease = false;
    std::vector<Asset> assets;

    const Asset* asset(std::string_view assetName) const;
};

// Parses the JSON of one release. Returns nullopt (and `error`) when it is
// not a release object.
std::optional<Release> parseRelease(std::string_view json, std::string& error);

// Compares dotted version numbers ("0.1.10" > "0.1.9"; missing parts are 0;
// a leading 'v' is ignored). A pre-release suffix ("1.0.0-rc1") ranks below
// the version without one; suffixes compare as text. <0, 0 or >0.
int compareVersions(std::string_view a, std::string_view b);

// The platform this program was built for, as in the package names:
// os "windows" / "macos" / "linux", arch "x64" / "arm64" / ...
std::string_view platformOs();
std::string_view platformArch();

// The package file of a version for a platform, as cmake/Packaging.cmake
// names it: OpenDay-<version>-<os>-<arch>.<exe|dmg|tar.gz>.
std::string packageName(std::string_view version, std::string_view os, std::string_view arch);

// The SHA-256 of `fileName` in a SHA256SUMS file ("<hex>  <name>" lines,
// as sha256sum writes them); empty if missing.
std::string sha256FromSums(std::string_view sums, std::string_view fileName);

} // namespace openday::update
