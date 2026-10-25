// Updates from the GitHub releases of OpenDay (docs/updates.md): asks the
// GitHub API for the latest release, downloads the package of this
// platform, verifies its SHA-256 and installs it:
//   Windows: runs the downloaded installer (OpenDay quits meanwhile);
//   macOS:   replaces the OpenDay.app bundle from the disk image, restarts;
//   Linux:   unpacks the archive over the unpacked installation, restarts.
// When the program cannot update itself (a development build, a read-only
// location, no package for this platform), the release page is offered.
// The network work runs on a worker thread; the screens poll status().
#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace openday::app {

namespace fs = std::filesystem;

class Updater {
public:
    enum class State {
        Idle,
        Checking,
        UpToDate,
        Available,   // a newer release; download() or openReleasePage()
        Downloading, // download, verification and installation
        Ready,       // installed (macOS, Linux) or installer ready (Windows): finish()
        Failed,
    };
    struct Status {
        State state = State::Idle;
        std::string current;      // this program's version
        std::string latest;       // the newest release's version
        std::string pageUrl;      // its release page
        bool installable = false; // the program can update itself
        std::string error;        // Failed: why; Available and not installable: why not
        std::uint64_t received = 0, total = 0; // Downloading
    };

    // `workDir`: where the packages are downloaded (emptied on start).
    Updater(fs::path workDir, std::string currentVersion);
    ~Updater();
    Updater(const Updater&) = delete;
    Updater& operator=(const Updater&) = delete;

    Status status() const;
    // Starts the check (Idle, UpToDate or Failed before a release was found).
    void check();
    // Starts the download and installation (Available, or Failed after it).
    void download();
    // Stops a running download (back to Available).
    void cancel();
    // Ready: starts the installer (Windows) or the new version. Returns true
    // if the program should quit now; false with `error` otherwise.
    bool finish(std::string& error);
    // Opens the release page (or the releases list) in the browser.
    void openReleasePage() const;

private:
    struct Shared;
    std::shared_ptr<Shared> shared_;
};

} // namespace openday::app
