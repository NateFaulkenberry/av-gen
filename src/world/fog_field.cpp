// The fog field on the CPU (ADR-563): the transliteration of `shaders/fog.wgsl`.
//
// Built the way `core/vortex.cpp` ↔ `shaders/vortex.wgsl` is built, for the reason ADR-055 and
// ADR-388 both give: a field only one consumer can evaluate is a field nothing can test, and the
// vortex spent two ADRs proving that the expensive way. Every function here evaluates the same
// expressions in the same order as the shader, reading the SAME packed lanes -- so the two start
// from bytes that are identical by construction and a disagreement is a disagreement about the
// maths rather than about how a parameter was interpreted on the way in.
//
// It takes a `MediumSlot` rather than a struct of its own precisely so there is no second
// interpretation step to get wrong. ADR-401, ADR-561 and ADR-562 each found a hand-written copy of
// one conversion that had drifted; this one has nothing to copy.

#include "world/atmospherics.hpp"

#include <algorithm>
#include <cmath>

namespace avgen::world {
namespace {

float smoothstepf(float a, float b, float x) {
    if (a == b) {
        return x < a ? 0.0f : 1.0f;
    }
    const float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

} // namespace

float fogEllipticalRadius(const MediumSlot& m, const glm::vec3& rel) {
    const float c = m.lane[13].z;
    const float s = m.lane[13].w;
    const float x = rel.x * c + rel.z * s;
    const float z = -rel.x * s + rel.z * c;
    const float radius = std::max(m.lane[0].w, 1e-3f);
    const float along = std::max(m.lane[13].y, 0.05f);
    const float u = x / (radius * along);
    const float v = z / radius;
    return std::sqrt(u * u + v * v);
}

float fogVerticalProfile(const MediumSlot& m, float relY) {
    const float thickness = std::max(m.lane[13].x, 1e-3f);
    const float bias = std::clamp(m.lane[14].y, 0.0f, 1.0f);
    const float base = -thickness + 2.0f * thickness * bias;
    const float h = (relY - base) / thickness;
    if (h < 0.0f) {
        const float below = h * 3.0f;
        return std::exp(-below * below);
    }
    const float falloff = std::max(m.lane[14].z, 0.01f);
    const float thin = std::exp(-h * falloff);
    const float dome = std::clamp(1.0f - h * 0.5f, 0.0f, 1.0f);
    const float lid = dome * dome;
    const float blend = std::clamp(m.lane[14].w, 0.0f, 1.0f);
    return thin + (lid - thin) * blend;
}

float fogShapeAt(const MediumSlot& m, const glm::vec3& p) {
    if (m.lane[0].w <= 0.0f) {
        return 0.0f;
    }
    const glm::vec3 rel = p - glm::vec3(m.lane[0]);
    const float rr = fogEllipticalRadius(m, rel);
    if (rr > 1.35f) {
        return 0.0f;
    }
    const float soft = std::clamp(m.lane[14].x, 0.02f, 1.0f);
    const float rim = 1.0f - smoothstepf(1.0f - soft, 1.0f + soft * 0.35f, rr);
    if (rim <= 0.0f) {
        return 0.0f;
    }
    return rim * fogVerticalProfile(m, rel.y);
}

} // namespace avgen::world
