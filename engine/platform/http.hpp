// HTTPS downloads with the operating system's own client, for the updater
// and the GOG download:
// WinHTTP on Windows; elsewhere the system libcurl, loaded at run time (so
// that neither the build nor the package depends on it). Blocking: call it
// from a worker thread.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace openday::platform {

// The final response's head, after the redirects.
struct HttpHead {
    int status = 0;
    std::optional<std::uint64_t> contentLength;
    // Content-Range of a 206: the first byte and the whole size ("*" gives none).
    std::optional<std::uint64_t> rangeFirst, rangeTotal;
    std::string finalUrl; // where the redirects led
};

struct HttpRequest {
    std::string url;                  // https:// only; redirects are followed
    std::vector<std::string> headers; // "Name: value"
    std::string userAgent = "OpenDay";
    // Receives the body in pieces; return false to stop (the request then fails).
    std::function<bool(const std::uint8_t* data, std::size_t size)> onData;
    // Bytes received and the expected total (0 if unknown); return false to cancel.
    std::function<bool(std::uint64_t received, std::uint64_t total)> onProgress;
    // Called once the final response's head is in, before its body; return
    // false to stop (the request then fails with error "stopped").
    std::function<bool(const HttpHead& head)> onHead;
};

struct HttpResponse {
    bool ok = false; // the transfer completed (any status code)
    int status = 0;  // HTTP status of the final response
    std::string error;
};

HttpResponse httpGet(const HttpRequest& request);

// Convenience: the whole body as a string. `ok` also requires status 200.
HttpResponse httpGetText(const std::string& url, const std::vector<std::string>& headers, const std::string& userAgent,
                         std::string& body, std::size_t maxSize = 4u << 20);

} // namespace openday::platform
