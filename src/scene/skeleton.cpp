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
