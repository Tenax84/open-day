// The tools of the debug server (docs/debug-mcp.md), carried out by the game
// loop: screenshots, input, waiting, the game speed, starting a skirmish,
// and a battle's state and orders (app/battle_control.hpp).
#pragma once

#include <chrono>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "app/app.hpp"
#include "app/mcp_server.hpp"

namespace openday::app {

class DebugTools {
public:
    explicit DebugTools(App& app) : app_(app) {}
    ~DebugTools(); // answers the calls still waiting

    static std::vector<ToolInfo> toolList();
    static std::string instructions();

    // Runs the queued calls; once per frame, after drawing. `picture` is the
    // frame just drawn and `palette` its colors; `elapsedMs` the game time
    // the frame advanced.
    void frame(McpServer& server, const render::IndexedSurface& picture, const formats::Palette& palette, int elapsedMs);

    // Factor applied to the game time (set_speed).
    double speed() const { return speed_; }

private:
    // A call that finishes in a later frame: after `waitMs` of game time,
    // once `until` holds (or the real-time deadline passed).
    struct Pending {
        std::shared_ptr<ToolCall> call;
        int waitMs = 0;
        std::function<bool()> until;
        std::chrono::steady_clock::time_point deadline;
        std::function<ToolResult(bool timedOut)> finish;
    };

    std::optional<ToolResult> start(const std::shared_ptr<ToolCall>& call);
    void defer(const std::shared_ptr<ToolCall>& call, int waitMs, int timeoutMs, std::function<bool()> until,
               std::function<ToolResult(bool timedOut)> finish);
    // The text result, with a screenshot of the next frame when asked for.
    std::optional<ToolResult> reply(const std::shared_ptr<ToolCall>& call, std::function<std::string()> text, int waitMs = 0);

    void send(const ui::InputEvent& e);
    std::string screenshotContent() const;
    std::string screenInfo() const;
    BattleControl* battle() const;
    bool battleSettled() const;

    std::optional<ToolResult> battleTool(const std::shared_ptr<ToolCall>& call, BattleControl& b);
    std::optional<ToolResult> battleOrder(const std::shared_ptr<ToolCall>& call, BattleControl& b);

    App& app_;
    double speed_ = 1;
    struct Queued {
        std::shared_ptr<ToolCall> call;
        std::chrono::steady_clock::time_point since;
    };
    std::deque<Queued> queue_;
    std::unique_ptr<Pending> pending_;
    const render::IndexedSurface* picture_ = nullptr;
    const formats::Palette* palette_ = nullptr;
};

} // namespace openday::app
