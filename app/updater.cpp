#include "app/updater.hpp"

#include <SDL3/SDL.h>

#include <atomic>
#include <fstream>
#include <iterator>
#include <mutex>
#include <system_error>
#include <thread>
#include <vector>

#include "formats/sha256.hpp"
#include "formats/tar.hpp"
#include "platform/http.hpp"
#include "update/release.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#else
#include <unistd.h>
#endif

namespace openday::app {
namespace {

std::string utf8(const fs::path& p) {
    const std::u8string u = p.u8string();
    return std::string(u.begin(), u.end());
}

fs::path utf8Path(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }

// The folder of the running program (macOS: <bundle>/Contents/Resources/).
fs::path basePath() {
    const char* base = SDL_GetBasePath(); // owned by SDL
    return base ? utf8Path(base) : fs::path();
}

#ifndef _WIN32
bool writable(const fs::path& dir) { return access(dir.c_str(), W_OK) == 0; }
#endif

// Where and how this copy of the program is installed, and whether it can
// update itself (`why` says why not).
struct Installation {
    bool supported = false;
    std::string why;
    fs::path root; // Windows: install folder; macOS: the .app bundle; Linux: the prefix holding bin/
};

Installation detectInstallation() {
    Installation in;
    std::error_code ec;
    const fs::path base = basePath();
    if (base.empty()) {
        in.why = "The program folder is unknown.";
        return in;
    }
#if defined(_WIN32)
    in.root = base.parent_path(); // drop the trailing separator
    if (!fs::exists(in.root / "Uninstall.exe", ec)) {
        in.why = "This copy was not installed by the OpenDay installer.";
        return in;
    }
#elif defined(__APPLE__)
    in.root = base.parent_path().parent_path().parent_path(); // .../OpenDay.app/Contents/Resources/
    const std::string root = utf8(in.root);
    if (in.root.extension() != ".app") {
        in.why = "This copy is not an application bundle.";
        return in;
    }
    if (root.find("/AppTranslocation/") != std::string::npos || root.rfind("/Volumes/", 0) == 0) {
        in.why = "Move OpenDay to the Applications folder first.";
        return in;
    }
    if (!writable(in.root.parent_path())) {
        in.why = "The folder of OpenDay is not writable.";
        return in;
    }
#else
    const fs::path bin = base.parent_path();
    in.root = bin.parent_path();
    if (bin.filename() != "bin" || !fs::exists(bin / "openday", ec) ||
        !(fs::exists(in.root / "share/doc/OpenDay/LICENSE", ec) || fs::exists(in.root / "share/doc/openday/LICENSE", ec))) {
        in.why = "This copy was not unpacked from an OpenDay release archive.";
        return in;
    }
    if (!writable(bin) || !writable(in.root)) {
        in.why = "The folder of OpenDay is not writable.";
        return in;
    }
#endif
    in.supported = true;
    return in;
}

#ifdef __APPLE__
// Runs a program and waits for it; returns its exit code (-1 if it did not start).
int runProgram(const std::vector<std::string>& args) {
    std::vector<const char*> argv;
    for (const std::string& a : args) argv.push_back(a.c_str());
    argv.push_back(nullptr);
    SDL_Process* p = SDL_CreateProcess(argv.data(), false);
    if (!p) return -1;
    int code = -1;
    SDL_WaitProcess(p, true, &code);
    SDL_DestroyProcess(p);
    return code;
}
#endif

#ifndef _WIN32
// Starts a program and leaves it running.
bool startProgram(const std::vector<std::string>& args) {
    std::vector<const char*> argv;
    for (const std::string& a : args) argv.push_back(a.c_str());
    argv.push_back(nullptr);
    SDL_Process* p = SDL_CreateProcess(argv.data(), false);
    if (!p) return false;
    SDL_DestroyProcess(p); // does not stop the process
    return true;
}
#endif

#ifdef __APPLE__
// Copies OpenDay.app out of the disk image next to the installed bundle and
// swaps the two. Renaming the bundle of the running program is fine on macOS.
std::string installMac(const fs::path& dmg, const fs::path& bundle, const fs::path& workDir) {
    std::error_code ec;
    const fs::path mount = workDir / "mnt";
    fs::create_directories(mount, ec);
    if (runProgram({"/usr/bin/hdiutil", "attach", "-nobrowse", "-readonly", "-noautoopen", "-mountpoint", utf8(mount),
                    utf8(dmg)}) != 0)
        return "The disk image could not be opened.";
    const fs::path parent = bundle.parent_path();
    const fs::path fresh = parent / ".OpenDay-update.app", old = parent / ".OpenDay-old.app";
    fs::remove_all(fresh, ec);
    fs::remove_all(old, ec);
    const int copied = runProgram({"/usr/bin/ditto", utf8(mount / "OpenDay.app"), utf8(fresh)});
    if (runProgram({"/usr/bin/hdiutil", "detach", utf8(mount), "-quiet"}) != 0)
        runProgram({"/usr/bin/hdiutil", "detach", utf8(mount), "-quiet", "-force"});
    if (copied != 0 || !fs::exists(fresh / "Contents/MacOS/OpenDay", ec)) {
        fs::remove_all(fresh, ec);
        return "The new version could not be copied.";
    }
    fs::rename(bundle, old, ec);
    if (ec) {
        fs::remove_all(fresh, ec);
        return "The installed version could not be replaced: " + ec.message();
    }
    fs::rename(fresh, bundle, ec);
    if (ec) {
        std::error_code ignored;
        fs::rename(old, bundle, ignored); // put the old one back
        fs::remove_all(fresh, ignored);
        return "The new version could not be put in place: " + ec.message();
    }
    fs::remove_all(old, ec);
    return {};
}
#endif

#if !defined(_WIN32) && !defined(__APPLE__)
// Unpacks the release archive over the installation. Each file is written
// next to its target and renamed over it, so that the running program
// (whose file stays open) is replaced safely.
std::string installLinux(const fs::path& archive, const fs::path& prefix) {
    std::vector<formats::TarEntry> entries;
    try {
        std::ifstream f(archive, std::ios::binary);
        const std::vector<std::uint8_t> gz((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        entries = formats::readTar(formats::gunzip(gz));
    } catch (const std::exception& e) {
        return std::string("The archive could not be read: ") + e.what();
    }
    std::error_code ec;
    int files = 0;
    for (const formats::TarEntry& e : entries) {
        // Drop the top folder (OpenDay-<version>-linux-<arch>/); refuse
        // anything that would leave the installation.
        const std::size_t slash = e.path.find('/');
        if (slash == std::string::npos) continue;
        const fs::path rel = fs::path(e.path.substr(slash + 1)).lexically_normal();
        if (rel.empty() || rel.is_absolute() || *rel.begin() == "..") continue;
        const fs::path target = prefix / rel;
        if (e.kind == formats::TarEntry::Kind::Directory) {
            fs::create_directories(target, ec);
            continue;
        }
        if (e.kind != formats::TarEntry::Kind::File) continue;
        fs::create_directories(target.parent_path(), ec);
        const fs::path temp = target.string() + ".openday-update";
        {
            std::ofstream out(temp, std::ios::binary | std::ios::trunc);
            out.write(reinterpret_cast<const char*>(e.data.data()), static_cast<std::streamsize>(e.data.size()));
            if (!out) return "Writing " + utf8(target) + " failed.";
        }
        fs::permissions(temp, static_cast<fs::perms>(e.mode ? e.mode & 0777 : 0644), ec);
        fs::rename(temp, target, ec);
        if (ec) {
            fs::remove(temp, ec);
            return "Replacing " + utf8(target) + " failed: " + ec.message();
        }
        ++files;
    }
    if (files == 0 || !fs::exists(prefix / "bin/openday", ec)) return "The archive does not contain OpenDay.";
    return {};
}
#endif

} // namespace

struct Updater::Shared {
    mutable std::mutex m;
    Status status;
    std::atomic<bool> cancel{false};
    fs::path workDir;
    Installation installation;
    update::Release release;
    std::string assetName; // the package for this platform; empty if none
    fs::path package;      // the downloaded and verified package
    bool busy = false;     // a worker thread is running

    void set(State s, std::string error = {}) {
        std::lock_guard lock(m);
        status.state = s;
        status.error = std::move(error);
        busy = false;
    }
};

namespace {

std::vector<std::string> apiHeaders() {
    return {"Accept: application/vnd.github+json", "X-GitHub-Api-Version: 2022-11-28"};
}

std::string userAgent(const std::string& version) { return "OpenDay/" + version; }

} // namespace

Updater::Updater(fs::path workDir, std::string currentVersion) : shared_(std::make_shared<Shared>()) {
    shared_->workDir = std::move(workDir);
    shared_->status.current = std::move(currentVersion);
    shared_->installation = detectInstallation();
    std::error_code ec;
    fs::remove_all(shared_->workDir, ec); // packages of an earlier update
}

// The worker threads own a reference to the shared state, so the program
// does not wait for a slow network when it quits.
Updater::~Updater() { shared_->cancel = true; }

Updater::Status Updater::status() const {
    std::lock_guard lock(shared_->m);
    return shared_->status;
}

void Updater::check() {
    {
        std::lock_guard lock(shared_->m);
        const State s = shared_->status.state;
        if (shared_->busy || !(s == State::Idle || s == State::UpToDate || (s == State::Failed && shared_->status.latest.empty())))
            return;
        shared_->busy = true;
        shared_->status.state = State::Checking;
    }
    std::thread([sh = shared_] {
        const std::string current = sh->status.current; // not changed after construction
        std::string body;
        const platform::HttpResponse res =
            platform::httpGetText(update::latestReleaseUrl(), apiHeaders(), userAgent(current), body);
        if (!res.ok) {
            // 404: no published release yet (or the repository is private).
            sh->set(State::Failed, res.status == 404 ? "No published release was found." : res.error);
            return;
        }
        std::string error;
        std::optional<update::Release> rel = update::parseRelease(body, error);
        if (!rel) return sh->set(State::Failed, error);
        if (rel->draft || rel->prerelease || update::compareVersions(rel->version, current) <= 0) {
            std::lock_guard lock(sh->m);
            sh->status.state = State::UpToDate;
            sh->busy = false;
            return;
        }
        const std::string name = update::packageName(rel->version, update::platformOs(), update::platformArch());
        std::lock_guard lock(sh->m);
        sh->status.latest = rel->version;
        sh->status.pageUrl = rel->pageUrl;
        sh->assetName = rel->asset(name) ? name : std::string();
        sh->status.installable = sh->installation.supported && !sh->assetName.empty();
        sh->status.error = !sh->installation.supported ? sh->installation.why
                           : sh->assetName.empty()     ? "The release has no package for this platform."
                                                       : std::string();
        sh->release = std::move(*rel);
        sh->status.state = State::Available;
        sh->busy = false;
    }).detach();
}

void Updater::download() {
    {
        std::lock_guard lock(shared_->m);
        const State s = shared_->status.state;
        if (shared_->busy || !shared_->status.installable || !(s == State::Available || s == State::Failed)) return;
        shared_->busy = true;
        shared_->cancel = false;
        shared_->status.state = State::Downloading;
        shared_->status.received = shared_->status.total = 0;
        shared_->status.error.clear();
    }
    std::thread([sh = shared_] {
        update::Asset asset;
        std::string current;
        {
            std::lock_guard lock(sh->m);
            asset = *sh->release.asset(sh->assetName);
            current = sh->status.current;
        }
        // The expected checksum: the asset's digest from GitHub, else the
        // release's SHA256SUMS file.
        std::string expected = asset.sha256;
        if (expected.empty()) {
            if (const update::Asset* sums = sh->release.asset("SHA256SUMS")) {
                std::string text;
                if (platform::httpGetText(sums->url, {}, userAgent(current), text, 1u << 20).ok)
                    expected = update::sha256FromSums(text, asset.name);
            }
        }
        if (expected.empty()) return sh->set(State::Failed, "The release publishes no checksum for " + asset.name + ".");

        std::error_code ec;
        fs::create_directories(sh->workDir, ec);
        const fs::path file = sh->workDir / asset.name;
        const fs::path part = sh->workDir / (asset.name + ".part");
        formats::Sha256 hash;
        std::uint64_t written = 0;
        platform::HttpResponse res;
        {
            std::ofstream out(part, std::ios::binary | std::ios::trunc);
            if (!out) return sh->set(State::Failed, "Cannot write " + utf8(part) + ".");
            platform::HttpRequest req;
            req.url = asset.url;
            req.userAgent = userAgent(current);
            req.onData = [&](const std::uint8_t* data, std::size_t size) {
                hash.update({data, size});
                written += size;
                out.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
                return static_cast<bool>(out) && !sh->cancel;
            };
            req.onProgress = [&](std::uint64_t received, std::uint64_t total) {
                std::lock_guard lock(sh->m);
                sh->status.received = received;
                sh->status.total = total ? total : asset.size;
                return !sh->cancel.load();
            };
            res = platform::httpGet(req);
        }
        if (sh->cancel) {
            fs::remove(part, ec);
            return sh->set(State::Available);
        }
        if (!res.ok || res.status != 200) {
            fs::remove(part, ec);
            return sh->set(State::Failed, "Download failed: " + (res.error.empty() ? "HTTP status " + std::to_string(res.status) : res.error));
        }
        if ((asset.size && written != asset.size) || hash.finishHex() != expected) {
            fs::remove(part, ec);
            return sh->set(State::Failed, "The downloaded package is damaged (checksum mismatch).");
        }
        fs::rename(part, file, ec);
        if (ec) return sh->set(State::Failed, "Cannot store the package: " + ec.message());

        std::string error;
#if defined(__APPLE__)
        error = installMac(file, sh->installation.root, sh->workDir);
        fs::remove(file, ec);
#elif !defined(_WIN32)
        error = installLinux(file, sh->installation.root);
        fs::remove(file, ec);
#endif
        if (!error.empty()) return sh->set(State::Failed, error);
        {
            std::lock_guard lock(sh->m);
            sh->package = file;
        }
        sh->set(State::Ready);
    }).detach();
}

void Updater::cancel() { shared_->cancel = true; }

bool Updater::finish(std::string& error) {
    fs::path package, root;
    {
        std::lock_guard lock(shared_->m);
        if (shared_->status.state != State::Ready) {
            error = "The update is not ready.";
            return false;
        }
        package = shared_->package;
        root = shared_->installation.root;
    }
#if defined(_WIN32)
    // ShellExecuteEx, not CreateProcess: the installer asks for elevation.
    // It removes the old version first; /D keeps the installation folder.
    const std::wstring file = package.wstring();
    const std::wstring params = L"/D=" + root.wstring();
    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOASYNC;
    info.lpVerb = L"open";
    info.lpFile = file.c_str();
    info.lpParameters = params.c_str();
    info.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&info)) {
        error = GetLastError() == ERROR_CANCELLED ? "The installer was not allowed to run."
                                                  : "The installer could not be started.";
        return false;
    }
    return true;
#elif defined(__APPLE__)
    if (!startProgram({"/usr/bin/open", "-n", utf8(root), "--args", "--no-intro"})) {
        error = "OpenDay could not be restarted; start it again.";
        return false;
    }
    return true;
#else
    if (!startProgram({utf8(root / "bin" / "openday"), "--no-intro"})) {
        error = "OpenDay could not be restarted; start it again.";
        return false;
    }
    return true;
#endif
}

void Updater::openReleasePage() const {
    std::string url;
    {
        std::lock_guard lock(shared_->m);
        url = shared_->status.pageUrl;
    }
    if (url.empty()) url = std::string("https://github.com/") + update::kRepository + "/releases/latest";
    SDL_OpenURL(url.c_str());
}

} // namespace openday::app
