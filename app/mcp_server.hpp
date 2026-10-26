// The debug server (docs/debug-mcp.md): a Model Context Protocol server
// over HTTP on the loopback interface, so that a tool such as an AI agent
// can watch and drive the running game. Its threads answer the protocol;
// tool calls are queued and carried out by the game loop (DebugTools), as
// the game's state belongs to the main thread.
#pragma once

#include <future>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "formats/json.hpp"

namespace openday::app {

struct ToolResult {
    std::string content = "[]"; // JSON array of MCP content items
    bool error = false;
};

struct ToolCall {
    std::string name;
    formats::JsonValue args;
    std::promise<ToolResult> result;
};

struct ToolInfo {
    std::string name, description;
    std::string inputSchema; // JSON Schema of the arguments, as JSON text
};

class McpServer {
public:
    // Listens on 127.0.0.1:`port`. Throws std::runtime_error when it cannot.
    McpServer(int port, std::vector<ToolInfo> tools, std::string instructions);
    ~McpServer();
    McpServer(const McpServer&) = delete;
    McpServer& operator=(const McpServer&) = delete;

    // Calls received since the last time; each must get its result.
    std::vector<std::shared_ptr<ToolCall>> takeCalls();

    struct Shared;

private:
    std::shared_ptr<Shared> shared_;
    std::thread acceptor_;
};

} // namespace openday::app
