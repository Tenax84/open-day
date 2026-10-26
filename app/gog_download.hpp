// Downloading the user's own Windows installer of the game from their GOG
// account (docs/gog-download.md), on a worker thread: the sign-in code is
// exchanged for a token, the token asks GOG for the installer's address,
// and the installer (with any .bin parts) is downloaded into a folder,
// resuming a .part an earlier try left. Nothing in it is run; the setup
// copies the game out of it. The token is kept in memory, for this one
// download.
#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace openday::app::gog {

enum class Phase : std::uint8_t {
    Idle,
    SigningIn,   // the code is exchanged for a token
    Finding,     // GOG is asked where the installer is
    Downloading, // the installer's bytes are arriving
    Done,        // the installer is whole, at Status::installer
    Failed,      // Status::failure says why
};

enum class Failure : std::uint8_t {
    None,
    SignIn,      // GOG did not accept the code (used once, expires soon)
    NotOwned,    // the account does not hold the game
    NoInstaller, // GOG lists no Windows installer for it
    Network,     // GOG could not be reached, or the connection broke
    Unexpected,  // an answer this reader does not know
    Disk,        // the file could not be written
    NoSpace,     // the disk has no room for the installer
    Cancelled,
};

struct Status {
    Phase phase = Phase::Idle;
    Failure failure = Failure::None;
    std::uint64_t received = 0; // bytes of the installer (all its files) on disk
    std::uint64_t size = 0;     // all its files; 0 until GOG has said
    std::string file;           // the file arriving
    std::filesystem::path installer; // the installer (.exe) when Done
    std::string detail;         // what went wrong, in English, for the log
};

// The sentence the user reads for a failure; empty for None.
const char* failureText(Failure failure);

// One download, on its own thread. Destroying it cancels a download still
// running and waits for the thread.
class Download {
public:
    Download();
    ~Download();
    Download(const Download&) = delete;
    Download& operator=(const Download&) = delete;

    // Signs in with `code` and downloads the installer into `folder` (made
    // when missing), under the names GOG publishes, with a ".part" name while
    // a file is incomplete. A whole file already there of the size GOG names
    // is taken as it is. A download still running is cancelled first.
    void start(std::string code, std::filesystem::path folder);

    // Asks a running download to stop (Failure::Cancelled). The part stays,
    // so a later try resumes it.
    void cancel();

    Status status() const;

private:
    struct State;
    std::shared_ptr<State> state_;
};

} // namespace openday::app::gog
