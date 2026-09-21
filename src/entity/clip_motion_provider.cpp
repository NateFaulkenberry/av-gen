#include "entity/clip_motion_provider.hpp"

#include <algorithm>
#include <cmath>

namespace avgen::entity {

int ClipMotionProvider::select(const MotionRequest& request) const {
    if (clips_ == nullptr || entries_.empty()) {
        return -1;
    }
    // The mode and the style must both match exactly. Among what is left, the entry whose authored
    // speed is closest to what the body wants -- which is the whole
    // of clip selection for a provider that does not blend, and is deliberately *not* a threshold:
    // a threshold is a gait decision and `Gait::select` already owns it, with hysteresis this has
    // no business duplicating.
    const float want =
        std::sqrt((request.desiredVelocity.x * request.desiredVelocity.x) +
                  (request.desiredVelocity.z * request.desiredVelocity.z));
    int best = -1;
    float bestCost = 0.0f;
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        const ClipEntry& e = entries_[i];
        if (e.mode != request.mode) {
            continue;
        }
        // **An empty entry style matches only an empty request, not everything.** Treating it as
        // a wildcard makes a style that nothing carries -- a typo, a pack that shipped without its
        // "limp" clips -- resolve silently to the default walk, and §64 says not to hide failures.
        // A pack that genuinely wants a catch-all authors one entry per style, explicitly.
        if (e.style != request.style) {
            continue;
        }
        if (static_cast<std::size_t>(e.clip) >= clips_->size()) {
            continue; // an entry naming a clip this rig does not have is skipped, not fatal
        }
        // An entry with no authored speed is a standing pose: it costs what the body's speed is,
        // so it wins when the body is still and loses as soon as it moves.
        const float cost = std::abs(e.authoredSpeed - want);
        if (best < 0 || cost < bestCost) {
            best = static_cast<int>(i);
            bestCost = cost;
        }
    }
    return best;
}

MotionResult ClipMotionProvider::advance(const MotionRequest& request, const MotionMemory& in,
                                         double time, float dt, MotionMemory& next) const {
    MotionResult result;
    // The memory is written on every path, including the failing ones. A chain that falls through
    // discards this copy, but a caller using the provider directly must not be handed a stale
    // value it cannot tell from a fresh one.
    next = in;

    if (clips_ == nullptr) {
        result.status = MotionStatus::NotReady;
        return result;
    }
    const int entryIndex = select(request);
    if (entryIndex < 0) {
        result.status = MotionStatus::NoContent;
        return result;
    }
    const ClipEntry& entry = entries_[static_cast<std::size_t>(entryIndex)];
    const scene::AnimationClip& clip = (*clips_)[static_cast<std::size_t>(entry.clip)];
    if (clip.length() <= 0.0f) {
        result.status = MotionStatus::NoContent;
        return result;
    }

    // Playback rate: the body's speed against the stride the clip was authored for. Clamped for
    // the reason `GaitSettings` gives -- a clip at 3x is a cartoon -- and **reported saturated**
    // through the rate itself, because B.A measured the shipping cast sitting at the clamp floor
    // permanently and a rate that silently stops tracking is how that went unnoticed for so long.
    float rate = 1.0f;
    if (entry.authoredSpeed > 1e-4f) {
        const float want =
            std::sqrt((request.desiredVelocity.x * request.desiredVelocity.x) +
                      (request.desiredVelocity.z * request.desiredVelocity.z));
        rate = std::clamp(want / entry.authoredSpeed, 0.1f, 2.0f);
    }

    // Advance. **The clock is the memory, not the wall and not the timeline**: a provider that
    // computed `now - start` would need a start, which is state it does not own, and a provider
    // that used the timeline directly could not be played at a rate at all. This is also what
    // makes a seek's fixed-step replay land where a play landed.
    const std::uint32_t selection = entry.clip;
    const bool changed = in.selection != selection || in.generation == 0;
    float local = changed ? 0.0f : in.localTime + (dt * rate);
    if (entry.loop) {
        local = scene::wrapTime(local, clip.length());
    } else {
        local = std::clamp(local, 0.0f, clip.length());
    }

    next.localTime = local;
    next.selection = selection;
    next.generation = changed ? in.generation + 1 : in.generation;
    next.phase = clip.length() > 0.0f ? local / clip.length() : 0.0f;
    next.hasPhase = true;
    next.transitionStart = changed ? time : in.transitionStart;

    result.status = MotionStatus::Produced;
    result.content = clip.name;
    result.playbackRate = rate;
    return result;
}

MotionResult ClipMotionProvider::pose(const MotionMemory& memory, const scene::Skeleton& skeleton,
                                      scene::Pose& out) const {
    MotionResult result;
    if (clips_ == nullptr) {
        result.status = MotionStatus::NotReady;
        return result;
    }
    // `selection` is a clip index in THIS provider's space. It is validated rather than trusted:
    // a memory that came from a different provider would index this vector with a database frame,
    // and the chain's `provider` field exists to stop that -- this is the second lock on the door.
    if (static_cast<std::size_t>(memory.selection) >= clips_->size()) {
        result.status = MotionStatus::NoContent;
        return result;
    }
    const scene::AnimationClip& clip = (*clips_)[static_cast<std::size_t>(memory.selection)];
    if (clip.length() <= 0.0f) {
        result.status = MotionStatus::NoContent;
        return result;
    }
    scene::setRestPose(skeleton, out);
    scene::sampleClip(clip, clip.start + memory.localTime, out);
    result.status = MotionStatus::Produced;
    result.content = clip.name;
    return result;
}

} // namespace avgen::entity
