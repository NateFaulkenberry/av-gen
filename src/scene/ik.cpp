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
        // Straight, but told which way to fold: the plane through the root, the target and the
        // pole. Both directions are normalised *before* the cross product, so the test below is a
        // sine and not a length. Crossing the raw vectors and comparing against `kStraightEpsilon`
        // would be a threshold in square metres: a chick's leg is 0.041 model units long, so two
        // perfectly good directions 2 degrees apart cross to 6e-5 and the solve would refuse a
        // plane it had been handed. The same arithmetic on a horse passes. Scale-dependent
        // thresholds fail on exactly one end of a content set and look like an asset problem.
        const glm::vec3 toTargetDir = at * (1.0f / out.requested);
        const glm::vec3 toPoleDir = normalizeOr(pole - chain.root, glm::vec3(0.0f));
        bendAxis = glm::cross(toTargetDir, toPoleDir);
        if (glm::dot(toPoleDir, toPoleDir) < 0.5f || lengthOf(bendAxis) < kStraightEpsilon) {
            // The pole is on the root-target line, or on top of the root: it names no plane either.
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


// ---- reachable contact solving (ADR-544) --------------------------------------------------------

const char* bodyCompensationStatusName(BodyCompensationStatus status) {
    switch (status) {
    case BodyCompensationStatus::NotNeeded: return "not-needed";
    case BodyCompensationStatus::Solved: return "solved";
    case BodyCompensationStatus::Limited: return "limited";
    case BodyCompensationStatus::Impossible: return "impossible";
    }
    return "?";
}

namespace {

// The worst amount by which a demand exceeds its limb, given a body translation, and how many
// demands exceed it at all.
struct Shortfall {
    float worst = 0.0f;
    std::uint32_t count = 0;
};

Shortfall shortfallFor(std::span<const ReachDemand> demands, const glm::vec3& translation) {
    Shortfall out;
    for (const ReachDemand& d : demands) {
        const float need = glm::length(d.target - (d.root + translation));
        const float over = need - d.reach;
        if (over > 1e-6f) {
            out.worst = std::max(out.worst, over);
            ++out.count;
        }
    }
    return out;
}

} // namespace

BodyCompensation solveBodyCompensation(std::span<const ReachDemand> demands,
                                       const BodyCompensationLimits& limits) {
    BodyCompensation out;
    const Shortfall before = shortfallFor(demands, glm::vec3(0.0f));
    out.shortfallBefore = before.worst;
    out.unreachableBefore = before.count;
    out.shortfallAfter = before.worst;
    out.unreachableAfter = before.count;
    if (demands.empty() || before.count == 0) {
        return out; // NotNeeded, translation exactly zero
    }

    const glm::vec3 compliance = glm::clamp(limits.compliance, glm::vec3(0.0f), glm::vec3(1.0f));
    const glm::vec3 lo(-std::max(limits.maxLateral, 0.0f), -std::max(limits.maxDown, 0.0f),
                       -std::max(limits.maxLateral, 0.0f));
    const glm::vec3 hi(std::max(limits.maxLateral, 0.0f), std::max(limits.maxUp, 0.0f),
                       std::max(limits.maxLateral, 0.0f));

    glm::vec3 t(0.0f);
    Shortfall best = before;
    glm::vec3 bestT(0.0f);
    for (std::uint32_t sweep = 0; sweep < limits.iterations; ++sweep) {
        // The correction each violated demand is asking for: move the body along the line from its
        // limb root towards its target, by exactly the amount the limb is short. Averaged, because
        // several limbs asking at once should not each get their whole wish.
        glm::vec3 sum(0.0f);
        std::uint32_t asking = 0;
        for (const ReachDemand& d : demands) {
            const glm::vec3 v = d.target - (d.root + t);
            const float need = glm::length(v);
            const float over = need - d.reach;
            if (over <= 1e-6f || need <= 1e-9f) {
                continue;
            }
            sum += v * (over / need);
            ++asking;
        }
        if (asking == 0) {
            break;
        }
        out.sweeps = sweep + 1;
        t = glm::clamp(t + (sum / static_cast<float>(asking)) * compliance, lo, hi);
        const Shortfall now = shortfallFor(demands, t);
        // Keep the best sweep rather than the last. Under a per-axis clamp the sequence is not
        // guaranteed monotone, and returning a translation that is worse than one already visited
        // would be a body that lurched for nothing.
        if (now.worst < best.worst) {
            best = now;
            bestT = t;
        }
        if (now.count == 0) {
            best = now;
            bestT = t;
            break;
        }
    }

    out.translation = bestT;
    out.shortfallAfter = best.worst;
    out.unreachableAfter = best.count;
    if (best.count == 0) {
        out.status = BodyCompensationStatus::Solved;
        return out;
    }
    // Which kind of failure. If the body is sitting on a limit it was allowed to reach, more room
    // would have helped and this is `Limited`. If it is *inside* its limits and still short, no
    // translation satisfies these demands at once and more room would not help.
    const float eps = 1e-5f;
    const bool onLimit = std::fabs(bestT.x - lo.x) < eps || std::fabs(bestT.x - hi.x) < eps ||
                         std::fabs(bestT.y - lo.y) < eps || std::fabs(bestT.y - hi.y) < eps ||
                         std::fabs(bestT.z - lo.z) < eps || std::fabs(bestT.z - hi.z) < eps;
    const bool frozen = compliance.x <= 0.0f || compliance.y <= 0.0f || compliance.z <= 0.0f;
    out.status = (onLimit || frozen) ? BodyCompensationStatus::Limited
                                     : BodyCompensationStatus::Impossible;
    return out;
}

} // namespace avgen::scene
