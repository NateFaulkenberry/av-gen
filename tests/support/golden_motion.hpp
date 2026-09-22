#pragma once

// Phase C §79: a small, deterministic motion database made of intentionally distinguishable
// motions, for tests of the matcher's mechanics.
//
// §79's caveat is kept at the top because it is the whole point: **synthetic data validates
// implementation mechanics, not real-world performance.** Every clip here differs from every other
// in one obvious way (speed, direction, curvature, a ramp), so a test can say which one the matcher
// should pick and a wrong pick is unambiguous. None of it says anything about how the matcher does
// on the scout or on 100STYLE; those have their own tests.
//
// The rig is three joints (a travel joint and two feet). Each foot swings along the direction of
// travel relative to the body and is planted for half the cycle, so a planted foot is still in model
// space, as on real travelling content. Every clip is one second at 30 Hz and faces +Z unless it
// says otherwise. Turns carry their heading track (`PackClip::heading`), as §21's variants do.

#include "scene/motion_database.hpp"
#include "scene/motion_pack.hpp"

#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <functional>
#include <tuple>
#include <string>
#include <vector>

namespace avgen::testsupport {

inline constexpr float kGoldenRate = 30.0f;
inline constexpr float kGoldenWalk = 1.2f;
inline constexpr float kGoldenRun = 3.5f;

inline scene::Skeleton goldenRig() {
    scene::Skeleton sk;
    sk.name = "golden";
    sk.joints.push_back(scene::Joint{"root.x", -1, scene::Transform{}});
    sk.joints.push_back(scene::Joint{"foot.l", 0, scene::Transform{}});
    sk.joints.push_back(scene::Joint{"foot.r", 0, scene::Transform{}});
    sk.palette = {0, 1, 2};
    sk.inverseBind = {glm::mat4(1.0f), glm::mat4(1.0f), glm::mat4(1.0f)};
    return sk;
}

struct GoldenSpec {
    std::string name;
    // Where the travel joint is at time t (model space) and which way the body faces (yaw).
    std::function<glm::vec3(float)> path;
    std::function<float(float)> yaw;
    bool loop = true;
    // Swap which foot is recorded as planted, without changing the motion (§47's contact case).
    bool swapContacts = false;
};

// One clip from a spec, and its pack entry.
inline void addGoldenClip(scene::MotionPack& pack, const GoldenSpec& spec) {
    scene::AnimationClip clip;
    clip.name = spec.name;
    clip.start = 0.0f;
    clip.duration = 1.0f;
    scene::AnimationChannel root;
    root.joint = 0;
    root.path = scene::AnimationPath::Translation;
    root.interpolation = scene::Interpolation::Linear;
    scene::AnimationChannel turn = root;
    turn.path = scene::AnimationPath::Rotation;
    scene::AnimationChannel left = root;
    left.joint = 1;
    scene::AnimationChannel right = root;
    right.joint = 2;
    std::vector<float> heading;
    for (int i = 0; i <= 30; ++i) {
        const float t = static_cast<float>(i) / kGoldenRate;
        const glm::vec3 p = spec.path(t);
        const float yaw = spec.yaw(t);
        const glm::quat facing = glm::angleAxis(yaw, glm::vec3(0.0f, 1.0f, 0.0f));
        // The step, in the body's own frame: the travel over a small interval, turned back into it.
        const float h = 1.0f / 120.0f;
        const glm::vec3 v = (spec.path(t + h) - spec.path(t - h)) / (2.0f * h);
        const glm::vec3 local = glm::inverse(facing) * v;
        const float speed = std::sqrt((local.x * local.x) + (local.z * local.z));
        const glm::vec3 dir = speed > 1e-4f ? glm::vec3(local.x / speed, 0.0f, local.z / speed) : glm::vec3(0.0f);
        // Half a stride either side of the hips: a foot planted in model space slides back under a
        // body moving forward at `speed`.
        const float stride = 0.25f * speed;
        const float swing = std::sin(6.283185307179586f * t) * stride;
        root.times.push_back(t);
        root.values.emplace_back(p, 0.0f);
        turn.times.push_back(t);
        turn.values.emplace_back(facing.x, facing.y, facing.z, facing.w);
        left.times.push_back(t);
        left.values.emplace_back(glm::vec3(0.1f, 0.0f, 0.0f) + (dir * swing), 0.0f);
        right.times.push_back(t);
        right.values.emplace_back(glm::vec3(-0.1f, 0.0f, 0.0f) - (dir * swing), 0.0f);
        heading.push_back(yaw);
    }
    clip.channels = {std::move(root), std::move(turn), std::move(left), std::move(right)};

    scene::PackClip meta;
    meta.name = spec.name;
    meta.loop = spec.loop;
    meta.sampleRate = kGoldenRate;
    meta.length = 1.0f;
    meta.frames = 31;
    meta.heading = std::move(heading);
    for (const auto& [joint, from, to] :
         {std::tuple{"foot.l", 0.02f, 0.48f}, std::tuple{"foot.r", 0.52f, 0.98f}}) {
        scene::ContactTrack track;
        track.joint = joint;
        track.jointIndex = pack.skeleton.find(joint);
        track.kind = scene::ContactKind::Foot;
        const bool swap = spec.swapContacts;
        track.spans.push_back(scene::ContactSpan{swap ? (from < 0.5f ? 0.52f : 0.02f) : from,
                                                 swap ? (from < 0.5f ? 0.98f : 0.48f) : to, 1.0f});
        meta.contacts.push_back(track);
    }
    pack.animation.push_back(std::move(clip));
    pack.clips.push_back(std::move(meta));
}

inline float noTurn(float) { return 0.0f; }

// The golden corpus: every clip one obvious difference away from the others.
inline scene::MotionPack goldenPack() {
    scene::MotionPack pack;
    pack.name = "golden";
    pack.skeleton = goldenRig();
    pack.skeletonDigest = scene::skeletonDigest(pack.skeleton);
    const auto line = [](glm::vec3 dir, float speed) {
        return [dir, speed](float t) { return dir * (speed * t); };
    };
    const auto arc = [](float rate, float speed) {
        return [rate, speed](float t) {
            // A circle of radius speed/rate, starting heading +Z and turning toward +X for rate > 0.
            const float r = speed / rate;
            return glm::vec3(r * (1.0f - std::cos(rate * t)), 0.0f, r * std::sin(rate * t));
        };
    };
    const auto ramp = [](float from, float to) {
        // Speed from `from` to `to` over the second: z = from*t + (to-from)*t^2/2.
        return [from, to](float t) { return glm::vec3(0.0f, 0.0f, (from * t) + (0.5f * (to - from) * t * t)); };
    };
    addGoldenClip(pack, {"Idle", line(glm::vec3(0, 0, 1), 0.0f), noTurn});
    addGoldenClip(pack, {"Walk", line(glm::vec3(0, 0, 1), kGoldenWalk), noTurn});
    addGoldenClip(pack, {"Run", line(glm::vec3(0, 0, 1), kGoldenRun), noTurn});
    addGoldenClip(pack, {"Back", line(glm::vec3(0, 0, -1), kGoldenWalk), noTurn});
    addGoldenClip(pack, {"StrafeLeft", line(glm::vec3(1, 0, 0), kGoldenWalk), noTurn});
    addGoldenClip(pack, {"StrafeRight", line(glm::vec3(-1, 0, 0), kGoldenWalk), noTurn});
    addGoldenClip(pack, {"TurnLeft", arc(1.5f, kGoldenWalk), [](float t) { return 1.5f * t; }, false});
    addGoldenClip(pack, {"TurnRight", arc(-1.5f, kGoldenWalk), [](float t) { return -1.5f * t; }, false});
    addGoldenClip(pack, {"Start", ramp(0.0f, kGoldenWalk), noTurn, false});
    addGoldenClip(pack, {"Stop", ramp(kGoldenWalk, 0.0f), noTurn, false});
    return pack;
}

inline scene::MotionDatabaseOptions goldenOptions() {
    scene::MotionDatabaseOptions options;
    options.sampleRate = kGoldenRate;
    options.config.joints = {"foot.l", "foot.r"};
    options.config.contactJoints = {"foot.l", "foot.r"};
    options.config.trajectoryTimes = {0.2f, 0.4f, 0.6f};
    return options;
}

} // namespace avgen::testsupport
