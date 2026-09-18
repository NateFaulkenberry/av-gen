#include "scene/root_motion.hpp"

#include "scene/animation.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <cctype>

namespace avgen::scene {

bool rootMotionAxesFromName(std::string_view name, RootMotionAxes& out) {
    RootMotionAxes axes{false, false, false};
    if (name == "none") {
        out = axes;
        return true;
    }
    for (const char c : name) {
        switch (static_cast<char>(std::tolower(static_cast<unsigned char>(c)))) {
        case 'x': axes.x = true; break;
        case 'y': axes.y = true; break;
        case 'z': axes.z = true; break;
        default: return false;
        }
    }
    out = axes;
    return true;
}

std::string rootMotionAxesName(const RootMotionAxes& axes) {
    if (axes.none()) {
        return "none";
    }
    std::string s;
    if (axes.x) s += 'x';
    if (axes.y) s += 'y';
    if (axes.z) s += 'z';
    return s;
}

namespace {

// The lowest-indexed joint `clip` gives a translation channel to, or -1. ADR-260's rule, and the
// reason it is this and not `joints[0]`: import order is topological, so the lowest index among
// the *animated* joints is the highest one in the hierarchy this clip moves, while `joints[0]` is
// an armature wrapper no clip animates at all.
int lowestTranslatedJoint(const AnimationClip& clip) {
    int best = -1;
    for (const AnimationChannel& c : clip.channels) {
        if (c.path != AnimationPath::Translation || c.times.empty()) {
            continue;
        }
        const int j = static_cast<int>(c.joint);
        if (best < 0 || j < best) {
            best = j;
        }
    }
    return best;
}

int topmostAncestor(const Skeleton& skeleton, int joint) {
    int j = joint;
    // Parents always precede their children (`Skeleton::valid`), so this terminates in at most
    // `joint` steps and cannot cycle.
    for (int guard = 0; j >= 0 && guard <= static_cast<int>(skeleton.joints.size()); ++guard) {
        const int parent = skeleton.joints[static_cast<std::size_t>(j)].parent;
        if (parent < 0) {
            return j;
        }
        j = parent;
    }
    return joint;
}

} // namespace

glm::vec3 jointModelPosition(const Skeleton& skeleton, const Pose& pose, int joint) {
    if (joint < 0 || static_cast<std::size_t>(joint) >= pose.size()) {
        return glm::vec3(0.0f);
    }
    // Up first (cheap, no allocation for any rig this project loads), then compose down.
    int chain[static_cast<std::size_t>(64)];
    int n = 0;
    for (int j = joint; j >= 0 && n < 64; j = skeleton.joints[static_cast<std::size_t>(j)].parent) {
        chain[n++] = j;
    }
    glm::mat4 m(1.0f);
    for (int i = n - 1; i >= 0; --i) {
        m = m * pose.local[static_cast<std::size_t>(chain[i])].matrix();
    }
    return glm::vec3(m[3]);
}

std::vector<std::string> RootMotionSet::bind(const std::vector<RootMotionSpec>& specs,
                                             const Skeleton& skeleton,
                                             const std::vector<AnimationClip>& clips) {
    bindings_.clear();
    std::vector<std::string> problems;
    for (const RootMotionSpec& spec : specs) {
        const int ci = findClip(clips, spec.clip);
        if (ci < 0) {
            problems.push_back(fmt::format("root motion: this rig has no clip '{}'", spec.clip));
            continue;
        }
        const AnimationClip& clip = clips[static_cast<std::size_t>(ci)];
        RootMotionBinding b;
        b.clip = ci;
        b.clipName = clip.name;
        b.axes = spec.axes;
        if (spec.joint.empty()) {
            b.joint = lowestTranslatedJoint(clip);
            if (b.joint < 0) {
                problems.push_back(fmt::format(
                    "root motion: clip '{}' translates no joint, so there is nothing to extract",
                    clip.name));
                continue;
            }
        } else {
            b.joint = skeleton.find(spec.joint);
            if (b.joint < 0) {
                problems.push_back(fmt::format("root motion: clip '{}' names joint '{}', which this "
                                               "rig does not carry",
                                               clip.name, spec.joint));
                continue;
            }
        }
        b.jointName = skeleton.joints[static_cast<std::size_t>(b.joint)].name;
        b.carrier = topmostAncestor(skeleton, b.joint);
        if (b.axes.none()) {
            problems.push_back(fmt::format(
                "root motion: clip '{}' opts in with no axes, which is the same as not opting in",
                clip.name));
            continue;
        }
        // A second opt-in for the same clip is an authoring mistake with two plausible readings
        // and no good one; the first wins and the second is named.
        const auto clash = std::find_if(bindings_.begin(), bindings_.end(),
                                        [&](const RootMotionBinding& e) { return e.clip == b.clip; });
        if (clash != bindings_.end()) {
            problems.push_back(
                fmt::format("root motion: clip '{}' is opted in twice; the first wins", clip.name));
            continue;
        }
        bindings_.push_back(std::move(b));
    }
    return problems;
}

const RootMotionBinding* RootMotionSet::find(int clipIndex) const {
    for (const RootMotionBinding& b : bindings_) {
        if (b.clip == clipIndex) {
            return &b;
        }
    }
    return nullptr;
}

glm::vec3 rootMotionDisplacement(const Skeleton& skeleton, const std::vector<AnimationClip>& clips,
                                 const RootMotionBinding& binding, float clipSeconds,
                                 Pose& scratchPose) {
    if (binding.clip < 0 || static_cast<std::size_t>(binding.clip) >= clips.size() ||
        binding.joint < 0) {
        return glm::vec3(0.0f);
    }
    const AnimationClip& clip = clips[static_cast<std::size_t>(binding.clip)];
    // `clip.start` is the *first key time*, which on this content is 1/30 s and not zero: Blender
    // writes the frame range it was given, and a take authored on frames 1..32 arrives at 1/30 s.
    // Measuring from zero would read a held first pose as a displacement (animation.hpp says what
    // that cost the whole alien pack once already).
    setRestPose(skeleton, scratchPose);
    sampleClip(clip, clip.start, scratchPose);
    const glm::vec3 from = jointModelPosition(skeleton, scratchPose, binding.joint);
    setRestPose(skeleton, scratchPose);
    sampleClip(clip, clipSeconds, scratchPose);
    const glm::vec3 to = jointModelPosition(skeleton, scratchPose, binding.joint);
    return (to - from) * binding.axes.mask();
}

void applyRootMotionCompensation(const RootMotionBinding& binding, const glm::vec3& displacement,
                                 Pose& pose) {
    if (binding.carrier < 0 || static_cast<std::size_t>(binding.carrier) >= pose.size()) {
        return;
    }
    // The carrier is parentless, so its local translation *is* its model translation, and the
    // translation component of `T * R * S` is untouched by R and S. Subtracting here therefore
    // moves every joint beneath it by exactly `-displacement` in model space and rotates nothing.
    pose.local[static_cast<std::size_t>(binding.carrier)].position -= displacement;
}

} // namespace avgen::scene
