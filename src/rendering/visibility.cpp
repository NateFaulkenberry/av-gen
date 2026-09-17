// The culling decision, and the reason for it. See visibility.hpp for why this is device-free.
#include "rendering/visibility.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>

namespace avgen::rendering {


FrustumPlanes frustumPlanes(const glm::mat4& m) {
    // Gribb-Hartmann on a 0..1 depth range. glm is column major, so row i is
    // (m[0][i], m[1][i], m[2][i], m[3][i]); the near plane is row 2 alone (z >= 0).
    const auto row = [&](int i) { return glm::vec4(m[0][i], m[1][i], m[2][i], m[3][i]); };
    const glm::vec4 r0 = row(0);
    const glm::vec4 r1 = row(1);
    const glm::vec4 r2 = row(2);
    const glm::vec4 r3 = row(3);
    FrustumPlanes planes{{r3 + r0, r3 - r0, r3 + r1, r3 - r1, r2, r3 - r2}};
    for (glm::vec4& plane : planes) {
        const float length = glm::length(glm::vec3(plane));
        if (length > 1e-12f) {
            plane /= length;
        }
    }
    return planes;
}

float cullProjScale(float fovYRadians, std::uint32_t viewportHeight) {
    const float tangent = std::tan(std::max(fovYRadians, 1e-4f) * 0.5f);
    return static_cast<float>(viewportHeight) / (2.0f * std::max(tangent, 1e-6f));
}

int cullLodLevel(const scene::LodSettings& lod, const FrustumPlanes& planes, const CullCamera& camera,
                 glm::vec3 center, float radius, float lodRadius) {
    const int lodCount = std::clamp(lod.lodCount, 1, scene::kMaxLodLevels);
    const float distance = glm::length(center - camera.position);
    const float screenRadius = radius / std::max(distance, 1e-4f) * camera.projScale;
    // The ladder's own sphere (see the header, and the note in shaders/cull.wgsl).
    const float ladderScreenRadius =
        (lodRadius > 0.0f ? lodRadius : radius) / std::max(distance, 1e-4f) * camera.projScale;
    if (lod.cull) {
        for (const glm::vec4& plane : planes) {
            if (glm::dot(glm::vec3(plane), center) + plane.w < -radius) {
                return -1;
            }
        }
        if (lod.maxDistance > 0.0f && distance - radius > lod.maxDistance) {
            return -1;
        }
        if (lod.minScreenRadius > 0.0f && screenRadius < lod.minScreenRadius) {
            return -1;
        }
    }
    // A threshold of 0 ends the ladder, so an unconfigured object stays at LOD0.
    int level = 0;
    for (int k = 0; k + 1 < lodCount && k < 3; ++k) {
        const float threshold = lod.lodDistances[k];
        if (!(threshold > 0.0f)) {
            break;
        }
        const bool take = lod.lodByScreenSize ? ladderScreenRadius <= threshold : distance >= threshold;
        if (!take) {
            break;
        }
        level = k + 1;
    }
    return level;
}

InstanceBounds instanceBounds(const std::vector<scene::InstanceRecord>& records) {
    InstanceBounds bounds;
    if (records.empty()) {
        return bounds;
    }
    glm::vec3 lo(std::numeric_limits<float>::max());
    glm::vec3 hi(std::numeric_limits<float>::lowest());
    float maxScale = 0.0f;
    float minScale = std::numeric_limits<float>::max();
    for (const scene::InstanceRecord& r : records) {
        const glm::vec3 p(r.position);
        lo = glm::min(lo, p);
        hi = glm::max(hi, p);
        const glm::vec3 s = glm::abs(glm::vec3(r.scale));
        // The shader's radius is `sourceRadius * max(|scale.x|, |scale.y|, |scale.z|)`, so the
        // per-record number to take extremes of is that max -- not the smallest component of any
        // record, which would describe a sphere the shader never forms.
        const float record = std::max(std::max(s.x, s.y), s.z);
        maxScale = std::max(maxScale, record);
        minScale = std::min(minScale, record);
    }
    bounds.min = lo;
    bounds.max = hi;
    bounds.maxAbsScale = maxScale;
    bounds.minAbsScale = minScale;
    bounds.valid = true;
    return bounds;
}

// `limitDistance` is `DetailLimits::proceduralDistanceCull` (ADR-186). It gates only the two
// distance tests: the frustum rejection below stays whatever the policy is, because an object
// entirely behind the camera contributes nothing to any render, offline or not.
bool objectFullyCulled(const scene::LodSettings& lod, const FrustumPlanes& planes, const CullCamera& camera,
                       const glm::mat4& objectToWorld, const InstanceBounds& bounds, float sourceRadius,
                       bool limitDistance) {
    if (!bounds.valid || !lod.cull) {
        return false;
    }
    // The object matrix's largest column length, exactly the `limits.w` the cull pass is given.
    float objectScale = 0.0f;
    for (int c = 0; c < 3; ++c) {
        objectScale = std::max(objectScale, glm::length(glm::vec3(objectToWorld[c])));
    }
    // No record's bounding sphere can be larger than this one, because the shader's radius is
    // sourceRadius * max|record scale| * objectScale and maxAbsScale is the largest of those.
    const float radius = sourceRadius * bounds.maxAbsScale * std::max(objectScale, 1e-6f);
    // World AABB of every record centre: the eight corners of the record-space box through the
    // object matrix. Every centre the shader computes lies inside it.
    glm::vec3 lo(std::numeric_limits<float>::max());
    glm::vec3 hi(std::numeric_limits<float>::lowest());
    for (int corner = 0; corner < 8; ++corner) {
        const glm::vec3 p((corner & 1) ? bounds.max.x : bounds.min.x, (corner & 2) ? bounds.max.y : bounds.min.y,
                          (corner & 4) ? bounds.max.z : bounds.min.z);
        const glm::vec3 world(objectToWorld * glm::vec4(p, 1.0f));
        lo = glm::min(lo, world);
        hi = glm::max(hi, world);
    }
    // Frustum: the shader culls a record when dot(n, centre) + w < -radius. The most positive any
    // centre in the box can be is at the corner the normal points at, so if that corner fails, so
    // does every record.
    for (const glm::vec4& plane : planes) {
        const glm::vec3 n(plane);
        const glm::vec3 farthest(n.x >= 0.0f ? hi.x : lo.x, n.y >= 0.0f ? hi.y : lo.y, n.z >= 0.0f ? hi.z : lo.z);
        if (glm::dot(n, farthest) + plane.w < -radius) {
            return true;
        }
    }
    // Distance and screen size both key on the *closest* the box gets to the camera, which is the
    // most favourable any record can be: the shader keeps a record when dist - radius <= maxDistance
    // and when radius / dist * projScale >= minScreenRadius.
    const glm::vec3 nearest = glm::clamp(camera.position, lo, hi);
    const float nearDistance = glm::length(nearest - camera.position);
    if (limitDistance && lod.maxDistance > 0.0f && nearDistance - radius > lod.maxDistance) {
        return true;
    }
    if (limitDistance && lod.minScreenRadius > 0.0f &&
        radius / std::max(nearDistance, 1e-4f) * camera.projScale < lod.minScreenRadius) {
        return true;
    }
    return false;
}


// ---- the origin-centred source radius (see visibility.hpp) --------------------------------------

float sourceCullRadius(const glm::vec3& boundsMin, const glm::vec3& boundsMax) {
    // The corner of the box furthest from the origin. Componentwise max(|lo|, |hi|) picks it in one
    // step: for each axis, whichever end of the interval is further from zero.
    const glm::vec3 corner = glm::max(glm::abs(boundsMin), glm::abs(boundsMax));
    return std::max(glm::length(corner), 1e-4f);
}

// ---- which rungs this object's records could be on (see visibility.hpp) -------------------------

namespace {

// The ladder of shaders/cull.wgsl cs_cull_classify, evaluated with the thresholds already scaled:
// the largest level whose every threshold up to it is taken. `metric` is the projected radius when
// `lod.lodByScreenSize` and the distance otherwise, and `scale` multiplies each threshold.
int ladderLevel(const scene::LodSettings& lod, float metric, float scale) {
    const int lodCount = std::clamp(lod.lodCount, 1, scene::kMaxLodLevels);
    int level = 0;
    for (int k = 0; k + 1 < lodCount && k < 3; ++k) {
        const float threshold = lod.lodDistances[k];
        if (!(threshold > 0.0f)) {
            break; // a zero threshold ends the ladder, exactly as the shader's `break` does
        }
        const float t = threshold * scale;
        if (!(lod.lodByScreenSize ? metric <= t : metric >= t)) {
            break;
        }
        level = k + 1;
    }
    return level;
}

} // namespace

LevelRange objectLevelRange(const scene::LodSettings& lod, const CullCamera& camera,
                            const glm::mat4& objectToWorld, const InstanceBounds& bounds,
                            float sourceRadius, float detailMin, float detailMax, bool hysteresisActive) {
    LevelRange range;
    const int lodCount = std::clamp(lod.lodCount, 1, scene::kMaxLodLevels);
    range.highest = lodCount - 1;
    if (!bounds.valid || lodCount <= 1) {
        range.highest = std::max(range.highest, 0);
        return range;
    }
    float objectScale = 0.0f;
    for (int c = 0; c < 3; ++c) {
        objectScale = std::max(objectScale, glm::length(glm::vec3(objectToWorld[c])));
    }
    objectScale = std::max(objectScale, 1e-6f);
    const float radiusMax = sourceRadius * bounds.maxAbsScale * objectScale;
    const float radiusMin = sourceRadius * std::min(bounds.minAbsScale, bounds.maxAbsScale) * objectScale;

    // The world AABB of every record centre, and the nearest and furthest any of them can be.
    glm::vec3 lo(std::numeric_limits<float>::max());
    glm::vec3 hi(std::numeric_limits<float>::lowest());
    for (int corner = 0; corner < 8; ++corner) {
        const glm::vec3 p((corner & 1) ? bounds.max.x : bounds.min.x, (corner & 2) ? bounds.max.y : bounds.min.y,
                          (corner & 4) ? bounds.max.z : bounds.min.z);
        const glm::vec3 world(objectToWorld * glm::vec4(p, 1.0f));
        lo = glm::min(lo, world);
        hi = glm::max(hi, world);
    }
    if (!std::isfinite(lo.x) || !std::isfinite(hi.x)) {
        return range;
    }
    const glm::vec3 nearest = glm::clamp(camera.position, lo, hi);
    const float nearDistance = std::max(glm::length(nearest - camera.position), 1e-4f);
    float farDistance = 0.0f;
    for (int corner = 0; corner < 8; ++corner) {
        const glm::vec3 p((corner & 1) ? hi.x : lo.x, (corner & 2) ? hi.y : lo.y, (corner & 4) ? hi.z : lo.z);
        farDistance = std::max(farDistance, glm::length(p - camera.position));
    }
    farDistance = std::max(farDistance, nearDistance);

    // Everything that can widen one instance's threshold, as a multiplier on it. ADR-082's spread
    // offsets each instance's threshold by up to half `lodSpread`; its dead zone moves the bar by
    // `lodHysteresis` in whichever direction keeps the instance where it was. ADR-038's `detail`
    // divides a screen-size threshold and multiplies a distance one, so the extremes swap between
    // the two modes.
    const float spread = 0.5f * std::clamp(lod.lodSpread, 0.0f, 0.5f);
    const float hysteresis = hysteresisActive ? std::clamp(lod.lodHysteresis, 0.0f, 0.5f) : 0.0f;
    const float detailLo = std::max(std::min(detailMin, detailMax), 1e-3f);
    const float detailHi = std::max(std::max(detailMin, detailMax), 1e-3f);
    const float wide = (1.0f + spread) * (1.0f + hysteresis);
    const float narrow = (1.0f - spread) * (1.0f - hysteresis);
    const float scaleHi = lod.lodByScreenSize ? wide / detailLo : wide * detailHi;
    const float scaleLo = lod.lodByScreenSize ? narrow / detailHi : narrow * detailLo;

    if (lod.lodByScreenSize) {
        // Bigger on screen means a lower rung, so the lowest rung is the biggest an instance can
        // look against the tightest thresholds, and the highest rung the smallest against the
        // loosest.
        const float screenMax = radiusMax / nearDistance * camera.projScale;
        const float screenMin = radiusMin / farDistance * camera.projScale;
        range.lowest = ladderLevel(lod, screenMax, scaleLo);
        range.highest = ladderLevel(lod, screenMin, scaleHi);
    } else {
        range.lowest = ladderLevel(lod, nearDistance, scaleHi);
        range.highest = ladderLevel(lod, farDistance, scaleLo);
    }
    if (range.highest < range.lowest) {
        std::swap(range.lowest, range.highest);
    }
    range.lowest = std::clamp(range.lowest, 0, lodCount - 1);
    range.highest = std::clamp(range.highest, range.lowest, lodCount - 1);
    return range;
}

// ---- reason codes -------------------------------------------------------------------------------

std::string_view visibilityReasonName(VisibilityReason reason) {
    switch (reason) {
    case VisibilityReason::Visible: return "VISIBLE";
    case VisibilityReason::FrustumCulled: return "FRUSTUM_CULLED";
    case VisibilityReason::DistanceCulled: return "DISTANCE_CULLED";
    case VisibilityReason::ScreenSizeCulled: return "SCREEN_SIZE_CULLED";
    case VisibilityReason::DepthBandThinned: return "DEPTH_BAND_THINNED";
    case VisibilityReason::ObjectFullyCulled: return "OBJECT_FULLY_CULLED";
    case VisibilityReason::ObjectNotDrawable: return "OBJECT_NOT_DRAWABLE";
    case VisibilityReason::ObjectBudgetExceeded: return "OBJECT_BUDGET_EXCEEDED";
    case VisibilityReason::ShadowOnlyRejected: return "SHADOW_ONLY_REJECTED";
    case VisibilityReason::InvalidBounds: return "INVALID_BOUNDS";
    }
    return "VISIBLE";
}

InstanceVisibility instanceVisibility(const scene::LodSettings& lod, const FrustumPlanes& planes,
                                      const CullCamera& camera, glm::vec3 center, float radius,
                                      float lodRadius) {
    InstanceVisibility out;
    out.center = center;
    out.radius = radius;
    if (!std::isfinite(radius) || !std::isfinite(center.x) || !std::isfinite(center.y) ||
        !std::isfinite(center.z) || radius <= 0.0f) {
        out.reason = VisibilityReason::InvalidBounds;
        return out;
    }
    out.distance = glm::length(center - camera.position);
    out.screenRadius = radius / std::max(out.distance, 1e-4f) * camera.projScale;
    for (std::size_t p = 0; p < planes.size(); ++p) {
        out.planeMargins[p] = glm::dot(glm::vec3(planes[p]), center) + planes[p].w + radius;
    }

    // The verdict. Not recomputed: this is the function the renderer's own parameters go through,
    // and the one the GPU is pinned against.
    out.lodLevel = cullLodLevel(lod, planes, camera, center, radius, lodRadius);
    if (out.lodLevel >= 0) {
        out.reason = VisibilityReason::Visible;
        return out;
    }

    // Attribution, in the shader's own order, so a sphere that fails two tests is reported against
    // the one that actually rejected it first.
    for (const float margin : out.planeMargins) {
        if (margin < 0.0f) {
            out.reason = VisibilityReason::FrustumCulled;
            return out;
        }
    }
    if (lod.maxDistance > 0.0f && out.distance - radius > lod.maxDistance) {
        out.reason = VisibilityReason::DistanceCulled;
        return out;
    }
    if (lod.minScreenRadius > 0.0f && out.screenRadius < lod.minScreenRadius) {
        out.reason = VisibilityReason::ScreenSizeCulled;
        return out;
    }
    // Unreachable while `cullLodLevel` rejects for exactly those three reasons. If it ever grows a
    // fourth, this is where the lab finds out rather than quietly reporting the wrong stage.
    assert(false && "cullLodLevel rejected an instance for a reason instanceVisibility cannot name");
    out.reason = VisibilityReason::InvalidBounds;
    return out;
}

VisibilityReason proceduralVisibility(const scene::ProceduralGeometry& object, const FrustumPlanes& planes,
                                      const CullCamera& camera, const glm::mat4& objectToWorld,
                                      const InstanceBounds& bounds, float sourceRadius, bool limitDistance,
                                      bool shadowPass, bool slotAvailable) {
    // The order is ProceduralRenderer::update's own: the visible/empty test, then the slot budget,
    // then the whole-object rejection, and the shadow gate last because it is applied at the draw.
    if (!object.visible || object.instances.empty() || object.meshHash == 0) {
        return VisibilityReason::ObjectNotDrawable;
    }
    if (!slotAvailable) {
        return VisibilityReason::ObjectBudgetExceeded;
    }
    if (!bounds.valid) {
        return VisibilityReason::InvalidBounds;
    }
    if (objectFullyCulled(object.lod, planes, camera, objectToWorld, bounds, sourceRadius, limitDistance)) {
        return VisibilityReason::ObjectFullyCulled;
    }
    if (shadowPass && !object.castsShadow) {
        return VisibilityReason::ShadowOnlyRejected;
    }
    return VisibilityReason::Visible;
}

} // namespace avgen::rendering
