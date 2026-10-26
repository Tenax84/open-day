#include "anim/animator.hpp"

#include <algorithm>
#include <tuple>

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
    struct Keyed {
        int group;
        Point frame; // uncropped frame top-left, the sort key
        AnimDraw draw;
    };
    std::vector<Keyed> drawn;
    std::vector<std::function<void()>> endsNow;
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
        Point pos = t.pos, tile = t.tile;
        if (!t.path.empty()) {
            const SlideStep& s = t.path[static_cast<std::size_t>(std::min<int>(step, static_cast<int>(t.path.size()) - 1))];
            facing = s.facing & 7;
            pos = s.pos;
            tile = s.tile;
        }
        if (t.loop) {
            // A new facing starts its clip from the first frame: the clip of
            // the previous facing is rewound (unit-animation.md 5).
            if (cur.lastFacing >= 0 && cur.lastFacing != facing) cur.frameCounter[static_cast<std::size_t>(cur.lastFacing)] = 0;
            cur.lastFacing = facing;
        }
        const render::Animation* clip = t.clips[static_cast<std::size_t>(t.loop ? facing : 0)];
        bool shown = false;
        if (clip && !clip->frames.empty()) {
            const int count = static_cast<int>(clip->frames.size());
            int frame = 0;
            if (t.loop) frame = cur.frameCounter[static_cast<std::size_t>(facing)]++ % count;
            else frame = std::min(step, count - 1);
            const render::AnimFrame& f = clip->frames[static_cast<std::size_t>(frame)];
            if (f.sprite.width > 0) {
                Keyed k{t.group, pos, {pos.x + f.left, pos.y + f.top, &f.sprite, {}}};
                if (t.occlusion == Occlusion::AtRest) k.draw.refresh = refreshAtRest(tile);
                else if (t.occlusion == Occlusion::Translate) k.draw.refresh = refreshTranslate(tile, facing);
                drawn.push_back(std::move(k));
                shown = true;
            }
        }
        if (now_ == t.start + t.duration) {
            // An end part without a clip (e.g. infantry has no move-end clip)
            // follows a part that showed frames one tick later; deferring its
            // end action too would leave a tick without the unit.
            if (shown) pendingEnds_.push_back(t.onEnd ? t.onEnd : std::function<void()>());
            else if (t.onEnd) endsNow.push_back(t.onEnd);
        }
    }
    std::erase_if(tracks_, [&](const Running& r) { return r.track.start + r.track.duration <= now_; });
    for (auto& fn : endsNow) fn();
    std::stable_sort(drawn.begin(), drawn.end(), [](const Keyed& a, const Keyed& b) {
        return std::tie(a.group, a.frame.y, a.frame.x) < std::tie(b.group, b.frame.y, b.frame.x);
    });
    for (Keyed& k : drawn) visible_.push_back(std::move(k.draw));
}

} // namespace openday::game
