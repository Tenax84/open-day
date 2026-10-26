// The HD interface pack made by itself (docs/mechanics/interface.md, "HD
// interface", "Automatic pack"): on a worker thread, Real-ESRGAN's portable
// realesrgan-ncnn-vulkan is found or downloaded from its own GitHub release
// (pinned, checked by SHA-256) into <per-user dir>/realesrgan, and the pack
// is made from the user's interface art (makeHdPack) while the game runs.
#pragma once

#include <atomic>
#include <filesystem>
#include <string>
#include <thread>

#include "app/app.hpp"

namespace openday::app {

class HdPackMaker {
public:
    enum class Outcome : int {
        Running,
        Made,           // the pack is there
        UpscalerFailed, // the upscaler ran but made nothing usable (no Vulkan device, say)
        NotAvailable,   // no upscaler: none for this platform, or the download failed
        Cancelled,      // the game was closed first
    };

    // Starts at once. `remembered`: the `upscaler` of openday.cfg.
    HdPackMaker(const GameData& data, std::filesystem::path userDir, std::string remembered);
    // Stops the download or the upscaler and waits for the thread.
    ~HdPackMaker();
    HdPackMaker(const HdPackMaker&) = delete;
    HdPackMaker& operator=(const HdPackMaker&) = delete;

    Outcome outcome() const { return outcome_.load(); }
    // The upscaler used (valid once the outcome is in).
    const std::filesystem::path& upscaler() const { return upscaler_; }

private:
    void work(const GameData& data, const std::string& remembered);
    std::filesystem::path download();

    std::filesystem::path userDir_, upscaler_;
    std::atomic<Outcome> outcome_{Outcome::Running};
    std::atomic<bool> cancel_{false};
    std::thread thread_;
};

// The pack folder holds pictures already (made earlier, or by hand).
bool hdPackPresent(const std::filesystem::path& packDir);

} // namespace openday::app
