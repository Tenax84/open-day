// Resources of a Windows PE file: the string tables of the game's language
// DLL (docs/mechanics/interface.md "Strings") and the bitmaps of the game
// program (docs/mechanics/encyclopedia.md, tactical-interface.md 7.2), read
// at run time from the user's installation. Written from the public PE/COFF
// and BMP specifications.
#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include "formats/palette.hpp"

namespace openday::formats {

struct PeError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

inline constexpr std::uint32_t kResourceBitmap = 2; // RT_BITMAP
inline constexpr std::uint32_t kResourceString = 6; // RT_STRING

// The raw data of every resource of a type that has a numeric id (the first
// language of each), by id.
std::map<int, std::vector<std::uint8_t>> loadResources(std::span<const std::uint8_t> pe, std::uint32_t type);
std::map<int, std::vector<std::uint8_t>> loadResources(const std::filesystem::path& path, std::uint32_t type);

// A bitmap resource (a DIB without the file header) as RGB pixels.
struct DibImage {
    int width = 0, height = 0;
    std::vector<Rgb> pixels; // width * height, top row first
};
// Uncompressed 1-, 4-, 8-, 24- and 32-bit DIBs; throws PeError.
DibImage decodeDib(std::span<const std::uint8_t> dib);

// All RT_STRING entries, by string id, converted to UTF-8. When a block
// exists in several languages, the first one in the file is used.
std::map<int, std::string> loadStringTable(std::span<const std::uint8_t> pe);
std::map<int, std::string> loadStringTable(const std::filesystem::path& path);

// Expands the Windows message-format placeholders %1 ... %9 (with an
// optional printf part such as %1!d!) and %%.
std::string formatMessage(const std::string& pattern, const std::vector<std::string>& args);

} // namespace openday::formats
