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

// How an animation repairs the map cells in front of it (unit-animation.md 8).
enum class Occlusion {
    None,      // drawn on top of everything
    AtRest,    // a unit clip on `tile`
    Translate, // a move: per tick from the tile being entered and the facing
};

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
    Point tile{-1, -1};           // the clip's tile when path is empty (for AtRest)
    int group = 0;                // draw-order group: 0 ground (unit clips, moves), 1 floating
    Occlusion occlusion = Occlusion::None;
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

    // Frames of the current tick, sorted by group, then frame top, then
    // frame left (unit-animation.md 8).
    const std::vector<AnimDraw>& frames() const { return visible_; }

    std::function<void(const render::Animation&)> playSound;

private:
    struct Running {
        AnimTrack track;
        std::array<int, 8> frameCounter{};
        int lastFacing = -1; // facing of the previous tick (moves)
    };
    std::vector<Running> tracks_;
    std::vector<std::function<void()>> pendingEnds_;
    std::vector<AnimDraw> visible_;
    int now_ = 0;
};

} // namespace openday::game
