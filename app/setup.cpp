#include "app/setup.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <mutex>
#include <string>
#include <system_error>
#include <thread>
#include <utility>

#include "app/gog_download.hpp"
#include "app/setup_ui.hpp"
#include "data/game_source.hpp"
#include "data/mdb_import.hpp"
#include "formats/game_path.hpp"
#include "platform/web_sign_in.hpp"
#include "update/gog.hpp"

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

const std::string kFooter = "OpenDay " OPENDAY_VERSION " - not affiliated with the original publisher";

// The pages' panels (screen coordinates).
constexpr ui::Rect kPagePanel{100, 34, 440, 408};
constexpr ui::Rect kProgressPanel{100, 158, 440, 164};
constexpr int kButtonW = 256, kButtonH = 28;

// The page of a long piece of work (the copy, the conversion): runs `work`
// on a worker thread while the page shows `status`'s line and fraction.
// Closing the window cannot stop the work half way; the program ends after
// it.
void runWithProgress(const char* title, const char* text, const std::function<void()>& work,
                     const std::function<std::pair<std::string, float>()>& status) {
    SetupUi* ui = SetupUi::open();
    if (!ui) {
        work();
        return;
    }
    std::atomic<bool> finished{false};
    std::thread worker([&] {
        work();
        finished = true;
    });
    while (!finished) {
        ui->begin();
        const auto [line, fraction] = status();
        const ui::Rect in = ui->panel(kProgressPanel, title);
        const ui::Rect well{in.x, in.y, in.w, 76};
        ui->well(well);
        ui->text({well.x + 8, well.y + 8, well.w - 16, 32}, text);
        ui->text({well.x + 8, well.y + 50, well.w - 16, 16}, line, SetupUi::kTitle);
        ui->progress({in.x, well.y + well.h + 14, in.w, 18}, fraction);
        ui->footer(kFooter);
        ui->end();
    }
    worker.join();
}

// Tells the user that the system has no file dialog.
void noFileDialog(SetupUi& ui) {
    ui.message("NO FILE DIALOG", std::string("The file dialog is not available: ") + SDL_GetError() +
                                     "\n\nStart OpenDay with --game-dir <folder> or --install-from <file> instead.");
}

// --- Download from GOG -------------------------------------------------------
//
// The user signs in on GOG's own page (in a panel inside the window where
// the system has one, else in the web browser, copying the address the page
// ends on), and OpenDay downloads the installer of their copy into the
// downloads folder and copies the game out of it (docs/gog-download.md).

constexpr const char* kGogStoreUrl = "https://www.gog.com/en/game/fallen_haven_liberation_day";

fs::path downloadsDir() {
    if (const char* d = SDL_GetUserFolder(SDL_FOLDER_DOWNLOADS); d && *d) return utf8Path(d);
#ifdef _WIN32
    const fs::path home = envPath("USERPROFILE");
#else
    const fs::path home = homeDir();
#endif
    return home.empty() ? fs::path() : home / "Downloads";
}

std::string clipboardText() {
    if (!SDL_HasClipboardText()) return {};
    char* text = SDL_GetClipboardText();
    std::string out = text ? text : "";
    SDL_free(text);
    return out;
}

std::string megabytes(std::uint64_t bytes) {
    char text[32];
    std::snprintf(text, sizeof text, "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    return text;
}

enum class GogResult { Found, Cancel };

enum class Choice { None, Folder, Installer, Gog, Quit };

// Draws the first page; returns the choice clicked. With `live` false the
// page takes no pointer (the GOG sign-in panel is over it).
Choice firstPage(SetupUi& ui, bool live) {
    struct Item {
        Choice choice;
        const char* label;
        std::string help;
    };
    const Item items[] = {
        {Choice::Folder, "GAME FOLDER...",
         "An installation of the game, or a copy of its folder: the one that holds GLOBALS, IMGS, INTERF and the other "
         "game folders."},
        {Choice::Installer, "INSTALLER OR CD IMAGE...",
         "The GOG installer (setup_*.exe, with its .bin files next to it) or an image of the game CD (.iso, .bin). "
         "OpenDay copies the game files out of it into " + utf8(userDir() / "game") + "."},
        {Choice::Gog, "DOWNLOAD FROM GOG...",
         "Sign in to GOG: OpenDay downloads the installer of your copy into your downloads folder and copies the game "
         "files out of it."},
        {Choice::Quit, "QUIT", "You can also start OpenDay with --game-dir <folder> or --install-from <file>."},
    };
    if (!live) ui.ignoreMouse();
    const ui::Rect in = ui.panel(kPagePanel, "OPENDAY");
    const ui::Rect intro{in.x, in.y, in.w, 96};
    ui.well(intro);
    ui.text({intro.x + 8, intro.y + 8, intro.w - 16, intro.h - 16},
            "OpenDay plays \"Fallen Haven: Liberation Day\" with the files of the original game (for example the "
            "GOG release). They are not part of OpenDay.\n\nWhere is your copy of the game?");
    Choice choice = Choice::None;
    std::string help = "Point at a choice to read about it.";
    int y = intro.y + intro.h + 14;
    for (const Item& item : items) {
        if (item.choice == Choice::Quit) y += 8;
        const ui::Rect r{(SetupUi::kWidth - kButtonW) / 2, y, kButtonW, kButtonH};
        if (ui.button(r, item.label)) choice = item.choice;
        if (ui.hovered(r)) help = item.help;
        y += kButtonH + 6;
    }
    const ui::Rect helpWell{in.x, y + 10, in.w, in.y + in.h - y - 10};
    ui.well(helpWell);
    ui.text({helpWell.x + 8, helpWell.y + 8, helpWell.w - 16, helpWell.h - 16}, help, SetupUi::kTextDim);
    ui.footer(kFooter);
    return choice;
}

// The page of the GOG download. Returns Found with the installer in `found`
// once it is downloaded whole. While the sign-in panel is open, the first
// page stays under it as it was.
GogResult downloadFromGog(SetupUi& ui, fs::path& found) {
    fs::path folder = downloadsDir();
    if (folder.empty()) folder = userDir() / "downloads";

    enum class Mode { Panel, Browser, Downloading };
    enum Action { kNone, kCancel, kPaste, kOpenAgain, kStore };
    const bool panelAvailable = platform::web_sign_in::available();
    void* nativeWindow = ui.nativeWindow();
    platform::web_sign_in::Window panel;
    gog::Download download;
    Mode mode = Mode::Browser;
    std::string clipboardBefore; // a code there before the page opened is not taken
    std::string note;            // a line under the instructions

    // Opens GOG's sign-in page: in the panel, else in the web browser.
    const auto signIn = [&] {
        note.clear();
        if (panelAvailable && nativeWindow) {
            platform::web_sign_in::Request request;
            request.address = update::gog::signInAddress();
            request.landing = std::string(update::gog::landingAddress());
            request.title = "Sign in to GOG";
            // The colours of the setup window (setup_ui.cpp).
            request.look = {0x0a0c08, 0x565430, 0xe2c670, 0xc8b676, 0x3e341c, 0xece2b2, 0x969696, 0x6a6838, 0x424022};
            request.parent = nativeWindow;
            if (panel.open(request)) {
                mode = Mode::Panel;
                return;
            }
        }
        clipboardBefore = clipboardText();
        SDL_OpenURL(update::gog::signInAddress().c_str());
        mode = Mode::Browser;
    };
    const auto tryCode = [&](const std::string& text, bool tell) {
        if (const auto code = update::gog::loginCode(text)) {
            clipboardBefore = text; // a code is used once
            std::printf("downloading the GOG installer into %s\n", utf8(folder).c_str());
            download.start(*code, folder);
            mode = Mode::Downloading;
            return true;
        }
        if (tell) note = "The clipboard holds no GOG sign-in address.";
        return false;
    };
    // A failure, told in a dialog; true when the user tries again.
    const auto tryAgain = [&](gog::Failure failure) {
        panel.close();
        return ui.dialog("DOWNLOAD FROM GOG",
                         std::string(gog::failureText(failure)) + "\n\nA broken-off download is resumed by the next try.",
                         {"TRY AGAIN", "CLOSE"}, 1) == 0;
    };

    signIn();
    GogResult result = GogResult::Cancel;
    Uint64 lastPoll = 0;
    for (;;) {
        ui.begin();
        if (ui.closing() || (ui.escapePressed() && mode != Mode::Panel)) break;

        Action action = kNone;
        if (mode == Mode::Panel) {
            firstPage(ui, false);
        } else {
            const ui::Rect in = ui.panel(kPagePanel, "DOWNLOAD FROM GOG");
            const ui::Rect well{in.x, in.y, in.w, mode == Mode::Browser ? 200 : 120};
            ui.well(well);
            const ui::Rect inWell{well.x + 8, well.y + 8, well.w - 16, well.h - 16};
            const int buttonY = in.y + in.h - kButtonH;
            const int left = in.x, right = in.x + in.w - 200;
            if (mode == Mode::Browser) {
                const int used = ui.text(
                    inWell, "GOG's sign-in page is open in your web browser. Sign in there; GOG then shows an empty white page. "
                            "Copy its address from the browser's address bar: OpenDay notices it on the clipboard by itself "
                            "(or press PASTE ADDRESS).\n\nOpenDay then downloads the installer of your copy of the game into " +
                                utf8(folder) + " and copies the game files out of it. The sign-in is kept for this download only.");
                if (!note.empty()) ui.text({inWell.x, inWell.y + used + 6, inWell.w, 16}, note, SetupUi::kNote);
                if (ui.button({left, buttonY - kButtonH - 10, 200, kButtonH}, "PASTE ADDRESS")) action = kPaste;
                if (ui.button({right, buttonY - kButtonH - 10, 200, kButtonH}, "OPEN GOG AGAIN")) action = kOpenAgain;
                if (ui.button({left, buttonY, 200, kButtonH}, "GOG STORE PAGE")) action = kStore;
                if (ui.button({right, buttonY, 200, kButtonH}, "CANCEL")) action = kCancel;
            } else {
                const gog::Status status = download.status();
                if (status.phase == gog::Phase::SigningIn) ui.text(inWell, "Signing in to GOG...");
                else if (status.phase == gog::Phase::Finding) ui.text(inWell, "Asking GOG for the installer...");
                else ui.text(inWell, "Downloading " + status.file + " into " + utf8(folder) + ".");
                if (status.size > 0)
                    ui.text({inWell.x, well.y + well.h - 24, inWell.w, 16},
                            megabytes(status.received) + " of " + megabytes(status.size), SetupUi::kTitle);
                const float fraction = status.size ? static_cast<float>(static_cast<double>(status.received) /
                                                                        static_cast<double>(status.size))
                                                   : 0.0f;
                ui.progress({in.x, well.y + well.h + 14, in.w, 18}, fraction);
                if (ui.button({right, buttonY, 200, kButtonH}, "CANCEL")) action = kCancel;
            }
            ui.footer(kFooter);
        }
        ui.end();

        if (action == kCancel) break;
        if (action == kPaste) tryCode(clipboardText(), true);
        else if (action == kOpenAgain) signIn();
        else if (action == kStore) SDL_OpenURL(kGogStoreUrl);

        if (mode == Mode::Panel) {
            using platform::web_sign_in::State;
            const State state = panel.state();
            if (state == State::Landed) {
                if (!tryCode(panel.landedAddress(), false)) {
                    if (!tryAgain(gog::Failure::SignIn)) break;
                    signIn();
                }
            } else if (state == State::Closed) {
                break;
            } else if (state == State::Failed) {
                clipboardBefore = clipboardText();
                SDL_OpenURL(update::gog::signInAddress().c_str());
                mode = Mode::Browser;
            }
        } else if (mode == Mode::Browser) {
            // The address noticed on the clipboard by itself.
            if (const Uint64 t = SDL_GetTicks(); t - lastPoll >= 500) {
                lastPoll = t;
                if (std::string text = clipboardText(); text != clipboardBefore) tryCode(text, false);
            }
        } else {
            const gog::Status status = download.status();
            if (status.phase == gog::Phase::Done) {
                found = status.installer;
                result = GogResult::Found;
                break;
            }
            if (status.phase == gog::Phase::Failed) {
                std::fprintf(stderr, "GOG download failed: %s\n", status.detail.c_str());
                if (!tryAgain(status.failure)) break;
                signIn();
            }
        }
    }
    panel.close();
    download.cancel();
    return result;
}

// The first page: asks for the game folder, or for an installer or CD image
// to copy the game from. Returns empty if the user gives up (`quit` set) or
// no window can be shown.
fs::path askForGameDir(bool& quit) {
    SetupUi* ui = SetupUi::open();
    if (!ui) return {};
    for (;;) {
        ui->begin();
        if (ui->closing() || ui->escapePressed()) {
            quit = true;
            return {};
        }
        const Choice action = firstPage(*ui, true);
        ui->end();

        if (action == Choice::Quit) {
            quit = true;
            return {};
        }
        if (action == Choice::Folder) {
            bool unavailable = false;
            const fs::path chosen = ui->chooseFolder(unavailable);
            if (unavailable) noFileDialog(*ui);
            if (chosen.empty()) continue;
            if (fs::path dir = validGameDir(chosen); !dir.empty()) return dir;
            ui->message("NOT A GAME FOLDER", "This folder does not contain the game files:\n" + utf8(chosen) +
                                                 "\n\nChoose the folder that holds GLOBALS, IMGS and INTERF.");
        } else if (action == Choice::Installer || action == Choice::Gog) {
            fs::path installer;
            if (action == Choice::Gog) {
                if (downloadFromGog(*ui, installer) == GogResult::Cancel) continue;
            } else {
                bool unavailable = false;
                installer = ui->chooseFile({{"Game installer or CD image", "exe;iso;bin;img"}, {"All files", "*"}}, unavailable);
                if (unavailable) noFileDialog(*ui);
            }
            if (installer.empty()) continue;
            std::string error;
            if (fs::path dir = installGameFiles(installer, true, error); !dir.empty()) return dir;
            ui->message("CANNOT USE THE FILE", error);
        }
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
    runWithProgress("PREPARING THE GAME DATA",
                    "First start: converting the game databases. This happens once and takes a few seconds.", work, [&] {
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
        runWithProgress("COPYING THE GAME FILES",
                        "Copying the game files out of the installer or CD image. This happens once and takes a minute "
                        "or two.",
                        work, [&] {
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
    const bool firstRun = interactive && config.getInt("first_run", 0) != 0;
    if (!firstRun) {
        if (const std::string remembered = config.get("game_dir"); !remembered.empty())
            if (fs::path dir = validGameDir(utf8Path(remembered.c_str())); !dir.empty()) return dir;
        for (const fs::path& candidate : defaultGameDirs())
            if (fs::path dir = validGameDir(candidate); !dir.empty()) return dir;
    }

    if (interactive) {
        bool quit = false;
        if (fs::path dir = askForGameDir(quit); !dir.empty()) {
            config.set("game_dir", utf8(fs::absolute(dir)));
            config.erase("first_run");
            return dir;
        }
        if (quit) {
            error.clear();
            return {};
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
    if (!interactive) return;
    if (SetupUi* ui = SetupUi::open()) ui->message("OPENDAY", message);
    else SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, kTitle, message.c_str(), nullptr);
}

void placeWindow(SDL_Window* window, int scale) {
    SDL_Rect area{};
    if (!SDL_GetDisplayUsableBounds(SDL_GetDisplayForWindow(window), &area) || area.w <= 0 || area.h <= 0) {
        SDL_SetWindowSize(window, 640 * std::max(scale, 1), 480 * std::max(scale, 1));
        SDL_ShowWindow(window);
        return;
    }
    int top = 0, left = 0, bottom = 0, right = 0;
    if (!SDL_GetWindowBordersSize(window, &top, &left, &bottom, &right) || top == 0) {
        top = 32; // typical title bar when the borders are not known yet
        left = right = bottom = 8;
    }
    const int maxW = area.w - left - right, maxH = area.h - top - bottom;
    const int fit = std::max(1, std::min(maxW / 640, maxH / 480));
    int w = 0, h = 0;
    if (scale > 0 || fit >= 2) {
        const int s = scale > 0 ? std::min(scale, fit) : fit;
        w = 640 * s;
        h = 480 * s;
    } else {
        h = std::max(240, std::min(maxH, maxW * 3 / 4));
        w = h * 4 / 3;
    }
    SDL_SetWindowSize(window, w, h);
    SDL_SetWindowPosition(window, area.x + left + std::max(0, (maxW - w) / 2), area.y + top + std::max(0, (maxH - h) / 2));
    SDL_ShowWindow(window);
}

} // namespace openday::app::setup
