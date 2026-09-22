#include "entity/root_motion_adapt.hpp"

#include <algorithm>
#include <cmath>

namespace avgen::entity {
namespace {

// The scale that turns `have` into `want`, clamped, with the clamp reported.
float scaleFor(float have, float want, float lo, float hi, bool& clamped) {
    // An authored component of zero cannot be scaled into a non-zero one: multiplying nothing
    // gives nothing however large the factor. Saying so is the honest answer -- and it is the
    // case that makes a naive `want / have` produce an infinity.
    if (std::abs(have) < 1e-5f) {
        clamped = clamped || std::abs(want) > 1e-4f;
        return 1.0f;
    }
    const float raw = want / have;
    const float bounded = std::clamp(raw, lo, hi);
    clamped = clamped || std::abs(bounded - raw) > 1e-4f;
    return bounded;
}

} // namespace

RootMotionAdaptResult adaptRootMotion(const RootMotionAdaptSettings& settings,
                                      const glm::vec3& authored, const glm::vec3& desired,
                                      float dt) {
    RootMotionAdaptResult out;
    out.displacement = authored;
    if (dt <= 0.0f) {
        // ADR-521: the first tick of every render hands out dt 0. Nothing to scale toward.
        return out;
    }

    // What the clip covers this step, and what the controller wants covered, both as displacement
    // so the comparison has one unit.
    const glm::vec3 want = desired * dt;
    const float wantSpeed = std::sqrt((desired.x * desired.x) + (desired.z * desired.z));
    if (wantSpeed < settings.standSpeed) {
        // A body stopping is the stop's business (§9's ramp), not this function's. Driving the
        // scale toward its floor here would fight that ramp and the two would compose into
        // something neither intended.
        return out;
    }

    if (settings.separateLateral) {
        out.forwardScale = scaleFor(authored.z, want.z, settings.minScale, settings.maxScale, out.clamped);
        out.lateralScale = scaleFor(authored.x, want.x, settings.minScale, settings.maxScale, out.clamped);
    } else {
        const float have = std::sqrt((authored.x * authored.x) + (authored.z * authored.z));
        const float need = std::sqrt((want.x * want.x) + (want.z * want.z));
        out.forwardScale = scaleFor(have, need, settings.minScale, settings.maxScale, out.clamped);
        out.lateralScale = out.forwardScale;
    }

    // **Y is never scaled.** A clip that steps down a kerb steps down the same kerb at any pace,
    // and scaling the vertical would sink the body into a slope or float it above one -- which is
    // the terrain constraint §37 names, and the reason this is not `authored * scale`.
    out.displacement = glm::vec3(authored.x * out.lateralScale, authored.y,
                                 authored.z * out.forwardScale);
    return out;
}

} // namespace avgen::entity
