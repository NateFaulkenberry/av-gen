#include "pathtrace/lights.hpp"

#include "pathtrace/sampler.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace avgen::pathtrace {
namespace {

constexpr float kEps = 1e-9f;

[[nodiscard]] glm::vec3 emitterRadiance(const scene::PunctualLight& l) {
    return l.color * scene::colorTemperatureToRgb(l.temperature, l.tint) * l.intensity;
}

// Transcribed from shaders/lighting.wgsl so the two renderers agree about where a light stops.
[[nodiscard]] float rangeWindow(float dist2, float range) {
    if (range <= 0.0f) return 1.0f;
    const float ratio = dist2 / (range * range);
    const float w = std::clamp(1.0f - ratio * ratio, 0.0f, 1.0f);
    return w * w;
}

// The emitter's plane normal, for the kinds that have one.
[[nodiscard]] glm::vec3 emitterNormal(const scene::PunctualLight& l) {
    const float len = glm::length(l.direction);
    return len > kEps ? l.direction / len : glm::vec3(0.0f, -1.0f, 0.0f);
}

void rectBasis(const scene::PunctualLight& l, glm::vec3& n, glm::vec3& t, glm::vec3& b) {
    n = emitterNormal(l);
    glm::vec3 up = glm::length(l.up) > kEps ? glm::normalize(l.up) : glm::vec3(0.0f, 1.0f, 0.0f);
    t = glm::cross(n, up);
    if (glm::length(t) < 1e-6f) t = glm::cross(n, glm::vec3(1.0f, 0.0f, 0.0f));
    if (glm::length(t) < 1e-6f) t = glm::cross(n, glm::vec3(0.0f, 0.0f, 1.0f));
    t = glm::normalize(t);
    b = glm::normalize(glm::cross(t, n));
}

} // namespace

bool isDeltaLight(const scene::PunctualLight& l) {
    using T = scene::PunctualLight::Type;
    if (l.type == T::Directional) return true;
    if (l.type == T::Point || l.type == T::Spot) return true;
    // An area light with no area is a point in disguise, and sampling it as an area divides by zero.
    return scene::emitterArea(l) <= kEps;
}

float powerHeuristic(float pdfA, float pdfB) {
    if (pdfA <= 0.0f) return 0.0f;
    if (pdfB <= 0.0f) return 1.0f;
    // Form the ratio before squaring: (a^2)/(a^2+b^2) overflows to NaN for large a.
    const float r = pdfB / pdfA;
    return 1.0f / (1.0f + r * r);
}

LightSample sampleLight(const scene::PunctualLight& light, const glm::vec3& p, glm::vec2 u) {
    LightSample s;
    if (!light.enabled) return s;

    const glm::vec3 emit = emitterRadiance(light);
    if (emit.x <= 0.0f && emit.y <= 0.0f && emit.z <= 0.0f) return s;

    using T = scene::PunctualLight::Type;
    s.delta = isDeltaLight(light);

    if (light.type == T::Directional) {
        const float len = glm::length(light.direction);
        if (len < kEps) return s;
        s.direction = -light.direction / len;
        s.radiance = emit;   // illuminance; no falloff
        s.distance = std::numeric_limits<float>::infinity();
        s.pdf = 0.0f;        // delta
        s.valid = true;
        return s;
    }

    glm::vec3 target = light.position;
    float cosEmitter = 1.0f;
    float area = 0.0f;

    if (!s.delta) {
        if (light.type == T::Rect) {
            glm::vec3 n{};
            glm::vec3 t{};
            glm::vec3 b{};
            rectBasis(light, n, t, b);
            target = light.position + t * ((u.x - 0.5f) * light.width) + b * ((u.y - 0.5f) * light.height);
            const glm::vec3 toSurface = glm::normalize(p - target);
            cosEmitter = glm::dot(n, toSurface);
            if (cosEmitter <= 0.0f) return s;   // the shading point is behind the emitter
            area = light.width * light.height;
        } else {
            // Disk / Sphere / Tube: a disc of `radius` turned to face the shading point. Crude for a
            // sphere and honest about it -- proper solid-angle sphere sampling is a later refinement.
            const glm::vec3 toP = glm::normalize(p - light.position);
            glm::vec3 t{};
            glm::vec3 b{};
            orthonormalBasis(toP, t, b);
            const glm::vec2 d = sampleUniformDisc(u);
            target = light.position + (t * d.x + b * d.y) * light.radius;
            cosEmitter = 1.0f;
            area = scene::emitterArea(light);
        }
    }

    const glm::vec3 delta = target - p;
    const float dist2 = glm::dot(delta, delta);
    if (dist2 < 1e-12f) return s;
    const float dist = std::sqrt(dist2);
    s.direction = delta / dist;
    s.distance = dist;

    float attenuation = rangeWindow(dist2, light.range) / dist2;

    if (light.type == T::Spot) {
        const float len = glm::length(light.direction);
        if (len < kEps) return s;
        const glm::vec3 axis = light.direction / len;
        const float cosAngle = glm::dot(axis, -s.direction);
        const float cosOuter = std::cos(light.outerConeAngle);
        const float cosInner = std::cos(light.innerConeAngle);
        const float denom = std::max(1e-4f, cosInner - cosOuter);
        const float spot = std::clamp((cosAngle - cosOuter) / denom, 0.0f, 1.0f);
        if (spot <= 0.0f) return s;
        attenuation *= spot * spot;
    }

    if (s.delta) {
        s.radiance = emit * attenuation;
        s.pdf = 0.0f;
        s.valid = attenuation > 0.0f;
        return s;
    }

    // Area emitter. `intensity` is nits over the emitter, and `emitterArea` is the project's own
    // nits-to-intensity conversion, shared with rendering::lightInfluenceRadius.
    s.radiance = emit * attenuation * scene::emitterArea(light) * cosEmitter;

    // Uniform-area density converted to solid angle: p_A = 1/A, p_omega = p_A * dist^2 / cos.
    if (area > kEps && cosEmitter > kEps) {
        s.pdf = dist2 / (cosEmitter * area);
    } else {
        s.pdf = 0.0f;
    }
    s.valid = s.pdf > 0.0f;
    return s;
}

float lightPdf(const scene::PunctualLight& light, const glm::vec3& p, const glm::vec3& direction,
               float distance) {
    if (!light.enabled || isDeltaLight(light)) return 0.0f;
    if (!(distance > 0.0f) || !std::isfinite(distance)) return 0.0f;

    using T = scene::PunctualLight::Type;
    const float dist2 = distance * distance;

    if (light.type == T::Rect) {
        glm::vec3 n{};
        glm::vec3 t{};
        glm::vec3 b{};
        rectBasis(light, n, t, b);
        const float cosEmitter = glm::dot(n, -direction);
        if (cosEmitter <= kEps) return 0.0f;
        const float area = light.width * light.height;
        if (area <= kEps) return 0.0f;
        return dist2 / (cosEmitter * area);
    }

    const float area = scene::emitterArea(light);
    if (area <= kEps) return 0.0f;
    (void)p;
    return dist2 / area;   // the facing disc has cos = 1 by construction
}

// ---- emissive geometry -------------------------------------------------------------------------

EmissiveSample sampleEmissive(const Snapshot& snap, const glm::vec3& p, glm::vec2 u, float pick) {
    EmissiveSample s;
    if (snap.emissiveTriangles.empty() || snap.totalEmissiveArea <= kEps) return s;

    // Binary search the area CDF: uniform by area over the whole emissive surface.
    const auto it = std::lower_bound(snap.emissiveTriangles.begin(), snap.emissiveTriangles.end(),
                                     std::clamp(pick, 0.0f, 1.0f),
                                     [](const EmissiveTriangle& e, float v) { return e.cdf < v; });
    const EmissiveTriangle& tri =
        it == snap.emissiveTriangles.end() ? snap.emissiveTriangles.back() : *it;

    // Uniform point on a triangle. The sqrt warp is what makes it uniform rather than bunched at v0.
    const float su = std::sqrt(std::clamp(u.x, 0.0f, 1.0f));
    const float b0 = 1.0f - su;
    const float b1 = u.y * su;
    const glm::vec3 target = tri.v0 * b0 + tri.v1 * b1 + tri.v2 * (1.0f - b0 - b1);

    const glm::vec3 delta = target - p;
    const float dist2 = glm::dot(delta, delta);
    if (dist2 < 1e-12f) return s;
    const float dist = std::sqrt(dist2);
    const glm::vec3 dir = delta / dist;

    // The emitter radiates from the face the shading point is on. Two-sided, because a scene can
    // legitimately put an emissive shell around something and expect it to glow outward.
    const float cosEmitter = std::abs(glm::dot(tri.normal, -dir));
    if (cosEmitter <= kEps) return s;   // edge-on: zero projected area, infinite density

    s.direction = dir;
    s.distance = dist;
    s.radiance = tri.emission;
    s.meshIndex = tri.meshIndex;
    s.primIndex = tri.primIndex;
    // p_A = 1 / totalArea  ->  p_omega = p_A * dist^2 / cos
    s.pdf = dist2 / (cosEmitter * snap.totalEmissiveArea);
    s.valid = s.pdf > 0.0f && std::isfinite(s.pdf);
    return s;
}

float emissivePdf(const Snapshot& snap, std::uint32_t meshIndex, std::uint32_t primIndex,
                  const glm::vec3& direction, float distance) {
    if (snap.emissiveTriangles.empty() || snap.totalEmissiveArea <= kEps) return 0.0f;
    if (!(distance > 0.0f) || !std::isfinite(distance)) return 0.0f;

    // Find the triangle. Linear for now: this runs only when a BSDF ray actually lands on an
    // emitter, which is rare, and a map would cost memory on every scene to save time on few.
    for (const auto& e : snap.emissiveTriangles) {
        if (e.meshIndex != meshIndex || e.primIndex != primIndex) continue;
        const float cosEmitter = std::abs(glm::dot(e.normal, -direction));
        if (cosEmitter <= kEps) return 0.0f;
        return (distance * distance) / (cosEmitter * snap.totalEmissiveArea);
    }
    return 0.0f;   // not an emitter: the BSDF strategy is the only one that could have found it
}

} // namespace avgen::pathtrace
