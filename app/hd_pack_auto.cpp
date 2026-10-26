#include "app/hd_pack_auto.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <vector>

#include "app/hd_pack.hpp"
#include "formats/sha256.hpp"
#include "formats/zip.hpp"
#include "platform/http.hpp"

namespace openday::app {
namespace fs = std::filesystem;
namespace {

// Real-ESRGAN v0.2.5.0 (BSD-3-Clause), the portable ncnn/Vulkan builds. The
// macOS program holds both x86_64 and arm64 code; Linux has an x86_64 build
// only.
struct Package {
    const char* name;
    const char* sha256;
    std::uint64_t size;
};
constexpr const char* kRelease = "https://github.com/xinntao/Real-ESRGAN/releases/download/v0.2.5.0/";
#if defined(_WIN32)
constexpr Package kPackage{"realesrgan-ncnn-vulkan-20220424-windows.zip",
                           "abc02804e17982a3be33675e4d471e91ea374e65b70167abc09e31acb412802d", 45474481};
constexpr const char* kProgram = "realesrgan-ncnn-vulkan.exe";
#elif defined(__APPLE__)
constexpr Package kPackage{"realesrgan-ncnn-vulkan-20220424-macos.zip",
                           "e0ad05580abfeb25f8d8fb55aaf7bedf552c375b5b4d9bd3c8d59764d2cc333a", 51817124};
constexpr const char* kProgram = "realesrgan-ncnn-vulkan";
#elif defined(__x86_64__)
constexpr Package kPackage{"realesrgan-ncnn-vulkan-20220424-ubuntu.zip",
                           "e5aa6eb131234b87c0c51f82b89390f5e3e642b7b70f2b9bbe95b6a285a40c96", 46931474};
constexpr const char* kProgram = "realesrgan-ncnn-vulkan";
#else
constexpr Package kPackage{nullptr, nullptr, 0};
constexpr const char* kProgram = "realesrgan-ncnn-vulkan";
#endif
constexpr const char* kModel = "realesr-animevideov3";

// The members taken out of the package: the program, its OpenMP library
// (Windows) and the one model used.
bool wanted(const std::string& name) {
    return name == kProgram || name == "vcomp140.dll" || name.rfind(std::string("models/") + kModel + "-", 0) == 0;
}

} // namespace

bool hdPackPresent(const fs::path& packDir) {
    std::error_code ec;
    for (const char* sub : {"640", "1024"}) {
        bool any = false;
        for (const fs::directory_entry& f : fs::directory_iterator(packDir / sub, ec))
            if (f.path().extension() == ".png") {
                any = true;
                break;
            }
        if (!any) return false;
    }
    return true;
}

void drawHdPackProgress(render::IndexedSurface& s, const ui::UiContext& ui, const HdPackMaker& maker) {
    const int pct = static_cast<int>(std::clamp(maker.progress(), 0.0f, 1.0f) * 100.0f);
    const std::string text =
        std::string(maker.downloading() ? "Downloading the upscaler... " : "Making the HD interface... ") + std::to_string(pct) + " %";
    const int lh = ui.standard ? ui.standard->lineHeight() : 14;
    const int w = 220, h = lh + 16, x = s.width() - w - 6, y = 6;
    auto fill = [&s](int fx, int fy, int fw, int fh, std::uint8_t c) {
        for (int r = std::max(fy, 0); r < std::min(fy + fh, s.height()); ++r)
            std::fill(s.row(r) + std::max(fx, 0), s.row(r) + std::min(fx + fw, s.width()), c);
    };
    fill(x, y, w, h, ui.grey);
    fill(x + 1, y + 1, w - 2, h - 2, ui.black);
    if (ui.standard) ui.standard->draw(s, x + 6, y + 3, text, ui.white);
    const int bx = x + 6, by = y + 5 + lh, bw = w - 12;
    fill(bx, by, bw, 6, ui.grey);
    fill(bx + 1, by + 1, (bw - 2) * pct / 100, 4, ui.highlight);
}

HdPackMaker::HdPackMaker(const GameData& data, fs::path userDir, std::string remembered)
    : userDir_(std::move(userDir)) {
    thread_ = std::thread([this, &data, r = std::move(remembered)] { work(data, r); });
}

HdPackMaker::~HdPackMaker() {
    cancel_ = true;
    if (thread_.joinable()) thread_.join();
}

fs::path HdPackMaker::download() {
    if (!kPackage.name) {
        error_ = "Real-ESRGAN has no build for this platform";
        return {};
    }
    std::vector<std::uint8_t> bytes;
    bytes.reserve(kPackage.size);
    formats::Sha256 hash;
    platform::HttpRequest req;
    req.url = std::string(kRelease) + kPackage.name;
    req.onData = [&](const std::uint8_t* d, std::size_t n) {
        if (bytes.size() + n > kPackage.size) return false;
        bytes.insert(bytes.end(), d, d + n);
        hash.update({d, n});
        return true;
    };
    req.onProgress = [this](std::uint64_t received, std::uint64_t) {
        progress_ = static_cast<float>(std::min(received, kPackage.size)) / static_cast<float>(kPackage.size);
        return !cancel_.load();
    };
    std::printf("downloading the upscaler (%s)...\n", kPackage.name);
    std::fflush(stdout);
    const platform::HttpResponse res = platform::httpGet(req);
    if (cancel_) return {};
    if (!res.ok || res.status != 200) {
        error_ = "the upscaler download failed: " + (res.error.empty() ? "HTTP status " + std::to_string(res.status) : res.error);
        std::fprintf(stderr, "%s\n", error_.c_str());
        return {};
    }
    if (bytes.size() != kPackage.size || hash.finishHex() != kPackage.sha256) {
        error_ = "the upscaler download is damaged (checksum mismatch)";
        std::fprintf(stderr, "%s\n", error_.c_str());
        return {};
    }
    // Unpacked into a fresh folder, renamed when whole.
    const fs::path dir = userDir_ / "realesrgan", part = userDir_ / "realesrgan.partial";
    std::error_code ec;
    fs::remove_all(part, ec);
    try {
        const formats::ZipArchive zip = formats::ZipArchive::fromBytes(std::move(bytes));
        for (const formats::ZipEntry& e : zip.entries()) {
            if (e.isDirectory() || !wanted(e.name)) continue;
            const fs::path file = part / fs::path(e.name);
            fs::create_directories(file.parent_path());
            const std::vector<std::uint8_t> data = zip.read(e);
            std::ofstream out(file, std::ios::binary);
            out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
            if (!out) throw std::runtime_error("cannot write " + file.string());
        }
    } catch (const std::exception& e) {
        error_ = std::string("cannot unpack the upscaler: ") + e.what();
        std::fprintf(stderr, "%s\n", error_.c_str());
        fs::remove_all(part, ec);
        return {};
    }
#ifndef _WIN32
    fs::permissions(part / kProgram, fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
                    fs::perm_options::add, ec);
#endif
    fs::remove_all(dir, ec);
    fs::rename(part, dir, ec);
    if (ec) {
        error_ = "cannot store the upscaler: " + ec.message();
        std::fprintf(stderr, "%s\n", error_.c_str());
        fs::remove_all(part, ec);
        return {};
    }
    return dir / kProgram;
}

void HdPackMaker::work(const GameData& data, const std::string& remembered) {
    fs::path upscaler = findUpscaler({}, remembered);
    if (upscaler.empty()) {
        downloading_ = true;
        upscaler = download();
        downloading_ = false;
        progress_ = 0;
    }
    if (cancel_) return outcome_.store(Outcome::Cancelled);
    if (upscaler.empty()) return outcome_.store(Outcome::Failed);
    upscaler_ = upscaler;
    const int rc = makeHdPack(data, upscaler, kModel, userDir_ / "hd-interface", userDir_ / "hd-interface.work", &cancel_, &error_, &progress_);
    if (cancel_) return outcome_.store(Outcome::Cancelled);
    outcome_.store(rc == 0 ? Outcome::Made : Outcome::Failed);
}

} // namespace openday::app
