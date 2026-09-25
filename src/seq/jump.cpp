#include "seq/jump.hpp"

#include <algorithm>
#include <cmath>

namespace avgen::seq {

std::vector<ActorKey> jumpKeys(const entity::JumpArc& arc, double launchSeconds, float hz) {
    std::vector<ActorKey> keys;
    const int steps = std::max(1, static_cast<int>(std::ceil(arc.duration * std::max(hz, 1.0f))));
    keys.reserve(static_cast<std::size_t>(steps) + 1);
    for (int i = 0; i <= steps; ++i) {
        const float t = arc.duration * static_cast<float>(i) / static_cast<float>(steps);
        ActorKey k;
        k.timeSeconds = launchSeconds + static_cast<double>(t);
        k.position = i == steps ? arc.to : arc.at(t);
        k.interp = params::KeyInterp::Linear;
        keys.push_back(k);
    }
    return keys;
}

std::pair<double, double> jumpSpan(const entity::JumpArc& arc, double launchSeconds) {
    return {launchSeconds, launchSeconds + static_cast<double>(arc.duration)};
}

std::optional<ClipCue> jumpClipCue(const scene::ClipSemantics& clip, const entity::JumpArc& arc,
                                   double launchSeconds, std::string then) {
    const scene::ClipEvent* takeoff = clip.event("takeoff");
    const scene::ClipEvent* touchdown = clip.event("touchdown");
    if (takeoff == nullptr || touchdown == nullptr || touchdown->seconds <= takeoff->seconds || arc.duration <= 0.0f) {
        return std::nullopt;
    }
    ClipCue cue;
    cue.clip = clip.clip;
    // Clip seconds per timeline second: the clip's flight, stretched or squeezed onto the arc's.
    cue.speed = (touchdown->seconds - takeoff->seconds) / arc.duration;
    cue.timeSeconds = launchSeconds - static_cast<double>(takeoff->seconds / cue.speed);
    cue.playback = ClipPlayback::Once;
    cue.then = std::move(then);
    return cue;
}

JumpTimes jumpTimes(const entity::JumpArc& arc, double launchSeconds) {
    return {launchSeconds, launchSeconds + static_cast<double>(arc.apexTime()),
            launchSeconds + static_cast<double>(arc.duration)};
}

} // namespace avgen::seq
