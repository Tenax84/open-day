#include "anim/animator.hpp"

#include <algorithm>

namespace openday::game {

int Animator::add(AnimTrack track) {
    track.start = std::max(track.start, now_ + 1);
    track.duration = std::max(track.duration, 0);
    const int end = track.start + track.duration;
    tracks_.push_back({std::move(track), {}});
    return end;
}

void Animator::tick() {
    // End actions of the previous tick run now, after its frame was shown.
    std::vector<std::function<void()>> ends;
    ends.swap(pendingEnds_);
    for (auto& fn : ends)
        if (fn) fn();

    ++now_;
    visible_.clear();
    // Start actions may schedule more tracks; iterate by index.
    for (std::size_t i = 0; i < tracks_.size(); ++i) {
        Running& r = tracks_[i];
        if (r.track.start > now_) continue;
        if (r.track.start == now_) {
            if (r.track.onStart) {
                auto fn = r.track.onStart; // may reallocate tracks_
                fn();
            }
            Running& rr = tracks_[i];
            if (rr.track.sound && playSound) playSound(*rr.track.sound);
        }
        Running& cur = tracks_[i];
        const AnimTrack& t = cur.track;
        const int step = now_ - t.start;
        int facing = 0;
        Overlay ov;
        if (!t.path.empty()) {
            const SlideStep& s = t.path[static_cast<std::size_t>(std::min<int>(step, static_cast<int>(t.path.size()) - 1))];
            facing = s.facing & 7;
            ov.x = s.pos.x;
            ov.y = s.pos.y;
            ov.tileX = s.tile.x;
            ov.tileY = s.tile.y;
        } else {
            ov.x = t.pos.x;
            ov.y = t.pos.y;
            ov.tileX = t.tile.x;
            ov.tileY = t.tile.y;
        }
        const render::Animation* clip = t.clips[static_cast<std::size_t>(t.loop ? facing : 0)];
        if (clip && !clip->frames.empty()) {
            const int count = static_cast<int>(clip->frames.size());
            int frame = 0;
            if (t.loop) frame = cur.frameCounter[static_cast<std::size_t>(facing)]++ % count;
            else frame = std::min(step, count - 1);
            const render::AnimFrame& f = clip->frames[static_cast<std::size_t>(frame)];
            if (f.sprite.width > 0) {
                ov.x += f.left;
                ov.y += f.top;
                ov.sprite = &f.sprite;
                visible_.push_back(ov);
            }
        }
        if (now_ == t.start + t.duration) pendingEnds_.push_back(t.onEnd ? t.onEnd : std::function<void()>());
    }
    std::erase_if(tracks_, [&](const Running& r) { return r.track.start + r.track.duration <= now_; });
}

} // namespace openday::game
