// HD interface packs (docs/mechanics/interface.md, "HD interface"): the
// interface art exported as PNG files, and a pack made from it with an
// external upscaler (Real-ESRGAN's realesrgan-ncnn-vulkan) installed by the
// user.
#pragma once

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
// element the upscaler fails on is tried once more in small tiles, then left
// to xBRZ. Returns 0 on success; prints its progress.
int makeHdPack(const GameData& data, const std::filesystem::path& upscaler, const std::string& model,
               const std::filesystem::path& packDir, const std::filesystem::path& workDir);

} // namespace openday::app
