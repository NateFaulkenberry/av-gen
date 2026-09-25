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

// ---- ADR-713 (§16): the flow controls, transliterated from `shaders/fog.wgsl` ---------------------
//
// Same lanes, same expressions, same order. Each is the identity at its default by a branch, so a
// bank that does not use them evaluates what it evaluated before ADR-713.

// SWIRL: where in the structure's rest frame (before the drift) the world point `p` is. Rigid, at
// lane 1.z radians a second, about the bank's own vertical axis.
glm::vec3 fogStructureFrame(const MediumSlot& m, const glm::vec3& p, float t) {
    const float omega = m.lane[1].z;
    if (omega == 0.0f) {
        return p;
    }
    const float a = -omega * t;
    const float c = std::cos(a);
    const float s = std::sin(a);
    const glm::vec3 centre(m.lane[0]);
    const glm::vec3 rel = p - centre;
    return centre + glm::vec3(rel.x * c - rel.z * s, rel.y, rel.x * s + rel.z * c);
}

// SWELL: `1 + amount * sin(rate * t)`, the factor the horizontal offset is divided by.
float fogSwell(const MediumSlot& m, float t) {
    const float amount = std::clamp(m.lane[3].x, 0.0f, 0.9f);
    if (amount <= 0.0f) {
        return 1.0f;
    }
    return 1.0f + amount * std::sin(t * m.lane[3].y);
}

glm::vec3 fogSwellOffset(const MediumSlot& m, const glm::vec3& rel, float swell) {
    if (swell == 1.0f) {
        return rel;
    }
    if (fogShapeKindOf(m) == FogShape::Sphere) {
        return rel / swell;
    }
    return glm::vec3(rel.x / swell, rel.y, rel.z / swell);
}

glm::vec3 fogSemiAxes(const MediumSlot& m) {
    const float radius = std::max(m.lane[0].w, 1e-3f);
    if (fogShapeKindOf(m) == FogShape::Sphere) {
        return glm::vec3(radius);
    }
    return glm::vec3(radius * std::max(m.lane[13].y, 0.05f), std::max(m.lane[13].x, 1e-3f), radius);
}

// TURBULENCE (and §16's curl): the world displacement, at most `amount` of each semi-axis.
glm::vec3 fogTurbulence(const MediumSlot& m, const glm::vec3& p, float t) {
    const float amount = std::clamp(m.lane[7].y, 0.0f, 1.0f);
    if (amount <= 0.0f) {
        return glm::vec3(0.0f);
    }
    const float scale = std::max(m.lane[7].w, 0.05f);
    const float rate = std::max(m.lane[6].w, 0.0f);
    const glm::vec3 rest = fogStructureFrame(m, p, t) - glm::vec3(m.lane[2]) * t - glm::vec3(m.lane[0]);
    const float c = m.lane[13].z;
    const float s = m.lane[13].w;
    const glm::vec3 semi = fogSemiAxes(m);
    const glm::vec3 local((rest.x * c + rest.z * s) / semi.x, rest.y / semi.y,
                          (-rest.x * s + rest.z * c) / semi.z);
    glm::vec3 n = noise::flowCurl(local * scale, t * rate, 53u) * kFogTurbulenceGain;
    const float len = glm::length(n);
    if (len > 1.0f) {
        n = n / len;
    }
    const glm::vec3 d = n * amount * semi;
    return glm::vec3(d.x * c - d.z * s, d.y, d.x * s + d.z * c);
}

float fogTurbulenceReach(const MediumSlot& m) {
    if (fogShapeKindOf(m) == FogShape::Capsule) {
        return std::max(m.lane[13].y, 1.0f);
    }
    return 1.0f;
}

// ---- ADR-714 (§25): height and distance colour, luminance-preserving -------------------------

float fogLuminance(const glm::vec3& c) {
    return c.r * 0.2126f + c.g * 0.7152f + c.b * 0.0722f;
}

glm::vec3 fogHueMix(const glm::vec3& base, const glm::vec3& tint, float w) {
    const float lt = fogLuminance(tint);
    if (w <= 0.0f || lt <= 1e-4f) {
        return base;
    }
    const glm::vec3 target = tint * (fogLuminance(base) / lt);
    return base + (target - base) * w;
}

float fogHeightColourWeight(const MediumSlot& m, float relY) {
    const float amount = std::clamp(m.lane[5].w, 0.0f, 1.0f);
    if (amount <= 0.0f) {
        return 0.0f;
    }
    const float thickness = std::max(m.lane[13].x, 1e-3f);
    const float bias = std::clamp(m.lane[14].y, 0.0f, 1.0f);
    const float base = -thickness + 2.0f * thickness * bias;
    return amount * smoothstepf(0.0f, 2.0f, (relY - base) / thickness);
}

float fogDistanceColourWeight(const MediumSlot& m, float cameraDistance) {
    const float amount = std::clamp(m.lane[8].w, 0.0f, 1.0f);
    if (amount <= 0.0f) {
        return 0.0f;
    }
    return amount * (1.0f - std::exp(-std::max(cameraDistance, 0.0f) / std::max(m.lane[2].w, 1.0f)));
}

glm::vec3 fogTintedColour(const MediumSlot& m, const glm::vec3& base, float relY, float cameraDistance) {
    const glm::vec3 height(m.lane[9].w, m.lane[10].w, m.lane[11].w);
    const glm::vec3 c = fogHueMix(base, height, fogHeightColourWeight(m, relY));
    return fogHueMix(c, glm::vec3(m.lane[8]), fogDistanceColourWeight(m, cameraDistance));
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
    // ADR-713: through the swirl, which is `p` itself at swirl 0.
    const float n = noise::fbm3((fogStructureFrame(m, p, t) - velocity * t) * scale, 41u);
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

// §26's height influence on emission -- the transliteration of `fogEmissionHeight` in
// `shaders/fog.wgsl`. Reuses the density's vertical profile, so one vertical model serves both.
float fogEmissionHeight(const MediumSlot& m, float relY) {
    const float amount = std::clamp(m.lane[12].w, 0.0f, 1.0f);
    if (amount <= 0.0f) {
        return 1.0f;
    }
    return 1.0f + (fogVerticalProfile(m, relY) - 1.0f) * amount;
}

float fogShapeAt(const MediumSlot& m, const glm::vec3& p, float t) {
    if (m.lane[0].w <= 0.0f) {
        return 0.0f;
    }
    // ADR-713: swell and turbulence -- the default on its own branch, as `shaders/fog.wgsl` has it
    // (there it is load-bearing for byte identity; here it keeps the twin the same shape).
    const float swell = fogSwell(m, t);
    const float turbulence = std::clamp(m.lane[7].y, 0.0f, 1.0f);
    if (swell == 1.0f && turbulence <= 0.0f) {
        return fogShapeFrom(m, p, p - glm::vec3(m.lane[0]), t);
    }
    glm::vec3 q = p;
    if (turbulence > 0.0f) {
        const float reach0 = fogPrimitiveDistance(m, fogSwellOffset(m, p - glm::vec3(m.lane[0]), swell));
        if (reach0 > 1.35f + turbulence * fogTurbulenceReach(m) / std::min(swell, 1.0f)) {
            return 0.0f;
        }
        q = p + fogTurbulence(m, p, t);
    }
    return fogShapeFrom(m, q, fogSwellOffset(m, q - glm::vec3(m.lane[0]), swell), t);
}

float fogShapeFrom(const MediumSlot& m, const glm::vec3& q, const glm::vec3& rel, float t) {
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
    return fogDensityRemap(m, std::max(rim * profile * fogMacroDetail(m, q, t), 0.0f));
}

} // namespace avgen::world
