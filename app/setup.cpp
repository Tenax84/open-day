#include "app/setup.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <mutex>
#include <system_error>
#include <thread>
#include <utility>

#include "data/game_source.hpp"
#include "data/mdb_import.hpp"
#include "formats/game_path.hpp"

namespace openday::app::setup {
namespace {

constexpr const char* kTitle = "OpenDay";
constexpr const char* kGameFolderName = "Fallen Haven Liberation Day";

fs::path utf8Path(const char* s) {
    return s ? fs::path(std::u8string(reinterpret_cast<const char8_t*>(s))) : fs::path();
}

std::string utf8(const fs::path& p) {
    const std::u8string u = p.u8string();
    return std::string(u.begin(), u.end());
}

// SDL_getenv gives UTF-8 on every platform (std::getenv is in the ANSI code
// page on Windows, which garbles non-ASCII folder names).
fs::path envPath(const char* name) {
    const char* v = SDL_getenv(name);
    return v && *v ? utf8Path(v) : fs::path();
}

bool pathExists(const fs::path& p) {
    std::error_code ec;
    return fs::exists(p, ec);
}

#ifndef _WIN32
fs::path homeDir() { return envPath("HOME"); }

// Typical sub-paths of a GOG installation below a Windows drive C: (also
// inside Wine prefixes and CrossOver bottles).
void addDriveC(std::vector<fs::path>& out, const fs::path& driveC) {
    out.push_back(driveC / "GOG Games" / kGameFolderName);
    out.push_back(driveC / "Program Files (x86)" / "GOG Galaxy" / "Games" / kGameFolderName);
    out.push_back(driveC / "Program Files" / "GOG Galaxy" / "Games" / kGameFolderName);
    out.push_back(driveC / "Program Files (x86)" / "GOG.com" / kGameFolderName);
}
#endif

// Runs `work` on a worker thread while a small window shows a progress bar.
// `status` gives the line under the two text lines and the fraction done.
void runWithProgress(const char* windowTitle, const char* line1, const char* line2, const std::function<void()>& work,
                     const std::function<std::pair<std::string, float>()>& status) {
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    if (!SDL_CreateWindowAndRenderer(windowTitle, 560, 120, 0, &window, &renderer)) {
        work();
        return;
    }
    std::atomic<bool> finished{false};
    std::thread worker([&] {
        work();
        finished = true;
    });
    while (!finished) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            // Closing the window cannot interrupt the work half way; the
            // program ends after it.
        }
        const auto [line, fraction] = status();
        SDL_SetRenderDrawColor(renderer, 16, 20, 32, 255);
        SDL_RenderClear(renderer);
        SDL_SetRenderDrawColor(renderer, 230, 230, 230, 255);
        SDL_RenderDebugText(renderer, 20, 20, line1);
        SDL_RenderDebugText(renderer, 20, 36, line2);
        SDL_RenderDebugText(renderer, 20, 60, line.c_str());
        const SDL_FRect frame{20, 84, 520, 16};
        SDL_RenderRect(renderer, &frame);
        const SDL_FRect bar{22, 86, 516.0f * std::clamp(fraction, 0.0f, 1.0f), 12};
        SDL_SetRenderDrawColor(renderer, 90, 140, 220, 255);
        SDL_RenderFillRect(renderer, &bar);
        SDL_RenderPresent(renderer);
        SDL_Delay(30);
    }
    worker.join();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
}

struct DialogChoice {
    std::atomic<bool> done{false};
    bool failed = false; // no dialog available (e.g. Linux without a portal or zenity)
    fs::path path;
};

void SDLCALL dialogChosen(void* userdata, const char* const* filelist, int /*filter*/) {
    auto* choice = static_cast<DialogChoice*>(userdata);
    if (!filelist) choice->failed = true;
    else if (filelist[0]) choice->path = utf8Path(filelist[0]);
    choice->done = true;
}

// Waits for an SDL file dialog. Returns false (after telling the user) if
// no dialog is available.
bool waitForDialog(DialogChoice& choice) {
    while (!choice.done) {
        SDL_PumpEvents();
        SDL_Delay(20);
    }
    if (!choice.failed) return true;
    const std::string msg = std::string("The file dialog is not available: ") + SDL_GetError() +
                            "\n\nStart OpenDay with --game-dir <folder> or --install-from <file> instead.";
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, kTitle, msg.c_str(), nullptr);
    return false;
}

// Asks for the game folder, or for an installer or CD image to copy the
// game from. Returns empty if the user gives up.
fs::path askForGameDir() {
    const std::string text =
        "OpenDay needs the files of the original game \"Fallen Haven: Liberation Day\" "
        "(for example the GOG release). They are not part of OpenDay.\n\n"
        "Choose one of these:\n"
        "  - the game folder of an installation (or a copy of it): it contains GLOBALS, IMGS, INTERF "
        "and the other game folders;\n"
        "  - the GOG installer (setup_*.exe, with its .bin files next to it) or an image of the game "
        "CD (.iso, .bin): OpenDay copies the game files out of it into " +
        utf8(userDir() / "game") + ".\n\n"
        "You can also start OpenDay with --game-dir <folder> or --install-from <file>.";
    for (;;) {
        const SDL_MessageBoxButtonData buttons[] = {
            {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Game folder..."},
            {0, 2, "Installer or CD image..."},
            {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Quit"},
        };
        const SDL_MessageBoxData box{SDL_MESSAGEBOX_INFORMATION, nullptr, "OpenDay: game files not found", text.c_str(),
                                     3, buttons, nullptr};
        int pressed = 0;
        if (!SDL_ShowMessageBox(&box, &pressed) || pressed == 0) return {};

        DialogChoice choice;
        if (pressed == 1) {
            SDL_ShowOpenFolderDialog(dialogChosen, &choice, nullptr, nullptr, false);
            if (!waitForDialog(choice)) return {};
            if (choice.path.empty()) continue; // cancelled: explain again
            if (fs::path dir = validGameDir(choice.path); !dir.empty()) return dir;
            const std::string msg = "This folder does not contain the game files:\n" + utf8(choice.path) +
                                    "\n\nChoose the folder that holds GLOBALS, IMGS and INTERF.";
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, kTitle, msg.c_str(), nullptr);
            continue;
        }
        static const SDL_DialogFileFilter filters[] = {
            {"Game installer or CD image", "exe;iso;bin;img"},
            {"All files", "*"},
        };
        SDL_ShowOpenFileDialog(dialogChosen, &choice, nullptr, filters, 2, nullptr, false);
        if (!waitForDialog(choice)) return {};
        if (choice.path.empty()) continue;
        std::string error;
        if (fs::path dir = installGameFiles(choice.path, true, error); !dir.empty()) return dir;
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, kTitle, error.c_str(), nullptr);
    }
}

// Runs the conversion, with a progress window when interactive.
bool convertWithProgress(const fs::path& gameDir, const fs::path& dataDir, bool interactive, data::ImportReport& report,
                         std::string& error) {
    std::atomic<std::size_t> done{0}, total{0};
    std::mutex m;
    std::string current;
    auto progress = [&](std::size_t i, std::size_t n, const std::string& stem) {
        done = i;
        total = n;
        std::lock_guard lock(m);
        current = stem;
        if (!interactive && !stem.empty()) std::printf("converting [%zu/%zu] %s\n", i + 1, n, stem.c_str());
    };
    auto work = [&] {
        try {
            report = data::importInstallation(gameDir, dataDir, progress);
        } catch (const std::exception& e) {
            error = e.what();
        }
    };
    if (!interactive) {
        work();
        return error.empty();
    }
    runWithProgress("OpenDay - preparing the game data", "First start: converting the game databases.",
                    "This happens once and takes a few seconds.", work, [&] {
                        std::string stem;
                        {
                            std::lock_guard lock(m);
                            stem = current;
                        }
                        const std::size_t n = total, i = done;
                        char line[160];
                        std::snprintf(line, sizeof line, "%zu / %zu  %s", n ? i + 1 : 0, n, stem.c_str());
                        return std::pair<std::string, float>(line, n ? static_cast<float>(i) / static_cast<float>(n) : 0.0f);
                    });
    return error.empty();
}

} // namespace

fs::path userDir() {
#if defined(_WIN32) || defined(__APPLE__)
    char* pref = SDL_GetPrefPath("", "OpenDay");
#else
    char* pref = SDL_GetPrefPath("", "openday");
#endif
    fs::path p = pref ? utf8Path(pref) : fs::current_path() / "openday-user";
    SDL_free(pref);
    std::error_code ec;
    fs::create_directories(p, ec);
#if defined(_WIN32) || defined(__APPLE__)
    // Earlier builds kept the saves in <app data>/OpenDay/OpenDay/saves.
    static bool migrated = false;
    if (!migrated) {
        migrated = true;
        const fs::path old = p / "OpenDay" / "saves";
        if (fs::is_directory(old, ec) && !fs::exists(p / "saves", ec)) fs::rename(old, p / "saves", ec);
    }
#endif
    return p;
}

Config Config::load() {
    Config c;
    std::ifstream in(userDir() / "openday.cfg", std::ios::binary);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        const auto eq = line.find('=');
        if (eq != std::string::npos) c.values_[line.substr(0, eq)] = line.substr(eq + 1);
    }
    return c;
}

void Config::save() const {
    std::ofstream out(userDir() / "openday.cfg", std::ios::binary);
    out << "# OpenDay settings (key=value)\n";
    for (const auto& [k, v] : values_) out << k << '=' << v << '\n';
}

std::string Config::get(const std::string& key, const std::string& fallback) const {
    const auto it = values_.find(key);
    return it == values_.end() ? fallback : it->second;
}

int Config::getInt(const std::string& key, int fallback) const {
    const auto it = values_.find(key);
    if (it == values_.end() || it->second.empty()) return fallback;
    char* end = nullptr;
    const long v = std::strtol(it->second.c_str(), &end, 10);
    return end && *end == '\0' ? static_cast<int>(v) : fallback;
}

fs::path validGameDir(const fs::path& candidate) {
    if (candidate.empty()) return {};
    for (const fs::path& dir : {candidate, candidate / "app"}) {
        if (pathExists(formats::findGameFile(dir, "GLOBALS/GLOBAL.MDB")) && pathExists(formats::findGameFile(dir, "INTERF/PAL.BMP")) &&
            pathExists(formats::findGameFile(dir, "IMGS")))
            return dir;
    }
    return {};
}

fs::path installGameFiles(const fs::path& source, bool interactive, std::string& error) {
    const fs::path target = userDir() / "game";
    std::error_code ec;
    if (fs::exists(target, ec) && !fs::is_empty(target, ec)) {
        if (fs::path dir = validGameDir(target); !dir.empty()) return dir; // copied before
        error = "The folder " + utf8(target) + " is in the way: it is not empty but holds no game. Remove it and try again.";
        return {};
    }
    data::GameSource info;
    try {
        info = data::inspectSource(source);
    } catch (const std::exception& e) {
        error = "Cannot use " + utf8(source.filename()) + ":\n" + e.what();
        return {};
    }
    std::printf("copying the game files from %s (%s) into %s\n", utf8(source).c_str(), info.description.c_str(),
                utf8(target).c_str());
    std::atomic<std::uint64_t> done{0}, total{0};
    std::mutex m;
    std::string current;
    auto work = [&] {
        try {
            data::installFromSource(source, target, [&](std::uint64_t d, std::uint64_t t, const std::string& file) {
                done = d;
                total = t;
                std::lock_guard lock(m);
                current = file;
            });
        } catch (const std::exception& e) {
            error = "Copying the game files from " + utf8(source.filename()) + " failed:\n" + e.what();
        }
    };
    if (interactive) {
        runWithProgress("OpenDay - copying the game files", "Copying the game files out of the installer or CD image.",
                        "This happens once and takes a minute or two.", work, [&] {
                            std::string file;
                            {
                                std::lock_guard lock(m);
                                file = current;
                            }
                            const std::uint64_t t = total, d = done;
                            char line[160];
                            std::snprintf(line, sizeof line, "%3d%%  %s", t ? static_cast<int>(d * 100 / t) : 0, file.c_str());
                            return std::pair<std::string, float>(line, t ? static_cast<float>(d) / static_cast<float>(t) : 0.0f);
                        });
    } else {
        work();
    }
    if (!error.empty()) return {};
    if (fs::path dir = validGameDir(target); !dir.empty()) return dir;
    error = "The files copied from " + utf8(source.filename()) + " do not form a game folder.";
    return {};
}

std::vector<fs::path> defaultGameDirs() {
    std::vector<fs::path> out;
    out.push_back(userDir() / "game");
    if (const char* base = SDL_GetBasePath()) out.push_back(utf8Path(base) / "game"); // portable layout
#ifdef _WIN32
    for (const char* var : {"ProgramFiles(x86)", "ProgramFiles"}) {
        const fs::path pf = envPath(var);
        if (pf.empty()) continue;
        out.push_back(pf / "GOG Galaxy" / "Games" / kGameFolderName);
        out.push_back(pf / "GOG.com" / kGameFolderName);
    }
    fs::path drive = envPath("SystemDrive");
    if (drive.empty()) drive = "C:";
    drive += "\\";
    out.push_back(drive / "GOG Games" / kGameFolderName);
    out.push_back(drive / "Program Files (x86)" / "GOG Galaxy" / "Games" / kGameFolderName);
#else
    if (const fs::path home = homeDir(); !home.empty()) {
        out.push_back(home / "GOG Games" / kGameFolderName);
        out.push_back(home / "Games" / kGameFolderName);
        out.push_back(home / "Games" / "Heroic" / kGameFolderName);
        addDriveC(out, home / ".wine" / "drive_c");
#ifdef __APPLE__
        const fs::path bottles = home / "Library" / "Application Support" / "CrossOver" / "Bottles";
        std::error_code ec;
        for (fs::directory_iterator it(bottles, ec), end; !ec && it != end; it.increment(ec))
            addDriveC(out, it->path() / "drive_c");
#endif
    }
#endif
    return out;
}

fs::path findGameDir(const fs::path& explicitDir, Config& config, bool interactive, std::string& error) {
    if (!explicitDir.empty()) {
        if (fs::path dir = validGameDir(explicitDir); !dir.empty()) {
            config.set("game_dir", utf8(fs::absolute(dir)));
            return dir;
        }
        error = "--game-dir " + utf8(explicitDir) + " does not contain the game files (GLOBALS/GLOBAL.MDB, INTERF, IMGS)";
        return {};
    }
    if (const fs::path env = envPath("OPENDAY_GAME_DIR"); !env.empty()) {
        if (fs::path dir = validGameDir(env); !dir.empty()) return dir;
        error = "OPENDAY_GAME_DIR=" + utf8(env) + " does not contain the game files (GLOBALS/GLOBAL.MDB, INTERF, IMGS)";
        return {};
    }
    if (const std::string remembered = config.get("game_dir"); !remembered.empty())
        if (fs::path dir = validGameDir(utf8Path(remembered.c_str())); !dir.empty()) return dir;
    for (const fs::path& candidate : defaultGameDirs())
        if (fs::path dir = validGameDir(candidate); !dir.empty()) return dir;

    if (interactive) {
        if (fs::path dir = askForGameDir(); !dir.empty()) {
            config.set("game_dir", utf8(fs::absolute(dir)));
            return dir;
        }
    }
    error = "the game files were not found; start with --game-dir <folder>, set OPENDAY_GAME_DIR, or put the game folder at " +
            utf8(userDir() / "game");
    return {};
}

fs::path dataDir(const fs::path& explicitDir, bool& isDefault) {
    isDefault = false;
    if (!explicitDir.empty()) return explicitDir;
    if (fs::path env = envPath("OPENDAY_DATA_DIR"); !env.empty()) return env;
    isDefault = true;
    return userDir() / "data";
}

bool prepareData(const fs::path& gameDir, const fs::path& dataDir, bool isDefault, bool force, bool interactive,
                 std::string& error) {
    if (!force) {
        if (data::isImportCurrent(gameDir, dataDir)) return true;
        // Converted by tools/mdb/mdb2sqlite.py: no stamp, but complete.
        if (!isDefault && pathExists(dataDir / "GLOBALS" / "GLOBAL.sqlite") && !pathExists(dataDir / data::kImportStampFile))
            return true;
    }
    std::printf("converting the game databases into %s\n", utf8(dataDir).c_str());
    std::error_code ec;
    fs::create_directories(dataDir, ec);
    data::ImportReport report;
    if (!convertWithProgress(gameDir, dataDir, interactive, report, error)) return false;
    if (!report.failures.empty()) {
        error = "converting the game databases failed:";
        for (std::size_t i = 0; i < report.failures.size() && i < 5; ++i) error += "\n" + report.failures[i];
        return false;
    }
    std::printf("%zu databases converted\n", report.converted);
    return true;
}

void reportError(const std::string& message, bool interactive) {
    std::fprintf(stderr, "error: %s\n", message.c_str());
    if (interactive) SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, kTitle, message.c_str(), nullptr);
}

} // namespace openday::app::setup
