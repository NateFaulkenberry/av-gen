#include "entity/motion_bake.hpp"

#include <cmath>

namespace avgen::entity {

Result<MotionBakeResult> bakeMotionSession(const IMotionProvider& provider,
                                           const scene::Skeleton& skeleton,
                                           const MotionScript& script,
                                           const MotionBakeOptions& options) {
    if (skeleton.joints.empty()) {
        return fail("motion bake: the skeleton has no joints");
    }
    if (options.sampleRate <= 0.0f || options.seconds <= 0.0f) {
        return fail("motion bake: a bake needs a positive rate and length");
    }
    if (!script) {
        return fail("motion bake: no request script");
    }
    const auto steps = static_cast<std::uint32_t>(std::lround(options.seconds * options.sampleRate)) + 1u;
    const float dt = 1.0f / options.sampleRate;
    const std::size_t joints = skeleton.joints.size();

    MotionBakeResult out;
    out.steps = steps;
    out.memories.reserve(steps);
    out.clip.name = options.name;
    out.clip.start = 0.0f;
    out.clip.duration = static_cast<float>(steps - 1u) * dt;
    // One channel per joint per path, keyed on every step. Linear, so a key time reproduces the
    // keyed pose exactly and the clip between keys is what the provider would interpolate anyway.
    out.clip.channels.resize(joints * 3u);
    for (std::size_t j = 0; j < joints; ++j) {
        for (std::size_t p = 0; p < 3u; ++p) {
            scene::AnimationChannel& ch = out.clip.channels[(j * 3u) + p];
            ch.joint = static_cast<std::uint32_t>(j);
            ch.path = static_cast<scene::AnimationPath>(p);
            ch.interpolation = scene::Interpolation::Linear;
            ch.times.reserve(steps);
            ch.values.reserve(steps);
        }
    }

    MotionMemory memory;
    scene::Pose pose;
    for (std::uint32_t i = 0; i < steps; ++i) {
        const double time = static_cast<double>(i) * static_cast<double>(dt);
        MotionMemory next;
        const MotionResult advanced = provider.advance(script(i, time), memory, time, dt, next);
        memory = next;
        out.memories.push_back(memory);
        const MotionResult posed = advanced.ok() ? provider.pose(memory, skeleton, pose) : MotionResult{};
        if (!posed.ok()) {
            // Keyed from rest rather than skipped, so the clip's timing is the session's timing and
            // a declined step is visible in the result instead of silently closed up.
            scene::setRestPose(skeleton, pose);
            ++out.declined;
        }
        const auto t = static_cast<float>(time);
        for (std::size_t j = 0; j < joints; ++j) {
            const scene::Transform& tr = pose.local[j];
            scene::AnimationChannel* ch = &out.clip.channels[j * 3u];
            ch[0].times.push_back(t);
            ch[0].values.emplace_back(tr.position, 0.0f);
            ch[1].times.push_back(t);
            ch[1].values.emplace_back(tr.rotation.x, tr.rotation.y, tr.rotation.z, tr.rotation.w);
            ch[2].times.push_back(t);
            ch[2].values.emplace_back(tr.scale, 0.0f);
        }
    }
    return out;
}

} // namespace avgen::entity
