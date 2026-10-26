#include "app/hd_pack.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "app/hd_interface.hpp"
#include "app/setup.hpp"
#include "formats/png_writer.hpp"
#include "render/indexed_surface.hpp"
#include "ui/atlas.hpp"

namespace openday::app {
namespace fs = std::filesystem;
namespace {

#ifdef _WIN32
constexpr const char* kProgram = "realesrgan-ncnn-vulkan.exe";
constexpr char kPathSeparator = ';';
#else
constexpr const char* kProgram = "realesrgan-ncnn-vulkan";
constexpr char kPathSeparator = ':';
#endif

std::string utf8(const fs::path& p) {
    const std::u8string s = p.u8string();
    return std::string(s.begin(), s.end());
}

bool isFile(const fs::path& p) {
    std::error_code ec;
    return !p.empty() && fs::is_regular_file(p, ec);
}

// The program itself, or the program in a folder.
fs::path programIn(const fs::path& p) {
    std::error_code ec;
    if (fs::is_directory(p, ec)) return isFile(p / kProgram) ? p / kProgram : fs::path();
    return isFile(p) ? p : fs::path();
}

bool cancelled(const std::atomic<bool>* cancel) { return cancel && cancel->load(); }

// Prints the reason of a failure and hands it to the caller.
void fail(std::string* error, const std::string& why) {
    std::fprintf(stderr, "%s\n", why.c_str());
    if (error) *error = why;
}

#ifdef _WIN32
// Windows: started directly, so that it gets no console window when the
// game was not started from one (SDL's processes get one unless they run
// in the background, and then their exit code is lost).
int runProgram(const std::vector<std::string>& args, const std::atomic<bool>* cancel, std::string* error,
               const std::function<void()>& poll) {
    std::wstring line;
    for (const std::string& a : args) {
        const fs::path p(std::u8string(reinterpret_cast<const char8_t*>(a.c_str())));
        if (!line.empty()) line += L' ';
        line += L'"' + p.wstring() + L'"'; // no argument holds a quote or ends with a backslash
    }
    STARTUPINFOW si{};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, line.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS, nullptr,
                        nullptr, &si, &pi)) {
        fail(error, "cannot start " + args[0].substr(args[0].find_last_of("/\\") + 1) + ": error " + std::to_string(GetLastError()));
        return -1;
    }
    CloseHandle(pi.hThread);
    while (WaitForSingleObject(pi.hProcess, 100) == WAIT_TIMEOUT) {
        if (cancelled(cancel)) TerminateProcess(pi.hProcess, 1);
        if (poll) poll();
    }
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    return cancelled(cancel) ? -1 : static_cast<int>(code);
}
#else
int runProgram(const std::vector<std::string>& args, const std::atomic<bool>* cancel, std::string* error,
               const std::function<void()>& poll) {
    std::vector<const char*> argv;
    for (const std::string& a : args) argv.push_back(a.c_str());
    argv.push_back(nullptr);
    const SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, argv.data());
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_NULL);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_NUMBER, SDL_PROCESS_STDIO_NULL);
    SDL_Process* p = SDL_CreateProcessWithProperties(props);
    SDL_DestroyProperties(props);
    if (!p) {
        fail(error, "cannot start " + args[0].substr(args[0].find_last_of("/\\") + 1) + ": " + SDL_GetError());
        return -1;
    }
    int code = -1;
    while (!SDL_WaitProcess(p, false, &code)) {
        if (cancelled(cancel)) {
            SDL_KillProcess(p, true);
            SDL_WaitProcess(p, true, &code);
            break;
        }
        if (poll) poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    SDL_DestroyProcess(p);
    return cancelled(cancel) ? -1 : code;
}
#endif

// Runs the upscaler on every PNG of `in`, writing PNGs of the same names into
// `out`. `tile` 0 = the program's choice. Its own output is not shown. The
// models are taken from the program's own folder. `poll` is called about ten
// times a second while it runs.
bool runUpscaler(const fs::path& upscaler, const std::string& model, const fs::path& in, const fs::path& out, int tile,
                 const std::atomic<bool>* cancel, std::string* error, const std::function<void()>& poll = {}) {
    std::vector<std::string> args = {utf8(upscaler), "-i", utf8(in), "-o", utf8(out), "-n", model, "-s", "4",
                                     "-t", std::to_string(tile), "-f", "png"};
    std::error_code ec;
    if (const fs::path models = upscaler.parent_path() / "models"; fs::is_directory(models, ec)) {
        args.push_back("-m");
        args.push_back(utf8(models));
    }
    std::string why;
    const int code = runProgram(args, cancel, &why, poll);
    if (code != 0 && !cancelled(cancel)) {
        if (why.empty()) fail(error, "the upscaler ended with code " + std::to_string(code));
        else if (error) *error = why; // printed already
    }
    return code == 0;
}

// An upscaled picture is usable if it is a whole multiple (2× or more) of
// the original and not empty where the original is not (a GPU running out
// of memory gives black or transparent pictures).
bool usable(const fs::path& original, const fs::path& upscaled) {
    const HdInterface::Picture a = HdInterface::loadPicture(original);
    const HdInterface::Picture b = HdInterface::loadPicture(upscaled);
    if (a.width <= 0 || b.width <= 0 || b.width % a.width != 0 || b.height % a.height != 0) return false;
    const int n = b.width / a.width;
    if (n < 2 || b.height / a.height != n) return false;
    auto empty = [](const HdInterface::Picture& p) {
        return std::all_of(p.argb.begin(), p.argb.end(), [](std::uint32_t c) { return (c & 0xff000000u) == 0 || (c & 0xffffffu) == 0; });
    };
    return !empty(b) || empty(a);
}

} // namespace

int exportInterface(const GameData& data, const fs::path& dir) {
    int count = 0;
    for (const int resolution : {0, 1}) {
        const fs::path smk = data.file(resolution == 1 ? "INTERF/1024X768.SMK" : "INTERF/640X480.SMK");
        if (!fs::exists(smk)) continue;
        const ui::InterfaceAtlas atlas = ui::InterfaceAtlas::load(smk, atlasElements(resolution));
        for (const auto& [frame, sprite] : atlas.elements()) {
            const fs::path file = HdInterface::packFile(dir, resolution * 100000 + frame);
            fs::create_directories(file.parent_path());
            render::IndexedSurface s(sprite.width, sprite.height);
            s.blit(sprite, 0, 0);
            formats::writePngRgba(file, s.width(), s.height(), s.toRgba(data.palette));
            ++count;
        }
    }
    return count;
}

fs::path findUpscaler(const fs::path& given, const std::string& remembered) {
    if (!given.empty()) return programIn(given);
    if (!remembered.empty()) {
        if (fs::path p = programIn(fs::path(std::u8string(reinterpret_cast<const char8_t*>(remembered.c_str())))); !p.empty()) return p;
    }
    if (const char* path = std::getenv("PATH")) {
        const std::string all = path;
        for (std::size_t start = 0; start <= all.size();) {
            const std::size_t end = std::min(all.find(kPathSeparator, start), all.size());
            if (end > start) {
                if (fs::path p = programIn(fs::path(all.substr(start, end - start))); !p.empty()) return p;
            }
            start = end + 1;
        }
    }
    return programIn(setup::userDir() / "realesrgan");
}

int makeHdPack(const GameData& data, const fs::path& upscaler, const std::string& model, const fs::path& packDir,
               const fs::path& workDir, const std::atomic<bool>* cancel, std::string* error, std::atomic<float>* progress) {
    std::error_code ec;
    fs::remove_all(workDir, ec);
    const fs::path in = workDir / "in", out = workDir / "out";
    const int total = exportInterface(data, in);
    if (total == 0) {
        fail(error, "no interface art found");
        return 1;
    }
    int made = 0, failed = 0;
    int finished = 0; // pictures of the resolutions already done
    auto report = [&](int done) {
        if (progress) progress->store(static_cast<float>(std::min(done, total)) / static_cast<float>(total));
    };
    for (const char* sub : {"640", "1024"}) {
        if (!fs::is_directory(in / sub, ec)) continue;
        std::vector<fs::path> names;
        for (const fs::directory_entry& f : fs::directory_iterator(in / sub, ec)) names.push_back(f.path().filename());
        std::printf("upscaling %zu pictures (%s) with %s...\n", names.size(), sub, model.c_str());
        std::fflush(stdout);
        fs::create_directories(out / sub, ec);
        // Progress: the pictures the upscaler has written so far.
        int polls = 0;
        auto poll = [&] {
            if (++polls % 5 != 0) return;
            std::error_code e;
            int n = 0;
            for (const fs::directory_entry& f : fs::directory_iterator(out / sub, e))
                if (f.path().extension() == ".png") ++n;
            report(finished + n);
        };
        if (!runUpscaler(upscaler, model, in / sub, out / sub, 0, cancel, error, poll)) {
            fs::remove_all(workDir, ec);
            return 1;
        }
        // Failures once more, in small tiles (less GPU memory).
        std::vector<fs::path> retry;
        for (const fs::path& n : names)
            if (!usable(in / sub / n, out / sub / n)) retry.push_back(n);
        if (!retry.empty() && !cancelled(cancel)) {
            std::printf("trying %zu pictures again in small tiles...\n", retry.size());
            std::fflush(stdout);
            const fs::path again = workDir / "again" / sub;
            fs::create_directories(again, ec);
            for (const fs::path& n : retry) {
                fs::remove(out / sub / n, ec);
                fs::copy_file(in / sub / n, again / n, ec);
            }
            runUpscaler(upscaler, model, again, out / sub, 32, cancel, nullptr);
        }
        if (cancelled(cancel)) {
            fs::remove_all(workDir, ec);
            return 1;
        }
        fs::create_directories(packDir / sub, ec);
        for (const fs::path& n : names) {
            if (usable(in / sub / n, out / sub / n)) {
                fs::copy_file(out / sub / n, packDir / sub / n, fs::copy_options::overwrite_existing, ec);
                if (!ec) {
                    ++made;
                    continue;
                }
            }
            ++failed;
        }
        finished += static_cast<int>(names.size());
        report(finished);
    }
    fs::remove_all(workDir, ec);
    std::printf("%d of %d pictures written to %s", made, total, utf8(packDir).c_str());
    if (failed > 0) std::printf(" (%d keep their original pixels)", failed);
    std::printf("\n");
    if (made == 0) {
        fail(error, "the upscaler made no usable pictures (no Vulkan-capable GPU?)");
        return 1;
    }
    return 0;
}

} // namespace openday::app
