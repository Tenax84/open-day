// HD interface packs (docs/mechanics/interface.md, "HD interface"): the
// interface art exported as PNG files, and a pack made from it with an
// external upscaler (Real-ESRGAN's realesrgan-ncnn-vulkan) installed by the
// user.
#pragma once

#include <atomic>
#include <filesystem>
#include <string>

#include "app/app.hpp"

namespace openday::app {

// Every element of both interface atlases as a PNG file, named as the pack
// expects them (HdInterface::packFile). Returns the number written.
int exportInterface(const GameData& data, const std::filesystem::path& dir);

// The upscaler program: `given` (the program or its folder), else the one
// remembered in openday.cfg (`upscaler`), else realesrgan-ncnn-vulkan on the
// PATH or in <per-user dir>/realesrgan. Empty if none is found.
std::filesystem::path findUpscaler(const std::filesystem::path& given, const std::string& remembered);

// Exports the art into `workDir`, enlarges it 4× with `upscaler` and `model`,
// and moves the results into `packDir` (pictures there are replaced). An
// element the upscaler fails on is tried once more in small tiles, then keeps
// its original pixels. Returns 0 on success; prints its progress, and on
// failure stores the reason in `*error`. Setting `*cancel` stops the
// upscaler and returns 1 (pictures already moved stay). `*progress` follows
// the share of the pictures made so far (0 to 1).
int makeHdPack(const GameData& data, const std::filesystem::path& upscaler, const std::string& model,
               const std::filesystem::path& packDir, const std::filesystem::path& workDir,
               const std::atomic<bool>* cancel = nullptr, std::string* error = nullptr,
               std::atomic<float>* progress = nullptr);

} // namespace openday::app
