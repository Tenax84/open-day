#include "platform/http.hpp"

#include <cstdio>
#include <cstdlib>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winhttp.h>
#else
#include <dlfcn.h>

#include <mutex>
#include <type_traits>
#endif

namespace openday::platform {

namespace {

// "bytes 100-199/1000" (or "bytes 100-199/*").
void parseContentRange(const std::string& value, HttpHead& head) {
    unsigned long long first = 0, last = 0;
    char total[32] = {};
    if (std::sscanf(value.c_str(), " bytes %llu-%llu/%31s", &first, &last, total) < 3) return;
    head.rangeFirst = first;
    if (total[0] != '*') head.rangeTotal = std::strtoull(total, nullptr, 10);
}

} // namespace

#ifdef _WIN32

namespace {

std::wstring wide(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::string narrow(const wchar_t* w) {
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return {};
    std::string s(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    s.resize(static_cast<std::size_t>(n - 1));
    return s;
}

std::string lastError(const char* what) {
    const DWORD code = GetLastError();
    std::string msg = std::string(what) + " failed (error " + std::to_string(code) + ")";
    if (code == ERROR_WINHTTP_TIMEOUT) msg += ": timed out";
    else if (code == ERROR_WINHTTP_NAME_NOT_RESOLVED) msg += ": host not found (no network?)";
    else if (code == ERROR_WINHTTP_CANNOT_CONNECT) msg += ": cannot connect";
    else if (code == ERROR_WINHTTP_SECURE_FAILURE) msg += ": secure connection failed";
    return msg;
}

struct Handle {
    HINTERNET h = nullptr;
    explicit Handle(HINTERNET handle) : h(handle) {}
    ~Handle() {
        if (h) WinHttpCloseHandle(h);
    }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    explicit operator bool() const { return h != nullptr; }
};

} // namespace

HttpResponse httpGet(const HttpRequest& request) {
    HttpResponse res;
    const std::wstring url = wide(request.url);
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwHostNameLength = static_cast<DWORD>(-1);
    parts.dwUrlPathLength = static_cast<DWORD>(-1);
    parts.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts) || parts.nScheme != INTERNET_SCHEME_HTTPS) {
        res.error = "not an https URL: " + request.url;
        return res;
    }
    const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
    std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength);
    if (parts.lpszExtraInfo) path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);

    // The system proxy settings (Windows 8.1 and later), else WinHTTP's own.
    Handle session(WinHttpOpen(wide(request.userAgent).c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                               WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session)
        session.h = WinHttpOpen(wide(request.userAgent).c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                                WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        res.error = lastError("WinHttpOpen");
        return res;
    }
    WinHttpSetTimeouts(session.h, 15000, 15000, 30000, 30000);
    Handle connection(WinHttpConnect(session.h, host.c_str(), parts.nPort, 0));
    if (!connection) {
        res.error = lastError("WinHttpConnect");
        return res;
    }
    Handle req(WinHttpOpenRequest(connection.h, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                  WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
    if (!req) {
        res.error = lastError("WinHttpOpenRequest");
        return res;
    }
    // Redirects (GitHub serves the assets from another host) stay on HTTPS.
    DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
    WinHttpSetOption(req.h, WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof(policy));
    std::wstring headers;
    for (const std::string& h : request.headers) headers += wide(h) + L"\r\n";
    if (!WinHttpSendRequest(req.h, headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(),
                            headers.empty() ? 0 : static_cast<DWORD>(-1), WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        res.error = lastError("WinHttpSendRequest");
        return res;
    }
    if (!WinHttpReceiveResponse(req.h, nullptr)) {
        res.error = lastError("WinHttpReceiveResponse");
        return res;
    }
    DWORD status = 0, size = sizeof(status);
    WinHttpQueryHeaders(req.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status,
                        &size, WINHTTP_NO_HEADER_INDEX);
    res.status = static_cast<int>(status);
    std::uint64_t total = 0;
    wchar_t length[32] = {};
    size = sizeof(length);
    const bool hasLength = WinHttpQueryHeaders(req.h, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, length,
                                               &size, WINHTTP_NO_HEADER_INDEX);
    if (hasLength) total = std::wcstoull(length, nullptr, 10);
    if (request.onHead) {
        HttpHead head;
        head.status = res.status;
        if (hasLength) head.contentLength = total;
        wchar_t range[128] = {};
        size = sizeof(range);
        if (WinHttpQueryHeaders(req.h, WINHTTP_QUERY_CUSTOM, L"Content-Range", range, &size, WINHTTP_NO_HEADER_INDEX))
            parseContentRange(narrow(range), head);
        DWORD urlSize = 0;
        WinHttpQueryOption(req.h, WINHTTP_OPTION_URL, nullptr, &urlSize);
        if (urlSize > 0) {
            std::wstring finalUrl(urlSize / sizeof(wchar_t), L'\0');
            if (WinHttpQueryOption(req.h, WINHTTP_OPTION_URL, finalUrl.data(), &urlSize))
                head.finalUrl = narrow(finalUrl.c_str());
        }
        if (!request.onHead(head)) {
            res.error = "stopped";
            return res;
        }
    }

    std::vector<std::uint8_t> buffer(64 * 1024);
    std::uint64_t received = 0;
    if (request.onProgress && !request.onProgress(0, total)) {
        res.error = "cancelled";
        return res;
    }
    for (;;) {
        DWORD got = 0;
        if (!WinHttpReadData(req.h, buffer.data(), static_cast<DWORD>(buffer.size()), &got)) {
            res.error = lastError("WinHttpReadData");
            return res;
        }
        if (got == 0) break;
        received += got;
        if (request.onData && !request.onData(buffer.data(), got)) {
            res.error = "cancelled";
            return res;
        }
        if (request.onProgress && !request.onProgress(received, total)) {
            res.error = "cancelled";
            return res;
        }
    }
    res.ok = true;
    return res;
}

#else

namespace {

// The few libcurl entry points used, with the option numbers of curl.h
// (part of its stable ABI).
using CurlHandle = void;
struct CurlSlist;
constexpr int kOptWriteData = 10001, kOptUrl = 10002, kOptUserAgent = 10018, kOptHttpHeader = 10023,
              kOptWriteFunction = 20011, kOptXferInfoData = 10057, kOptXferInfoFunction = 20219, kOptNoProgress = 43,
              kOptFollowLocation = 52, kOptMaxRedirs = 68, kOptConnectTimeout = 78, kOptLowSpeedLimit = 19,
              kOptLowSpeedTime = 20, kOptProtocols = 181, kOptRedirProtocols = 182, kOptErrorBuffer = 10010,
              kOptHeaderFunction = 20079, kOptHeaderData = 10029;
constexpr int kInfoResponseCode = 0x200000 + 2, kInfoEffectiveUrl = 0x100000 + 1;
constexpr long kProtoHttps = 1L << 1;
constexpr long kGlobalDefault = 3;
constexpr std::size_t kErrorSize = 256;

struct Curl {
    int (*globalInit)(long) = nullptr;
    CurlHandle* (*easyInit)() = nullptr;
    int (*easySetopt)(CurlHandle*, int, ...) = nullptr;
    int (*easyPerform)(CurlHandle*) = nullptr;
    int (*easyGetinfo)(CurlHandle*, int, ...) = nullptr;
    void (*easyCleanup)(CurlHandle*) = nullptr;
    const char* (*easyStrerror)(int) = nullptr;
    CurlSlist* (*slistAppend)(CurlSlist*, const char*) = nullptr;
    void (*slistFreeAll)(CurlSlist*) = nullptr;
    bool loaded = false;
};

const Curl& curl() {
    static Curl c;
    static std::once_flag once;
    std::call_once(once, [] {
#ifdef __APPLE__
        const char* names[] = {"/usr/lib/libcurl.4.dylib", "libcurl.4.dylib", "libcurl.dylib"};
#else
        const char* names[] = {"libcurl.so.4", "libcurl-gnutls.so.4", "libcurl-nss.so.4", "libcurl.so"};
#endif
        void* lib = nullptr;
        for (const char* n : names)
            if ((lib = dlopen(n, RTLD_NOW | RTLD_LOCAL)) != nullptr) break;
        if (!lib) return;
        auto sym = [lib](auto& fn, const char* name) {
            fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(dlsym(lib, name));
            return fn != nullptr;
        };
        c.loaded = sym(c.globalInit, "curl_global_init") && sym(c.easyInit, "curl_easy_init") &&
                   sym(c.easySetopt, "curl_easy_setopt") && sym(c.easyPerform, "curl_easy_perform") &&
                   sym(c.easyGetinfo, "curl_easy_getinfo") && sym(c.easyCleanup, "curl_easy_cleanup") &&
                   sym(c.easyStrerror, "curl_easy_strerror") && sym(c.slistAppend, "curl_slist_append") &&
                   sym(c.slistFreeAll, "curl_slist_free_all");
        if (c.loaded) c.loaded = c.globalInit(kGlobalDefault) == 0;
    });
    return c;
}

struct Transfer {
    const HttpRequest* request;
    CurlHandle* handle = nullptr;
    bool cancelled = false;
    bool stopped = false;
    bool headDone = false;
    HttpHead head; // of the latest response; a redirect starts it again
};

std::size_t onHeader(char* data, std::size_t size, std::size_t count, void* user) {
    auto* t = static_cast<Transfer*>(user);
    const std::size_t n = size * count;
    std::string line(data, n);
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
    const auto colon = line.find(':');
    if (line.rfind("HTTP/", 0) == 0) {
        t->head = HttpHead{};
    } else if (colon != std::string::npos) {
        std::string name = line.substr(0, colon);
        for (char& c : name)
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        const std::string value = line.substr(colon + 1);
        if (name == "content-length") t->head.contentLength = std::strtoull(value.c_str(), nullptr, 10);
        else if (name == "content-range") parseContentRange(value, t->head);
    }
    return n;
}

// Tells the caller about the final response's head, once.
bool reportHead(Transfer& t) {
    if (t.headDone || !t.request->onHead) return true;
    t.headDone = true;
    const Curl& c = curl();
    long status = 0;
    c.easyGetinfo(t.handle, kInfoResponseCode, &status);
    t.head.status = static_cast<int>(status);
    const char* url = nullptr;
    if (c.easyGetinfo(t.handle, kInfoEffectiveUrl, &url) == 0 && url) t.head.finalUrl = url;
    if (t.request->onHead(t.head)) return true;
    t.stopped = true;
    return false;
}

std::size_t onWrite(char* data, std::size_t size, std::size_t count, void* user) {
    auto* t = static_cast<Transfer*>(user);
    const std::size_t n = size * count;
    if (!reportHead(*t)) return 0;
    if (t->request->onData && !t->request->onData(reinterpret_cast<const std::uint8_t*>(data), n)) {
        t->cancelled = true;
        return 0;
    }
    return n;
}

int onProgress(void* user, long long total, long long now, long long, long long) {
    auto* t = static_cast<Transfer*>(user);
    if (t->request->onProgress &&
        !t->request->onProgress(static_cast<std::uint64_t>(now), static_cast<std::uint64_t>(total > 0 ? total : 0))) {
        t->cancelled = true;
        return 1;
    }
    return 0;
}

} // namespace

HttpResponse httpGet(const HttpRequest& request) {
    HttpResponse res;
    if (request.url.rfind("https://", 0) != 0) {
        res.error = "not an https URL: " + request.url;
        return res;
    }
    const Curl& c = curl();
    if (!c.loaded) {
        res.error = "libcurl is not available on this system";
        return res;
    }
    CurlHandle* h = c.easyInit();
    if (!h) {
        res.error = "curl_easy_init failed";
        return res;
    }
    CurlSlist* headers = nullptr;
    for (const std::string& line : request.headers) headers = c.slistAppend(headers, line.c_str());
    Transfer t{&request, h};
    char errorText[kErrorSize] = {};
    using WriteFn = std::size_t (*)(char*, std::size_t, std::size_t, void*);
    using ProgressFn = int (*)(void*, long long, long long, long long, long long);
    c.easySetopt(h, kOptUrl, request.url.c_str());
    c.easySetopt(h, kOptUserAgent, request.userAgent.c_str());
    if (headers) c.easySetopt(h, kOptHttpHeader, headers);
    c.easySetopt(h, kOptErrorBuffer, errorText);
    c.easySetopt(h, kOptFollowLocation, 1L);
    c.easySetopt(h, kOptMaxRedirs, 10L);
    c.easySetopt(h, kOptProtocols, kProtoHttps);
    c.easySetopt(h, kOptRedirProtocols, kProtoHttps);
    c.easySetopt(h, kOptConnectTimeout, 15L);
    c.easySetopt(h, kOptLowSpeedLimit, 1L); // abort when stalled for 30 s
    c.easySetopt(h, kOptLowSpeedTime, 30L);
    c.easySetopt(h, kOptWriteFunction, static_cast<WriteFn>(onWrite));
    c.easySetopt(h, kOptWriteData, &t);
    c.easySetopt(h, kOptXferInfoFunction, static_cast<ProgressFn>(onProgress));
    c.easySetopt(h, kOptXferInfoData, &t);
    c.easySetopt(h, kOptNoProgress, 0L);
    using HeaderFn = std::size_t (*)(char*, std::size_t, std::size_t, void*);
    c.easySetopt(h, kOptHeaderFunction, static_cast<HeaderFn>(onHeader));
    c.easySetopt(h, kOptHeaderData, &t);
    int code = c.easyPerform(h);
    if (code == 0 && !reportHead(t)) code = -1; // an empty body
    long status = 0;
    c.easyGetinfo(h, kInfoResponseCode, &status);
    res.status = static_cast<int>(status);
    if (code == 0) res.ok = true;
    else if (t.stopped) res.error = "stopped";
    else if (t.cancelled) res.error = "cancelled";
    else res.error = errorText[0] ? errorText : c.easyStrerror(code);
    c.easyCleanup(h);
    if (headers) c.slistFreeAll(headers);
    return res;
}

#endif

HttpResponse httpGetText(const std::string& url, const std::vector<std::string>& headers, const std::string& userAgent,
                         std::string& body, std::size_t maxSize) {
    HttpRequest req;
    req.url = url;
    req.headers = headers;
    req.userAgent = userAgent;
    req.onData = [&body, maxSize](const std::uint8_t* data, std::size_t size) {
        if (body.size() + size > maxSize) return false;
        body.append(reinterpret_cast<const char*>(data), size);
        return true;
    };
    HttpResponse res = httpGet(req);
    if (res.ok && res.status != 200) {
        res.ok = false;
        if (res.error.empty()) res.error = "HTTP status " + std::to_string(res.status);
    }
    return res;
}

} // namespace openday::platform
