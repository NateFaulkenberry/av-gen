#include "scene/skeleton.hpp"

#include <glm/gtc/quaternion.hpp>

#include <algorithm>

namespace avgen::scene {

bool Skeleton::valid() const {
    if (joints.empty() || palette.empty()) {
        return false;
    }
    if (palette.size() != inverseBind.size() || palette.size() > kMaxPaletteJoints) {
        return false;
    }
    for (std::size_t i = 0; i < joints.size(); ++i) {
        const int parent = joints[i].parent;
        if (parent >= static_cast<int>(i)) {
            return false; // a child must follow its parent, so one pass can go local -> model
        }
        if (parent < -1) {
            return false;
        }
    }
    return std::all_of(palette.begin(), palette.end(),
                       [&](std::uint32_t j) { return j < joints.size(); });
}

int Skeleton::find(std::string_view jointName) const {
    for (std::size_t i = 0; i < joints.size(); ++i) {
        if (joints[i].name == jointName) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

Pose restPose(const Skeleton& skeleton) {
    Pose pose;
    setRestPose(skeleton, pose);
    return pose;
}

void setRestPose(const Skeleton& skeleton, Pose& pose) {
    pose.local.resize(skeleton.joints.size());
    for (std::size_t i = 0; i < skeleton.joints.size(); ++i) {
        pose.local[i] = skeleton.joints[i].rest;
    }
}

void poseToModel(const Skeleton& skeleton, const Pose& pose, std::vector<glm::mat4>& out) {
    const std::size_t count = skeleton.joints.size();
    out.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        const glm::mat4 local = i < pose.local.size() ? pose.local[i].matrix() : skeleton.joints[i].rest.matrix();
        const int parent = skeleton.joints[i].parent;
        // Parents precede children (Skeleton::valid enforces it), so one forward pass suffices.
        out[i] = parent >= 0 && static_cast<std::size_t>(parent) < i ? out[static_cast<std::size_t>(parent)] * local
                                                                     : local;
    }
}

void jointPalette(const Skeleton& skeleton, const std::vector<glm::mat4>& model, std::vector<glm::mat4>& out) {
    out.resize(skeleton.palette.size());
    for (std::size_t k = 0; k < skeleton.palette.size(); ++k) {
        const std::uint32_t joint = skeleton.palette[k];
        if (joint < model.size() && k < skeleton.inverseBind.size()) {
            out[k] = model[joint] * skeleton.inverseBind[k];
        } else {
            out[k] = glm::mat4(1.0f);
        }
    }
}

void skinningPalette(const Skeleton& skeleton, const Pose& pose, std::vector<glm::mat4>& scratch,
                     std::vector<glm::mat4>& out) {
    poseToModel(skeleton, pose, scratch);
    jointPalette(skeleton, scratch, out);
}

JointMask resolveJointMask(const Skeleton& skeleton, const JointMaskSpec& spec) {
    JointMask mask;
    mask.weight.assign(skeleton.joints.size(), 0.0f);
    mask.named = static_cast<std::uint32_t>(spec.joints.size());
    std::vector<bool> subtree;
    for (std::size_t n = 0; n < spec.joints.size(); ++n) {
        const int root = skeleton.find(spec.joints[n]);
        if (root < 0) {
            // The honest failure. A name this rig does not carry is not a weight of zero that
            // happens to look the same; it is a question the mask could not answer, and the caller
            // gets to say so out loud.
            mask.missing.push_back(spec.joints[n]);
            continue;
        }
        const float w = n < spec.weights.size() ? std::clamp(spec.weights[n], 0.0f, 1.0f) : 1.0f;
        mask.weight[static_cast<std::size_t>(root)] = std::max(mask.weight[static_cast<std::size_t>(root)], w);
        if (!spec.descendants) {
            continue;
        }
        // Parents precede children, so one forward pass carries a named root's weight down its
        // subtree. Marked per root rather than read back out of `mask.weight`, so two roots with
        // different weights do not leak into each other's subtrees. On the alien pack this loop
        // adds nothing at all for a head -- the rig is flat -- which is why the flag is off by
        // default and why a mask is a group of names first.
        subtree.assign(skeleton.joints.size(), false);
        subtree[static_cast<std::size_t>(root)] = true;
        for (std::size_t i = static_cast<std::size_t>(root) + 1; i < skeleton.joints.size(); ++i) {
            const int parent = skeleton.joints[i].parent;
            if (parent < 0 || !subtree[static_cast<std::size_t>(parent)]) {
                continue;
            }
            subtree[i] = true;
            mask.weight[i] = std::max(mask.weight[i], w);
        }
    }
    for (std::size_t i = 0; i < mask.weight.size(); ++i) {
        if (mask.weight[i] <= 0.0f) {
            continue;
        }
        ++mask.joints;
        for (int p = skeleton.joints[i].parent; p >= 0; p = skeleton.joints[static_cast<std::size_t>(p)].parent) {
            if (mask.weight[static_cast<std::size_t>(p)] > 0.0f) {
                ++mask.nested;
                break;
            }
        }
    }
    return mask;
}

Transform blendTransform(const Transform& a, const Transform& b, float weight) {
    const float w = std::clamp(weight, 0.0f, 1.0f);
    Transform out;
    out.position = glm::mix(a.position, b.position, w);
    out.scale = glm::mix(a.scale, b.scale, w);
    // glm::slerp does not pick the short arc for you; a cross-fade that takes the long way round
    // looks like the character's elbow inverting, which is exactly the "snapping" a blend exists
    // to remove.
    glm::quat to = b.rotation;
    if (glm::dot(a.rotation, to) < 0.0f) {
        to = -to;
    }
    out.rotation = glm::normalize(glm::slerp(a.rotation, to, w));
    return out;
}

void blendPose(const Pose& a, const Pose& b, float weight, Pose& out) {
    const std::size_t count = std::min(a.local.size(), b.local.size());
    // Written elementwise so `out` may alias either input.
    if (out.local.size() != count) {
        out.local.resize(count);
    }
    for (std::size_t i = 0; i < count; ++i) {
        out.local[i] = blendTransform(a.local[i], b.local[i], weight);
    }
}

} // namespace avgen::scene
