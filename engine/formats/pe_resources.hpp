// String tables from the resources of a Windows PE file (the game's language
// DLL, docs/mechanics/interface.md "Strings"). Written from the public PE/COFF
// specification.
#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace openday::formats {

struct PeError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// All RT_STRING entries, by string id, converted to UTF-8. When a block
// exists in several languages, the first one in the file is used.
std::map<int, std::string> loadStringTable(std::span<const std::uint8_t> pe);
std::map<int, std::string> loadStringTable(const std::filesystem::path& path);

// Expands the Windows message-format placeholders %1 ... %9 (with an
// optional printf part such as %1!d!) and %%.
std::string formatMessage(const std::string& pattern, const std::vector<std::string>& args);

} // namespace openday::formats
