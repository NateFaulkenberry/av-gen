#pragma once

// The stride speed a locomotion clip implies, measured off the clip rather than asserted about it.
//
// Why this is not a runtime diagnostic (ADR-204 §3): `Gait::footSlip` compares the behaviour's
// travel speed with `GaitSettings::walkSpeed`, so when rate matching is on and unsaturated it
// returns 1.0 whatever the clip contains -- the authored number is both the expected value and the
// thing under test. Measuring a clip means knowing which joints are feet, which is asset knowledge
// rather than engine knowledge, so the measurement lives here where the bones can be named.
//
// The method: for an in-place cycle the planted foot is stationary in world space, so in the
// character's own frame it travels backwards along the forward axis at exactly the body's speed.
// The median backward speed of a toe while that toe is in the bottom `contactFraction` of its own
// height range over the cycle is that number. Contact stated as a fraction of the take's own range
// rather than as a height makes it a property of the clip; the answer is stable across fractions
// from 0.10 to 0.30 on every pack this has been run on.
//
// Shared by `test_alien_locomotion.cpp` (the Quaternius alien pack) and
// `test_stylized_wanderer.cpp` (the Mixamo-rigged `alien.gltf`), which name different toe joints
// and therefore cannot share a hard-coded pair.

#include "scene/animation.hpp"
#include "scene/skeleton.hpp"

#include <glm/glm.hpp>

#include <algorithm>
#include <initializer_list>
#include <span>
#include <string_view>
#include <vector>

namespace avgen::testing {

// The first and last key times across every channel -- what the clip actually covers, read from the
// channels rather than from whatever the clip says its duration is.
struct ClipKeySpan {
    float first = 0.0f;
    float last = 0.0f;
    [[nodiscard]] float period() const { return last - first; }
};

[[nodiscard]] inline ClipKeySpan clipKeySpan(const scene::AnimationClip& clip) {
    ClipKeySpan span{1e9f, -1e9f};
    for (const scene::AnimationChannel& c : clip.channels) {
        if (c.times.empty()) {
            continue;
        }
        span.first = std::min(span.first, c.times.front());
        span.last = std::max(span.last, c.times.back());
    }
    return span;
}

// Model-space position of `joint` with `clip` sampled at `t`.
[[nodiscard]] inline glm::vec3 jointPositionAt(const scene::SkinnedRig& rig,
                                               const scene::AnimationClip& clip, int joint, float t,
                                               scene::Pose& pose, std::vector<glm::mat4>& model) {
    scene::setRestPose(rig.skeleton, pose);
    scene::sampleClip(clip, t, pose);
    scene::poseToModel(rig.skeleton, pose, model);
    return glm::vec3(model[static_cast<std::size_t>(joint)][3]);
}

// Model units per second. Returns 0 when the rig carries none of `toeJoints`, which is the honest
// answer for a skeleton this cannot be measured on rather than a number nobody should trust.
[[nodiscard]] inline float clipStrideSpeed(const scene::SkinnedRig& rig,
                                           const scene::AnimationClip& clip,
                                           std::span<const std::string_view> toeJoints,
                                           float contactFraction = 0.2f, float sampleHz = 30.0f) {
    const ClipKeySpan span = clipKeySpan(clip);
    std::vector<float> times;
    for (float t = span.first; t <= span.last + 1e-4f; t += 1.0f / sampleHz) {
        times.push_back(t);
    }
    scene::Pose pose;
    std::vector<glm::mat4> model;
    std::vector<float> speeds;
    for (const std::string_view toe : toeJoints) {
        const int joint = rig.skeleton.find(toe);
        if (joint < 0) {
            continue;
        }
        std::vector<glm::vec3> track;
        track.reserve(times.size());
        for (const float t : times) {
            track.push_back(jointPositionAt(rig, clip, joint, t, pose, model));
        }
        float lo = track.front().y;
        float hi = track.front().y;
        for (const glm::vec3& p : track) {
            lo = std::min(lo, p.y);
            hi = std::max(hi, p.y);
        }
        const float threshold = lo + (hi - lo) * contactFraction;
        for (std::size_t k = 0; k + 1 < track.size(); ++k) {
            if (track[k].y > threshold || track[k + 1].y > threshold) {
                continue;
            }
            const float dt = times[k + 1] - times[k];
            const float v = -(track[k + 1].z - track[k].z) / dt;
            if (v > 0.0f) {
                speeds.push_back(v);
            }
        }
    }
    if (speeds.empty()) {
        return 0.0f;
    }
    std::sort(speeds.begin(), speeds.end());
    return speeds[speeds.size() / 2];
}

} // namespace avgen::testing
