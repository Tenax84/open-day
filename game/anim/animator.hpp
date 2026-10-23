// The tick-based animation timeline of the tactical map
// (docs/mechanics/unit-animation.md, "Timing").
#pragma once

#include <array>
#include <functional>
#include <vector>

#include "anim/unit_animation.hpp"
#include "map/map_renderer.hpp"
#include "render/animation.hpp"

namespace openday::game {

inline constexpr int kTicksPerSecond = 15;

// One scheduled animation. It is active from tick `start` to `start +
// duration`: onStart runs at the start tick, onEnd after the last tick.
struct AnimTrack {
    int start = 0;
    int duration = 0;
    // Clips by facing for moves (path[i].facing picks one), otherwise only
    // clips[0]. A missing clip draws nothing.
    std::array<const render::Animation*, 8> clips{};
    bool loop = false;            // moves loop their clip; others hold the last frame
    std::vector<SlideStep> path;  // per tick frame position, depth tile and facing
    Point pos;                    // frame position when path is empty
    Point tile{-1, -1};           // depth tile when path is empty; outside the map = on top
    const render::Animation* sound = nullptr; // played at the start
    std::function<void()> onStart;
    std::function<void()> onEnd;
};

class Animator {
public:
    int now() const { return now_; }
    bool idle() const { return tracks_.empty() && pendingEnds_.empty(); }

    // Schedules a track; a start in the past is moved to the next tick.
    // Returns the end tick (start + duration).
    int add(AnimTrack track);

    // Advances the clock by one tick.
    void tick();

    // Frames of the current tick.
    const std::vector<Overlay>& overlays() const { return visible_; }

    std::function<void(const render::Animation&)> playSound;

private:
    struct Running {
        AnimTrack track;
        std::array<int, 8> frameCounter{};
    };
    std::vector<Running> tracks_;
    std::vector<std::function<void()>> pendingEnds_;
    std::vector<Overlay> visible_;
    int now_ = 0;
};

} // namespace openday::game
