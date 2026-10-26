#include "app/mcp_server.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <string_view>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

namespace openday::app {

namespace {

#ifdef _WIN32
using Socket = SOCKET;
const Socket kNoSocket = INVALID_SOCKET;
void closeSocket(Socket s) { closesocket(s); }
using IoLength = int; // send / recv lengths
#else
using Socket = int;
const Socket kNoSocket = -1;
void closeSocket(Socket s) { close(s); }
using IoLength = std::size_t;
#endif

// Protocol revisions this server speaks; the client's choice is taken when
// it is one of them (the tools part is the same in all).
constexpr std::string_view kVersions[] = {"2025-11-25", "2025-06-18", "2025-03-26", "2024-11-05"};

constexpr std::size_t kMaxHeader = 64 * 1024;
constexpr std::size_t kMaxBody = 4 * 1024 * 1024;

void setTimeouts(Socket s, int ms) {
#ifdef _WIN32
    const DWORD t = static_cast<DWORD>(ms);
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&t), sizeof t);
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&t), sizeof t);
#else
    timeval t{};
    t.tv_sec = ms / 1000;
    t.tv_usec = (ms % 1000) * 1000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &t, sizeof t);
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &t, sizeof t);
#ifdef SO_NOSIGPIPE
    const int one = 1;
    setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
#endif
}

bool sendAll(Socket s, std::string_view data) {
#if defined(MSG_NOSIGNAL)
    const int flags = MSG_NOSIGNAL;
#else
    const int flags = 0;
#endif
    while (!data.empty()) {
        const auto chunk = static_cast<IoLength>(std::min<std::size_t>(data.size(), 1 << 20));
        const auto n = send(s, data.data(), chunk, flags);
        if (n <= 0) return false;
        data.remove_prefix(static_cast<std::size_t>(n));
    }
    return true;
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
    return std::string(s);
}

struct HttpRequest {
    std::string method, path;
    std::vector<std::pair<std::string, std::string>> headers; // lower-case names
    std::string body;

    std::string header(std::string_view name) const {
        for (const auto& [k, v] : headers)
            if (k == name) return v;
        return {};
    }
};

// Reads one request; false when the connection broke or the request is too
// large or malformed.
bool readRequest(Socket s, HttpRequest& r) {
    std::string data;
    std::size_t headerEnd = std::string::npos;
    char buf[8192];
    while ((headerEnd = data.find("\r\n\r\n")) == std::string::npos) {
        if (data.size() > kMaxHeader) return false;
        const auto n = recv(s, buf, static_cast<IoLength>(sizeof buf), 0);
        if (n <= 0) return false;
        data.append(buf, static_cast<std::size_t>(n));
    }
    std::size_t lineEnd = data.find("\r\n");
    const std::string first = data.substr(0, lineEnd);
    const std::size_t sp1 = first.find(' '), sp2 = first.find(' ', sp1 + 1);
    if (sp1 == std::string::npos || sp2 == std::string::npos) return false;
    r.method = first.substr(0, sp1);
    r.path = first.substr(sp1 + 1, sp2 - sp1 - 1);
    for (std::size_t pos = lineEnd + 2; pos < headerEnd;) {
        const std::size_t end = data.find("\r\n", pos);
        const std::string line = data.substr(pos, end - pos);
        pos = end + 2;
        const std::size_t colon = line.find(':');
        if (colon != std::string::npos) r.headers.emplace_back(lower(trim(line.substr(0, colon))), trim(line.substr(colon + 1)));
    }
    std::size_t length = 0;
    if (const std::string cl = r.header("content-length"); !cl.empty()) {
        try {
            length = static_cast<std::size_t>(std::stoull(cl));
        } catch (const std::exception&) {
            return false;
        }
    }
    if (length > kMaxBody) return false;
    r.body = data.substr(headerEnd + 4);
    while (r.body.size() < length) {
        const auto n = recv(s, buf, static_cast<IoLength>(sizeof buf), 0);
        if (n <= 0) return false;
        r.body.append(buf, static_cast<std::size_t>(n));
    }
    r.body.resize(length);
    return true;
}

void respond(Socket s, int status, std::string_view reason, std::string_view body, std::string_view extra = {}) {
    std::string head = "HTTP/1.1 " + std::to_string(status) + " " + std::string(reason) + "\r\n";
    if (!body.empty()) head += "Content-Type: application/json\r\n";
    head += "Content-Length: " + std::to_string(body.size()) + "\r\nConnection: close\r\n";
    head += extra;
    head += "\r\n";
    if (sendAll(s, head)) sendAll(s, body);
}

// Only pages of this machine may call (DNS rebinding protection, as the
// MCP transport specification asks for).
bool localOrigin(const std::string& origin) {
    if (origin.empty() || origin == "null") return true;
    for (const char* prefix : {"http://localhost", "http://127.0.0.1", "https://localhost", "https://127.0.0.1", "http://[::1]"}) {
        const std::string p = prefix;
        if (origin.compare(0, p.size(), p) == 0 && (origin.size() == p.size() || origin[p.size()] == ':')) return true;
    }
    return false;
}

std::string idJson(const formats::JsonValue& id) {
    if (id.type() == formats::JsonValue::Type::String) return formats::jsonQuote(id.string());
    if (id.type() == formats::JsonValue::Type::Number) {
        const double n = id.number();
        char buf[32];
        if (n == std::floor(n) && std::fabs(n) < 1e15) std::snprintf(buf, sizeof buf, "%.0f", n);
        else std::snprintf(buf, sizeof buf, "%.17g", n);
        return buf;
    }
    return "null";
}

std::string rpcResult(const std::string& id, const std::string& result) {
    return "{\"jsonrpc\":\"2.0\",\"id\":" + id + ",\"result\":" + result + "}";
}

std::string rpcError(const std::string& id, int code, std::string_view message) {
    return "{\"jsonrpc\":\"2.0\",\"id\":" + id + ",\"error\":{\"code\":" + std::to_string(code) +
           ",\"message\":" + formats::jsonQuote(message) + "}}";
}

} // namespace

struct McpServer::Shared {
    Socket listener = kNoSocket;
    std::atomic<bool> stop{false};
    std::vector<ToolInfo> tools;
    std::string instructions;
    std::mutex mutex;
    std::deque<std::shared_ptr<ToolCall>> calls;

    // The JSON-RPC answer to one message; empty for notifications.
    std::string answer(const std::string& body) {
        formats::JsonValue msg;
        try {
            msg = formats::JsonValue::parse(body);
        } catch (const std::exception&) {
            return rpcError("null", -32700, "parse error");
        }
        if (msg.type() != formats::JsonValue::Type::Object) return rpcError("null", -32600, "invalid request");
        const std::string& method = msg["method"].string();
        if (method.empty() || msg["id"].isNull()) return {}; // a notification or a response
        const std::string id = idJson(msg["id"]);
        const formats::JsonValue& params = msg["params"];

        if (method == "initialize") {
            std::string version(kVersions[1]);
            for (const std::string_view v : kVersions)
                if (params["protocolVersion"].string() == v) version = v;
            formats::JsonWriter w;
            w.beginObject().field("protocolVersion", version);
            w.key("capabilities").beginObject().key("tools").beginObject().field("listChanged", false).endObject().endObject();
            w.key("serverInfo").beginObject().field("name", "openday").field("version", OPENDAY_VERSION).endObject();
            w.field("instructions", instructions).endObject();
            return rpcResult(id, w.str());
        }
        if (method == "ping") return rpcResult(id, "{}");
        if (method == "tools/list") {
            formats::JsonWriter w;
            w.beginObject().key("tools").beginArray();
            for (const ToolInfo& t : tools)
                w.beginObject().field("name", t.name).field("description", t.description).key("inputSchema").raw(t.inputSchema).endObject();
            w.endArray().endObject();
            return rpcResult(id, w.str());
        }
        if (method == "tools/call") {
            const std::string& name = params["name"].string();
            if (std::none_of(tools.begin(), tools.end(), [&](const ToolInfo& t) { return t.name == name; }))
                return rpcError(id, -32602, "unknown tool: " + name);
            auto call = std::make_shared<ToolCall>();
            call->name = name;
            call->args = params["arguments"];
            std::future<ToolResult> future = call->result.get_future();
            {
                std::lock_guard lock(mutex);
                calls.push_back(call);
            }
            ToolResult result;
            for (;;) {
                if (future.wait_for(std::chrono::milliseconds(200)) == std::future_status::ready) {
                    try {
                        result = future.get();
                    } catch (const std::exception&) {
                        result = {"[{\"type\":\"text\",\"text\":\"the game is closing\"}]", true};
                    }
                    break;
                }
                if (stop) {
                    result = {"[{\"type\":\"text\",\"text\":\"the game is closing\"}]", true};
                    break;
                }
            }
            return rpcResult(id, "{\"content\":" + result.content + ",\"isError\":" + (result.error ? "true" : "false") + "}");
        }
        return rpcError(id, -32601, "method not found: " + method);
    }

    void serve(Socket client) {
        setTimeouts(client, 10000);
        HttpRequest r;
        if (!readRequest(client, r)) {
            respond(client, 400, "Bad Request", {});
        } else if (!localOrigin(r.header("origin"))) {
            respond(client, 403, "Forbidden", {});
        } else if (r.path != "/mcp" && r.path != "/") {
            respond(client, 404, "Not Found", {});
        } else if (r.method == "POST") {
            const std::string body = answer(r.body);
            if (body.empty()) respond(client, 202, "Accepted", {});
            else respond(client, 200, "OK", body);
        } else if (r.method == "DELETE") {
            respond(client, 200, "OK", {}); // the end of a session; there is no session state
        } else {
            respond(client, 405, "Method Not Allowed", {}, "Allow: POST\r\n"); // no server-sent event stream
        }
        closeSocket(client);
    }
};

McpServer::McpServer(int port, std::vector<ToolInfo> tools, std::string instructions) : shared_(std::make_shared<Shared>()) {
#ifdef _WIN32
    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) throw std::runtime_error("debug server: WSAStartup failed");
#endif
    shared_->tools = std::move(tools);
    shared_->instructions = std::move(instructions);
    const Socket s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == kNoSocket) throw std::runtime_error("debug server: cannot create a socket");
#ifndef _WIN32
    const int one = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
#endif
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<std::uint16_t>(port));
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(s, reinterpret_cast<const sockaddr*>(&addr), sizeof addr) != 0 || listen(s, 8) != 0) {
        closeSocket(s);
        throw std::runtime_error("debug server: cannot listen on 127.0.0.1:" + std::to_string(port) + " (port in use?)");
    }
    shared_->listener = s;
    acceptor_ = std::thread([shared = shared_] {
        while (!shared->stop) {
            fd_set set;
            FD_ZERO(&set);
            FD_SET(shared->listener, &set);
            timeval t{0, 200000};
            if (select(static_cast<int>(shared->listener) + 1, &set, nullptr, nullptr, &t) <= 0) continue;
            const Socket client = accept(shared->listener, nullptr, nullptr);
            if (client == kNoSocket) continue;
            // One thread per connection: a tool call may wait for the game
            // for a long time, and the client may send other requests meanwhile.
            std::thread([shared, client] { shared->serve(client); }).detach();
        }
    });
}

McpServer::~McpServer() {
    shared_->stop = true;
    if (acceptor_.joinable()) acceptor_.join();
    closeSocket(shared_->listener);
    std::lock_guard lock(shared_->mutex);
    for (const auto& call : shared_->calls) call->result.set_value({"[{\"type\":\"text\",\"text\":\"the game is closing\"}]", true});
    shared_->calls.clear();
}

std::vector<std::shared_ptr<ToolCall>> McpServer::takeCalls() {
    std::lock_guard lock(shared_->mutex);
    std::vector<std::shared_ptr<ToolCall>> out(shared_->calls.begin(), shared_->calls.end());
    shared_->calls.clear();
    return out;
}

} // namespace openday::app
