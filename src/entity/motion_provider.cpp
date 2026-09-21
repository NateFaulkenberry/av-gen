#include "entity/motion_provider.hpp"

#include <algorithm>
#include <cmath>

#include <glm/gtc/quaternion.hpp>

namespace avgen::entity {

float inertializationDecay(float halflife, float elapsed) {
    // y = 2 ln2 / halflife is the decay of a critically damped spring released from rest, and
    // `(1 + y t) e^{-y t}` is its exact solution.
    const float y = (2.0f * 0.6931472f) / std::max(halflife, 1e-4f);
    const float t = std::max(elapsed, 0.0f);
    return (1.0f + (y * t)) * std::exp(-y * t);
}

float derivedInertializeHalflife(float soonestRepeatSeconds, float frameSeconds) {
    // A blend cannot be given less room than one frame, and a repeat interval of zero would mean a
    // transition may arrive on the very next frame -- in which case no halflife avoids dropping
    // the whole offset and the equation below has no root. Clamped to one frame, which is the
    // shortest interval the rest of the reasoning is stated over.
    const float frame = std::max(frameSeconds, 1e-4f);
    const float soonest = std::max(soonestRepeatSeconds, frame);
    // f(y) = (the decay's worst single frame) - (the offset left when the next transition may
    // arrive), both as fractions of the jump, which is why the jump's size cancels. Increasing in
    // y: the first term is linear and the second falls monotonically. The root is where the two
    // failures are equally bad, which is the minimum of their maximum.
    const auto f = [&](float y) {
        const float halflife = (2.0f * 0.6931472f) / std::max(y, 1e-6f);
        return ((y * frame) / 2.7182818f) - inertializationDecay(halflife, soonest);
    };
    float lo = 1e-3f;
    float hi = 1.0e4f;
    if (f(lo) >= 0.0f) {
        return (2.0f * 0.6931472f) / lo;
    }
    for (int i = 0; i < 200; ++i) {
        const float mid = 0.5f * (lo + hi);
        if (f(mid) < 0.0f) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    return (2.0f * 0.6931472f) / (0.5f * (lo + hi));
}

void applyInertializedOffset(const scene::Pose& was, const scene::Pose& became, float decay,
                             scene::Pose& out) {
    static const glm::quat kIdentity(1.0f, 0.0f, 0.0f, 0.0f);
    const std::size_t joints = std::min(out.size(), std::min(was.size(), became.size()));
    for (std::size_t j = 0; j < joints; ++j) {
        const scene::Transform& a = was.local[j];
        const scene::Transform& b = became.local[j];
        scene::Transform& t = out.local[j];
        t.position += (a.position - b.position) * decay;
        t.scale += (a.scale - b.scale) * decay;
        // The rotational offset is a rotation, composed rather than added, and slerped from
        // identity by the decay so a half-decayed offset is half the angle rather than half the
        // quaternion.
        const glm::quat offset = a.rotation * glm::conjugate(b.rotation);
        t.rotation = glm::normalize(glm::slerp(kIdentity, offset, decay) * t.rotation);
    }
}

const char* movementModeName(MovementMode mode) {
    switch (mode) {
    case MovementMode::Ground: return "ground";
    case MovementMode::Airborne: return "airborne";
    case MovementMode::Rooted: return "rooted";
    }
    return "ground";
}

const char* motionStatusName(MotionStatus status) {
    switch (status) {
    case MotionStatus::Produced: return "produced";
    case MotionStatus::NoContent: return "no-content";
    case MotionStatus::SkeletonMismatch: return "skeleton-mismatch";
    case MotionStatus::NotReady: return "not-ready";
    case MotionStatus::Unsupported: return "unsupported";
    case MotionStatus::Failed: return "failed";
    }
    return "failed";
}

} // namespace avgen::entity
