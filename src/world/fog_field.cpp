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

#include "core/noise.hpp"
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

// §13/§14's macro detail. Identity at zero and mean-preserving by construction -- the
// transliteration of `fogMacroDetail` in `shaders/fog.wgsl`, same expressions, same order.
float fogMacroDetail(const MediumSlot& m, const glm::vec3& p, float t) {
    const float amount = std::clamp(m.lane[7].z, 0.0f, 1.0f);
    if (amount <= 0.0f) {
        return 1.0f;
    }
    const float scale = std::max(m.lane[7].x, 0.05f) / std::max(m.lane[0].w, 1.0f);
    // ADR-571 (§15/§16): an ADVECTION. `detail(p + v*dt, t + dt) == detail(p, t)` exactly, which
    // is what makes the structure move through the world rather than regenerate in place.
    const glm::vec3 velocity(m.lane[2]);
    const float n = noise::fbm3((p - velocity * t) * scale, 41u);
    return 1.0f + amount * (n * 2.0f - 1.0f);
}

// ADR-566: the six primitives, transliterated from `fogPrimitiveDistance` in `shaders/fog.wgsl`.
// Same expressions, same order, same normalisation -- 1 at the surface, zero past 1.35.
FogShape fogShapeKindOf(const MediumSlot& m) {
    const int i = static_cast<int>(std::clamp(m.lane[12].y, 0.0f, 5.0f) + 0.5f);
    return static_cast<FogShape>(i);
}

float fogPrimitiveDistance(const MediumSlot& m, const glm::vec3& rel) {
    const float radius = std::max(m.lane[0].w, 1e-3f);
    const float along = std::max(m.lane[13].y, 0.05f);
    const float c = m.lane[13].z;
    const float s = m.lane[13].w;
    const float x = rel.x * c + rel.z * s;
    const float z = -rel.x * s + rel.z * c;
    const float y = rel.y;
    const float ax = radius * along;
    const float az = radius;
    const float ay = std::max(m.lane[13].x, 1e-3f);

    switch (fogShapeKindOf(m)) {
    case FogShape::Sphere:
        return glm::length(glm::vec3(x, y, z)) / radius;
    case FogShape::Ellipsoid:
        return glm::length(glm::vec3(x / ax, y / ay, z / az));
    case FogShape::Box:
        return std::max(std::max(std::abs(x) / ax, std::abs(y) / ay), std::abs(z) / az);
    case FogShape::Capsule: {
        const float half = std::max(ax - az, 0.0f);
        const float qx = x - std::clamp(x, -half, half);
        return glm::length(glm::vec3(qx, y * (az / ay), z)) / az;
    }
    case FogShape::Cylinder: {
        const float u = x / ax;
        const float v = z / az;
        return std::max(std::sqrt(u * u + v * v), std::abs(y) / ay);
    }
    case FogShape::Bank:
        break;
    }
    return fogEllipticalRadius(m, rel);
}

// §24's density response curve -- the transliteration of `fogDensityRemap` in `shaders/fog.wgsl`.
// Identity at (threshold 0, softness 0, contrast 1), which is what makes it opt-in.
float fogDensityRemap(const MediumSlot& m, float shape) {
    const float threshold = std::clamp(m.lane[6].y, 0.0f, 0.99f);
    float s = std::max(shape - threshold, 0.0f) / std::max(1.0f - threshold, 1e-4f);
    const float softness = std::clamp(m.lane[6].z, 0.0f, 1.0f);
    if (softness > 0.0f) {
        s = s + (smoothstepf(0.0f, 1.0f, s) - s) * softness;
    }
    const float contrast = std::max(m.lane[6].x, 0.05f);
    if (contrast != 1.0f) {
        s = std::pow(std::max(s, 0.0f), contrast);
    }
    return std::clamp(s, 0.0f, 1.0f);
}

float fogShapeAt(const MediumSlot& m, const glm::vec3& p, float t) {
    if (m.lane[0].w <= 0.0f) {
        return 0.0f;
    }
    const glm::vec3 rel = p - glm::vec3(m.lane[0]);
    const float rr = fogPrimitiveDistance(m, rel);
    if (rr > 1.35f) {
        return 0.0f;
    }
    const float soft = std::clamp(m.lane[14].x, 0.02f, 1.0f);
    const float rim = 1.0f - smoothstepf(1.0f - soft, 1.0f + soft * 0.35f, rr);
    if (rim <= 0.0f) {
        return 0.0f;
    }
    float profile = fogVerticalProfile(m, rel.y);
    if (fogShapeKindOf(m) != FogShape::Bank) {
        const float influence = std::clamp(m.lane[12].z, 0.0f, 1.0f);
        profile = 1.0f + (profile - 1.0f) * influence;
    }
    return fogDensityRemap(m, std::max(rim * profile * fogMacroDetail(m, p, t), 0.0f));
}

} // namespace avgen::world
