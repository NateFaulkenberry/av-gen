#include "scene/ik.hpp"

#include <algorithm>
#include <cmath>

namespace avgen::scene {
namespace {

constexpr glm::quat kIdentity{1.0f, 0.0f, 0.0f, 0.0f};
constexpr float kPi = 3.14159265358979f;
// Lengths below this are not bones. A rig model-space unit is a metre on the farm pack and an
// asset unit on the aliens, and no rig in this repository carries a bone under a millimetre.
constexpr float kMinBone = 1e-5f;

[[nodiscard]] float lengthOf(const glm::vec3& v) { return std::sqrt(glm::dot(v, v)); }

[[nodiscard]] glm::vec3 normalizeOr(const glm::vec3& v, const glm::vec3& fallback) {
    const float len = lengthOf(v);
    return len > kMinBone ? v * (1.0f / len) : fallback;
}

// Any unit vector perpendicular to `v`, chosen from the axis `v` leans on least so the cross
// product never runs out of magnitude.
[[nodiscard]] glm::vec3 anyPerpendicular(const glm::vec3& v) {
    const glm::vec3 axis = std::abs(v.x) < 0.9f ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
    return normalizeOr(glm::cross(v, axis), glm::vec3(0.0f, 1.0f, 0.0f));
}

} // namespace

const char* ikStatusName(IkStatus status) {
    switch (status) {
    case IkStatus::Solved: return "solved";
    case IkStatus::Clamped: return "clamped";
    case IkStatus::DegenerateBone: return "degenerate-bone";
    case IkStatus::DegenerateTarget: return "degenerate-target";
    case IkStatus::DegenerateBend: return "degenerate-bend";
    }
    return "solved";
}

glm::quat shortestArc(const glm::vec3& from, const glm::vec3& to, const glm::vec3& fallbackAxis) {
    const float d = std::clamp(glm::dot(from, to), -1.0f, 1.0f);
    if (d > 0.9999999f) {
        return kIdentity;
    }
    if (d < -0.9999999f) {
        // Antiparallel: every axis perpendicular to `from` is a half-turn that works, so the caller
        // has to say which one. Left to the cross product this is a NaN, and ozz's 0.17 release
        // note is the same bug found by somebody else.
        const glm::vec3 axis = normalizeOr(glm::cross(from, fallbackAxis), anyPerpendicular(from));
        return glm::angleAxis(kPi, axis);
    }
    const glm::vec3 axis = glm::cross(from, to);
    const float len = lengthOf(axis);
    if (len < 1e-9f) {
        return kIdentity;
    }
    return glm::normalize(glm::angleAxis(std::acos(d), axis * (1.0f / len)));
}

// The law of cosines, then two aims.
//
// Step 1 sets the knee angle so the root-to-tip distance becomes what the target asks for, about
// the plane the chain is already bent in. Step 2 swings the whole (now correctly-shaped) limb so
// its tip direction points at the target. Step 3, if a pole was given, spins the limb about that
// root-to-target axis until the knee faces the pole.
//
// The order is what makes it exact: the first step fixes the *distance* and the second fixes the
// *direction*, and neither disturbs the other -- a rotation about the root cannot change
// |tip - root|, and the bend happens before any of it. Doing the aim first and the bend second
// would be the common bug where the tip walks off the target as the knee closes.
TwoBoneSolution solveTwoBone(const TwoBoneChain& chain, const glm::vec3& target, const glm::vec3& pole,
                             bool hasPole, float extension) {
    TwoBoneSolution out;
    out.mid = chain.mid;
    out.tip = chain.tip;

    const glm::vec3 ab = chain.mid - chain.root;
    const glm::vec3 bc = chain.tip - chain.mid;
    const float l1 = lengthOf(ab);
    const float l2 = lengthOf(bc);
    out.upperLength = l1;
    out.lowerLength = l2;
    out.requested = lengthOf(target - chain.root);
    out.achieved = lengthOf(chain.tip - chain.root);
    if (l1 < kMinBone || l2 < kMinBone) {
        out.status = IkStatus::DegenerateBone;
        return out;
    }
    out.minReach = std::abs(l1 - l2);
    out.maxReach = (l1 + l2) * std::clamp(extension, 0.0f, 1.0f);
    // A `minReach` above `maxReach` is possible with a silly extension and one very long bone; the
    // limb then has exactly one distance it can be at, and clamping to `minReach` is that distance.
    out.maxReach = std::max(out.maxReach, out.minReach);

    const glm::vec3 at = target - chain.root;
    if (out.requested < kMinBone) {
        out.status = IkStatus::DegenerateTarget;
        return out;
    }

    // ---- step 1: the knee angle -----------------------------------------------------------
    const float wanted = std::clamp(out.requested, out.minReach, out.maxReach);
    const bool clamped = std::abs(wanted - out.requested) > 1e-6f;
    const glm::vec3 u = -ab * (1.0f / l1); // mid -> root
    const glm::vec3 v = bc * (1.0f / l2);  // mid -> tip
    const float currentCos = std::clamp(glm::dot(u, v), -1.0f, 1.0f);
    const float currentAngle = std::acos(currentCos);
    const float wantedCos = std::clamp((l1 * l1 + l2 * l2 - wanted * wanted) / (2.0f * l1 * l2), -1.0f, 1.0f);
    const float wantedAngle = std::acos(wantedCos);

    // The plane the chain bends in now. `cross(v, u)` is the axis of the shortest rotation taking
    // the shin onto the thigh, so turning about it by a positive angle *closes* the knee.
    glm::vec3 bendAxis = glm::cross(v, u);
    const float bendSine = lengthOf(bendAxis);
    if (bendSine < kStraightEpsilon) {
        if (!hasPole) {
            out.status = IkStatus::DegenerateBend;
            return out;
        }
        // Straight, but told which way to fold: the plane through the root, the target and the pole.
        const glm::vec3 toPole = pole - chain.root;
        bendAxis = glm::cross(at, toPole);
        if (lengthOf(bendAxis) < kStraightEpsilon) {
            // The pole is on the root-target line, so it names no plane either.
            out.status = IkStatus::DegenerateBend;
            return out;
        }
        // Sign: the knee must end up on the pole's side. `cross(at, toPole)` turns the limb the
        // short way from the target direction towards the pole, which is the side we want.
        bendAxis = -bendAxis;
    }
    bendAxis = glm::normalize(bendAxis);
    out.midBend = glm::normalize(glm::angleAxis(currentAngle - wantedAngle, bendAxis));
    out.kneeAngle = wantedAngle;

    // Where that alone puts the tip. The mid has not moved yet.
    const glm::vec3 bent = chain.mid + (out.midBend * bc);

    // ---- step 2: aim the limb at the target -------------------------------------------------
    const glm::vec3 aimFrom = normalizeOr(bent - chain.root, glm::vec3(0.0f));
    const glm::vec3 aimTo = at * (1.0f / out.requested);
    if (glm::dot(aimFrom, aimFrom) < 0.5f) {
        // The bend folded the tip exactly onto the root. Only reachable when l1 == l2 and the
        // target sits on the root, which `DegenerateTarget` already caught -- but a solver that
        // trusts that is a solver one refactor away from a NaN.
        out.status = IkStatus::DegenerateTarget;
        out.midBend = kIdentity;
        return out;
    }
    glm::quat rootDelta = shortestArc(aimFrom, aimTo, bendAxis);

    // ---- step 3: the pole ---------------------------------------------------------------------
    if (hasPole) {
        const glm::vec3 axis = aimTo;
        const glm::vec3 midNow = chain.root + (rootDelta * ab);
        const glm::vec3 have = (midNow - chain.root) - glm::dot(midNow - chain.root, axis) * axis;
        const glm::vec3 wantVec = (pole - chain.root) - glm::dot(pole - chain.root, axis) * axis;
        const float haveLen = lengthOf(have);
        const float wantLen = lengthOf(wantVec);
        // A knee on the root-target axis has no side, and a pole on it names none. Either way there
        // is nothing to rotate towards, and the aim's own plane stands.
        if (haveLen > kMinBone && wantLen > kMinBone) {
            const glm::vec3 h = have * (1.0f / haveLen);
            const glm::vec3 w = wantVec * (1.0f / wantLen);
            const float angle = std::atan2(glm::dot(glm::cross(h, w), axis), std::clamp(glm::dot(h, w), -1.0f, 1.0f));
            rootDelta = glm::normalize(glm::angleAxis(angle, axis) * rootDelta);
        }
    }
    out.rootDelta = rootDelta;

    out.mid = chain.root + (rootDelta * ab);
    out.tip = chain.root + (rootDelta * (bent - chain.root));
    out.achieved = lengthOf(out.tip - chain.root);
    out.status = clamped ? IkStatus::Clamped : IkStatus::Solved;
    return out;
}

} // namespace avgen::scene
