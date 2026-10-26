#include "app/hd_pack_auto.hpp"

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
        for (const fs::directory_entry& f : fs::directory_iterator(packDir / sub, ec))
            if (f.path().extension() == ".png") return true;
    }
    return false;
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
    if (!kPackage.name) return {};
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
    req.onProgress = [this](std::uint64_t, std::uint64_t) { return !cancel_.load(); };
    std::printf("downloading the upscaler (%s)...\n", kPackage.name);
    std::fflush(stdout);
    const platform::HttpResponse res = platform::httpGet(req);
    if (cancel_) return {};
    if (!res.ok || res.status != 200) {
        const std::string why = res.error.empty() ? "HTTP status " + std::to_string(res.status) : res.error;
        std::fprintf(stderr, "upscaler download failed: %s\n", why.c_str());
        return {};
    }
    if (bytes.size() != kPackage.size || hash.finishHex() != kPackage.sha256) {
        std::fprintf(stderr, "upscaler download damaged (checksum mismatch)\n");
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
        std::fprintf(stderr, "cannot unpack the upscaler: %s\n", e.what());
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
        std::fprintf(stderr, "cannot store the upscaler: %s\n", ec.message().c_str());
        fs::remove_all(part, ec);
        return {};
    }
    return dir / kProgram;
}

void HdPackMaker::work(const GameData& data, const std::string& remembered) {
    fs::path upscaler = findUpscaler({}, remembered);
    if (upscaler.empty()) upscaler = download();
    if (cancel_) return outcome_.store(Outcome::Cancelled);
    if (upscaler.empty()) return outcome_.store(Outcome::NotAvailable);
    upscaler_ = upscaler;
    const int rc = makeHdPack(data, upscaler, kModel, userDir_ / "hd-interface", userDir_ / "hd-interface.work", &cancel_);
    if (cancel_) return outcome_.store(Outcome::Cancelled);
    outcome_.store(rc == 0 ? Outcome::Made : Outcome::UpscalerFailed);
}

} // namespace openday::app
