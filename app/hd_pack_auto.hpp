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
#include "render/indexed_surface.hpp"
#include "ui/widgets.hpp"

namespace openday::app {

class HdPackMaker {
public:
    enum class Outcome : int {
        Running,
        Made,      // the pack is there
        Failed,    // no upscaler, or it made nothing usable (error() says why)
        Cancelled, // the game was closed first
    };

    // Starts at once. `remembered`: the `upscaler` of openday.cfg.
    HdPackMaker(const GameData& data, std::filesystem::path userDir, std::string remembered);
    // Stops the download or the upscaler and waits for the thread.
    ~HdPackMaker();
    HdPackMaker(const HdPackMaker&) = delete;
    HdPackMaker& operator=(const HdPackMaker&) = delete;

    Outcome outcome() const { return outcome_.load(); }
    // While running: whether the upscaler is being downloaded (else the
    // pictures are being made), and how far that step is (0 to 1).
    bool downloading() const { return downloading_.load(); }
    float progress() const { return progress_.load(); }
    // The upscaler used, and why it failed (valid once the outcome is in).
    const std::filesystem::path& upscaler() const { return upscaler_; }
    const std::string& error() const { return error_; }

private:
    void work(const GameData& data, const std::string& remembered);
    std::filesystem::path download();

    std::filesystem::path userDir_, upscaler_;
    std::string error_;
    std::atomic<Outcome> outcome_{Outcome::Running};
    std::atomic<bool> cancel_{false}, downloading_{false};
    std::atomic<float> progress_{0};
    std::thread thread_;
};

// The pack folder holds pictures of both resolutions (made earlier, or by
// hand); otherwise the pack is missing or was left half made.
bool hdPackPresent(const std::filesystem::path& packDir);

// The progress strip of a running maker, drawn in the top right corner of the
// frame.
void drawHdPackProgress(render::IndexedSurface& s, const ui::UiContext& ui, const HdPackMaker& maker);

} // namespace openday::app
