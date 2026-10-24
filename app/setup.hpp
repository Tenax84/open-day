// Start-up configuration: the per-user directory, the remembered settings,
// finding the game installation and the first-run database conversion.
#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace openday::app::setup {

namespace fs = std::filesystem;

// The per-user directory (created on first use): converted data, saves and
// the configuration file.
//   Windows: %APPDATA%\OpenDay
//   macOS:   ~/Library/Application Support/OpenDay/
//   Linux:   $XDG_DATA_HOME/openday/ (~/.local/share/openday/)
fs::path userDir();

// key=value settings in userDir()/openday.cfg.
class Config {
public:
    static Config load();
    void save() const;

    std::string get(const std::string& key, const std::string& fallback = {}) const;
    int getInt(const std::string& key, int fallback) const;
    void set(const std::string& key, const std::string& value) { values_[key] = value; }
    void setInt(const std::string& key, int value) { values_[key] = std::to_string(value); }

private:
    std::map<std::string, std::string> values_;
};

// The game folder for a candidate: the folder itself, or an "app"
// sub-folder (the layout of some extracted GOG installers), if it holds the
// game files. Empty if neither does.
fs::path validGameDir(const fs::path& candidate);

// Places where an installation is looked for when none is configured.
std::vector<fs::path> defaultGameDirs();

// The game folder: `explicitDir` (--game-dir) if given, then
// $OPENDAY_GAME_DIR, the remembered folder and the default locations.
// With `interactive`, an SDL message box explains what is missing and lets
// the user choose the folder. Returns empty if no valid folder was found;
// `error` then says why. A folder given or chosen by the user is remembered.
fs::path findGameDir(const fs::path& explicitDir, Config& config, bool interactive, std::string& error);

// The folder of the converted databases: `explicitDir` (--data), then
// $OPENDAY_DATA_DIR, then userDir()/data.
fs::path dataDir(const fs::path& explicitDir, bool& isDefault);

// Converts the game databases into dataDir when that is needed (missing or
// outdated conversion, or `force`). A data folder made by
// tools/mdb/mdb2sqlite.py (no stamp file) is used as it is unless `force` or
// it is the default folder. With `interactive` a small window shows the
// progress. Returns false (with `error`) if the conversion failed.
bool prepareData(const fs::path& gameDir, const fs::path& dataDir, bool isDefault, bool force, bool interactive,
                 std::string& error);

// Shows an error to the user (message box when interactive) and on stderr.
void reportError(const std::string& message, bool interactive);

// Windows: the program has no console window of its own (GUI subsystem); when
// started from a terminal, stdout and stderr go to that terminal. No-op
// elsewhere.
void attachParentConsole();

} // namespace openday::app::setup
