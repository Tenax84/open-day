// Case-insensitive file lookup inside the game installation.
//
// The engine names game files as the Windows release spells them
// ("INTERF/PAL.BMP"). On case-sensitive file systems (Linux; macOS volumes
// formatted case-sensitive) a copy made with other tools, e.g. innoextract
// with lower-case names, still has to be found.
#pragma once

#include <filesystem>
#include <string_view>

namespace openday::formats {

// Returns root / relative ('/'-separated) if it exists; otherwise walks the
// components and matches each one case-insensitively (ASCII). When nothing
// matches, returns root / relative unchanged, so the caller's "cannot open"
// error names the expected file.
std::filesystem::path findGameFile(const std::filesystem::path& root, std::string_view relative);

// ASCII case-insensitive comparison of two names.
bool sameNameIgnoreCase(std::string_view a, std::string_view b);

} // namespace openday::formats
