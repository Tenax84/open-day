// Copies the game files from an installation medium the user owns: the GOG
// installer (Inno Setup, setup_*.exe with its *.bin files) or an image of
// the game CD (.iso, or the .bin of a .bin/.cue pair). Nothing is run; the
// files are read with formats/inno_setup and formats/iso9660 and written
// into a folder that then serves as the game folder.
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

namespace openday::data {

struct SourceError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

enum class SourceKind { Unknown, Installer, DiscImage };

// Looks at the file's contents (not its name).
SourceKind detectSource(const std::filesystem::path& file);

struct SourceFile {
    std::string path; // relative to the game folder, '/'-separated ("INTERF/PAL.BMP")
    std::uint64_t size = 0;
};

struct GameSource {
    SourceKind kind = SourceKind::Unknown;
    std::string description; // "Inno Setup 5.5.7 (unicode) installer of ..."
    std::vector<SourceFile> files;
};

// Reads the medium's table of contents and finds the game files in it.
// Throws SourceError when the file is not a supported medium or does not
// hold the game.
GameSource inspectSource(const std::filesystem::path& file);

// (bytes done, bytes total, file being written)
using InstallProgress = std::function<void(std::uint64_t done, std::uint64_t total, const std::string& file)>;

// Copies the game files into `gameDir`, which must not exist yet or be
// empty. The files are written to a sibling "<gameDir>.partial" folder that
// is renamed when everything succeeded, so an interrupted copy never looks
// like a game folder. Throws SourceError.
void installFromSource(const std::filesystem::path& file, const std::filesystem::path& gameDir,
                       const InstallProgress& progress = {});

// A relative path from an archive, made safe: '\' becomes '/', and empty,
// "." and ".." components, drive letters and absolute paths are refused
// (returns empty).
std::string safeRelativePath(const std::string& path);

} // namespace openday::data
