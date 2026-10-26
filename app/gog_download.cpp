#include "app/gog_download.hpp"

#include <atomic>
#include <cstdio>
#include <mutex>
#include <optional>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include "platform/http.hpp"
#include "update/gog.hpp"

namespace openday::app::gog {

namespace fs = std::filesystem;
namespace api = update::gog;

namespace {

constexpr const char* kUserAgent = "OpenDay";
// The most bytes one installer file may have.
constexpr std::uint64_t kMostFileBytes = std::uint64_t{8} << 30;

fs::path pathOf(const std::string& utf8Name) {
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(utf8Name.data()), utf8Name.size()));
}

std::string utf8(const fs::path& p) {
    const std::u8string u = p.u8string();
    return std::string(u.begin(), u.end());
}

std::uint64_t fileSize(const fs::path& p) {
    std::error_code ec;
    if (!fs::is_regular_file(p, ec)) return 0;
    const auto size = fs::file_size(p, ec);
    return ec ? 0 : size;
}

std::FILE* openFile(const fs::path& p, bool append) {
#ifdef _WIN32
    return _wfopen(p.c_str(), append ? L"ab" : L"wb");
#else
    return std::fopen(p.c_str(), append ? "ab" : "wb");
#endif
}

} // namespace

const char* failureText(Failure failure) {
    switch (failure) {
    case Failure::None: return "";
    case Failure::SignIn: return "GOG did not accept the sign-in. Sign in again.";
    case Failure::NotOwned: return "This GOG account does not have Fallen Haven: Liberation Day.";
    case Failure::NoInstaller: return "GOG lists no Windows installer for Fallen Haven: Liberation Day.";
    case Failure::Network: return "GOG could not be reached, or the connection broke. Check the connection and try again.";
    case Failure::Unexpected: return "GOG's answer could not be read. Try again later.";
    case Failure::Disk: return "The installer could not be saved.";
    case Failure::NoSpace: return "The disk has no room for the installer.";
    case Failure::Cancelled: return "The download was cancelled.";
    }
    return "";
}

struct Download::State {
    mutable std::mutex mutex;
    Status status;
    std::atomic<bool> cancel{false};
    std::thread worker;

    // Records why the download stopped; a cancel is told as one, whatever failed.
    void fail(Failure failure, std::string detail) {
        std::lock_guard lock(mutex);
        status.phase = Phase::Failed;
        status.failure = cancel ? Failure::Cancelled : failure;
        status.detail = std::move(detail);
    }

    void setPhase(Phase phase) {
        std::lock_guard lock(mutex);
        status.phase = phase;
    }

    // One JSON answer; nullopt (after fail()) when no whole answer came.
    // `label` names it in the log without the address, which may carry the code.
    std::optional<int> fetch(const char* label, const std::string& url, const std::vector<std::string>& headers,
                             std::string& body) {
        platform::HttpRequest request;
        request.url = url;
        request.headers = headers;
        request.userAgent = kUserAgent;
        request.onData = [&body](const std::uint8_t* data, std::size_t size) {
            if (body.size() + size > (1u << 20)) return false;
            body.append(reinterpret_cast<const char*>(data), size);
            return true;
        };
        request.onProgress = [this](std::uint64_t, std::uint64_t) { return !cancel; };
        const platform::HttpResponse response = platform::httpGet(request);
        if (!response.ok) {
            fail(Failure::Network, std::string(label) + ": " + response.error);
            return std::nullopt;
        }
        return response.status;
    }

    // Downloads one file into `folder`; returns its path, or empty after fail().
    fs::path downloadFile(const api::InstallerFile& file, const std::string& bearer, const fs::path& folder,
                          std::uint64_t before);

    void run(const std::string& code, const fs::path& folder);
};

fs::path Download::State::downloadFile(const api::InstallerFile& file, const std::string& bearer,
                                       const fs::path& folder, std::uint64_t before) {
    std::string body;
    const auto linkStatus = fetch("the download link", file.downlink, {"Authorization: " + bearer}, body);
    if (!linkStatus) return {};
    if (*linkStatus == 401 || *linkStatus == 403 || *linkStatus == 404) {
        fail(Failure::NotOwned, "the download link answered " + std::to_string(*linkStatus));
        return {};
    }
    const std::string address = api::secureLink(body);
    std::string name = api::fileNameOf(address);
    if (*linkStatus != 200 || address.empty() || name.empty()) {
        fail(Failure::Unexpected, "the download link answered " + std::to_string(*linkStatus));
        return {};
    }
    {
        std::lock_guard lock(mutex);
        status.file = name;
    }

    const fs::path whole = folder / pathOf(name);
    if (file.size > 0 && fileSize(whole) == file.size) return whole; // downloaded before
    const fs::path part = folder / pathOf(name + ".part");
    std::uint64_t done = fileSize(part);
    std::error_code ec;
    if (done > file.size) {
        fs::remove(part, ec);
        done = 0;
    }
    if (done == file.size && done > 0) {
        // A whole part an earlier try could not rename.
        fs::rename(part, whole, ec);
        if (!ec) return whole;
        done = 0;
    }

    setPhase(Phase::Downloading);
    struct Sink {
        std::FILE* out = nullptr;
        std::uint64_t done = 0, size = 0;
        bool disk = false, noSpace = false, badHost = false;
        std::string finalUrl;
    } sink;
    sink.done = done;
    platform::HttpRequest request;
    request.url = address;
    request.userAgent = kUserAgent;
    if (done > 0) request.headers.push_back("Range: bytes=" + std::to_string(done) + "-");
    request.onHead = [&](const platform::HttpHead& head) {
        if (!api::downloadHost(head.finalUrl)) {
            sink.badHost = true;
            return false;
        }
        std::uint64_t size = 0;
        if (head.status == 206 && head.rangeFirst && *head.rangeFirst == sink.done && head.rangeTotal) {
            size = *head.rangeTotal;
        } else if (head.status == 200 && head.contentLength) {
            sink.done = 0;
            size = *head.contentLength;
        } else {
            return false;
        }
        if (size < sink.done || size > kMostFileBytes) return false;
        std::error_code spaceError;
        const fs::space_info room = fs::space(folder, spaceError);
        if (!spaceError && room.available < size - sink.done) {
            sink.noSpace = true;
            return false;
        }
        sink.out = openFile(part, sink.done > 0);
        if (!sink.out) {
            sink.disk = true;
            return false;
        }
        sink.size = size;
        sink.finalUrl = head.finalUrl;
        std::lock_guard lock(mutex);
        status.received = before + sink.done;
        return true;
    };
    request.onData = [&](const std::uint8_t* data, std::size_t size) {
        if (size > sink.size - sink.done || std::fwrite(data, 1, size, sink.out) != size) {
            sink.disk = true;
            return false;
        }
        sink.done += size;
        std::lock_guard lock(mutex);
        status.received = before + sink.done;
        return true;
    };
    request.onProgress = [this](std::uint64_t, std::uint64_t) { return !cancel; };
    const platform::HttpResponse response = platform::httpGet(request);
    if (sink.out) {
        const bool flushed = std::fflush(sink.out) == 0;
        if (std::fclose(sink.out) != 0 || !flushed) sink.disk = true;
    }
    if (sink.noSpace) {
        fail(Failure::NoSpace, "no room for " + std::to_string(sink.size) + " bytes");
        return {};
    }
    if (sink.disk) {
        fail(Failure::Disk, "writing " + utf8(part));
        return {};
    }
    if (sink.badHost) {
        fail(Failure::Unexpected, "the installer's address led off GOG's hosts");
        return {};
    }
    if (response.status == 416) {
        // The part is not a beginning of the server's file: the next try
        // starts it again.
        fs::remove(part, ec);
        fail(Failure::Network, "the installer answered 416 to the part's range");
        return {};
    }
    if (!response.ok) {
        const bool answered = response.error == "stopped";
        fail(answered ? Failure::Unexpected : Failure::Network,
             answered ? "the installer answered " + std::to_string(response.status) : "the installer: " + response.error);
        return {};
    }
    if (sink.size == 0 || sink.done != sink.size) {
        fail(Failure::Network, "the installer ended early");
        return {};
    }
    // A redirect may name the file more exactly than the first address.
    if (const std::string finalName = api::fileNameOf(sink.finalUrl); !finalName.empty()) name = finalName;
    const fs::path target = folder / pathOf(name);
    fs::remove(target, ec);
    fs::rename(part, target, ec);
    if (ec) {
        fail(Failure::Disk, "renaming " + utf8(part) + ": " + ec.message());
        return {};
    }
    return target;
}

void Download::State::run(const std::string& code, const fs::path& folder) {
    setPhase(Phase::SigningIn);
    std::string body;
    const auto tokenStatus = fetch("the token request", api::tokenAddress(code), {}, body);
    if (!tokenStatus) return;
    const std::string token = api::accessToken(body);
    if (*tokenStatus != 200 || token.empty()) {
        fail(Failure::SignIn, "the token request answered " + std::to_string(*tokenStatus));
        return;
    }
    const std::string bearer = "Bearer " + token;

    setPhase(Phase::Finding);
    body.clear();
    const auto productStatus = fetch("the product", api::productAddress(), {}, body);
    if (!productStatus) return;
    if (*productStatus != 200) {
        fail(Failure::Unexpected, "the product answered " + std::to_string(*productStatus));
        return;
    }
    const std::vector<api::InstallerFile> files = api::windowsInstaller(body);
    if (files.empty()) {
        fail(Failure::NoInstaller, "no Windows installer in " + api::productAddress());
        return;
    }
    std::uint64_t total = 0;
    for (const api::InstallerFile& f : files) total += f.size;
    {
        std::lock_guard lock(mutex);
        status.size = total;
    }

    std::error_code ec;
    fs::create_directories(folder, ec);
    fs::path installer;
    std::uint64_t before = 0;
    for (const api::InstallerFile& f : files) {
        const fs::path got = downloadFile(f, bearer, folder, before);
        if (got.empty()) return;
        if (installer.empty()) installer = got;
        before += f.size;
        std::lock_guard lock(mutex);
        status.received = before;
    }
    std::lock_guard lock(mutex);
    status.installer = installer;
    status.phase = Phase::Done;
}

Download::Download() : state_(std::make_shared<State>()) {}

Download::~Download() {
    state_->cancel = true;
    if (state_->worker.joinable()) state_->worker.join();
}

void Download::start(std::string code, fs::path folder) {
    state_->cancel = true;
    if (state_->worker.joinable()) state_->worker.join();
    state_->cancel = false;
    {
        std::lock_guard lock(state_->mutex);
        state_->status = Status{};
        state_->status.phase = Phase::SigningIn;
    }
    State* state = state_.get();
    state_->worker = std::thread([state, code = std::move(code), folder = std::move(folder)] { state->run(code, folder); });
}

void Download::cancel() { state_->cancel = true; }

Status Download::status() const {
    std::lock_guard lock(state_->mutex);
    return state_->status;
}

} // namespace openday::app::gog
