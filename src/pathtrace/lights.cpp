#include "pathtrace/lights.hpp"

#include "pathtrace/sampler.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace avgen::pathtrace {
namespace {

constexpr float kEps = 1e-9f;

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

bool lightOutOfRange(const scene::PunctualLight& light, const glm::vec3& p) {
    using T = scene::PunctualLight::Type;
    if ((light.type != T::Point && light.type != T::Spot) || !(light.range > 0.0f)) return false;
    // `sampleLight` measures the same vector, and `rangeWindow` is zero exactly when dist2 / range^2 >= 1, which a
    // dist2 >= range * range implies (the division is monotone and range^2 / range^2 is exactly 1).
    const glm::vec3 delta = light.position - p;
    return glm::dot(delta, delta) >= light.range * light.range;
}

glm::vec3 lightEmission(const scene::PunctualLight& l) {
    return l.color * scene::colorTemperatureToRgb(l.temperature, l.tint) * l.intensity;
}

LightSample sampleLight(const scene::PunctualLight& light, const glm::vec3& emit, const glm::vec3& p, glm::vec2 u) {
    LightSample s;
    if (!light.enabled) return s;

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

namespace {

// A group's triangle `prim` in world space, and its area in the space its group's CDF was built in.
struct EmitterTriangleGeometry {
    glm::vec3 v0{0.0f};
    glm::vec3 v1{0.0f};
    glm::vec3 v2{0.0f};
    float worldArea = 0.0f;
    float cdfArea = 0.0f; // world area for a mesh, object-space area for an instanced source
};

[[nodiscard]] EmitterTriangleGeometry emitterTriangle(const Snapshot& snap, bool instanced, std::uint32_t object,
                                                      std::uint32_t instance, std::uint32_t prim) {
    EmitterTriangleGeometry g;
    const TriangleMesh& mesh = instanced ? snap.instanced[object].source : snap.meshes[object];
    const std::size_t t = static_cast<std::size_t>(prim) * 3;
    if (t + 2 >= mesh.indices.size()) return g;
    const glm::vec3 a = mesh.positions[mesh.indices[t + 0]];
    const glm::vec3 b = mesh.positions[mesh.indices[t + 1]];
    const glm::vec3 c = mesh.positions[mesh.indices[t + 2]];
    g.cdfArea = 0.5f * glm::length(glm::cross(b - a, c - a));
    if (instanced) {
        const glm::mat4& xf = snap.instanced[object].transforms[instance];
        g.v0 = glm::vec3(xf * glm::vec4(a, 1.0f));
        g.v1 = glm::vec3(xf * glm::vec4(b, 1.0f));
        g.v2 = glm::vec3(xf * glm::vec4(c, 1.0f));
        g.worldArea = 0.5f * glm::length(glm::cross(g.v1 - g.v0, g.v2 - g.v0));
    } else {
        g.v0 = a;
        g.v1 = b;
        g.v2 = c;
        g.worldArea = g.cdfArea;
    }
    return g;
}

// The area density of a point on triangle `tri` of group `g` (ADR-2247).
[[nodiscard]] float emitterAreaPdf(const Snapshot& snap, const EmitterGroup& g, const EmitterTriangleGeometry& tri) {
    if (!(tri.worldArea > 0.0f) || !(g.area > 0.0f) || !(snap.totalEmitterWeight > 0.0f)) return 0.0f;
    const float pGroup = g.weight / snap.totalEmitterWeight;
    return pGroup * (tri.cdfArea / g.area) / tri.worldArea;
}

[[nodiscard]] const EmitterGroup* groupOf(const Snapshot& snap, const ShadingPoint& hit) {
    std::int32_t index = -1;
    if (hit.instanced) {
        if (hit.object < snap.instancedEmitterGroup.size() && snap.instancedEmitterGroup[hit.object] >= 0) {
            index = snap.instancedEmitterGroup[hit.object] + static_cast<std::int32_t>(hit.instance);
        }
    } else if (hit.object < snap.meshEmitterGroup.size()) {
        index = snap.meshEmitterGroup[hit.object];
    }
    if (index < 0 || static_cast<std::size_t>(index) >= snap.emitterGroups.size()) return nullptr;
    const EmitterGroup& g = snap.emitterGroups[static_cast<std::size_t>(index)];
    if (g.instanced != hit.instanced || g.object != hit.object || g.instance != (hit.instanced ? hit.instance : 0u)) {
        return nullptr; // a table that does not describe this hit: say it cannot be found rather than guess
    }
    return &g;
}

} // namespace

EmissiveSample sampleEmissive(const Snapshot& snap, const glm::vec3& p, glm::vec2 u, float pick) {
    EmissiveSample s;
    if (!snap.hasEmitters() || !(snap.totalEmitterWeight > 0.0f)) return s;

    // The group, then -- with what is left of `pick` inside the group's slice -- the triangle.
    const float v = std::clamp(pick, 0.0f, 1.0f);
    const auto git = std::lower_bound(snap.emitterGroups.begin(), snap.emitterGroups.end(), v,
                                      [](const EmitterGroup& g, float x) { return g.cdf < x; });
    const auto gi = static_cast<std::size_t>(
        std::min<std::ptrdiff_t>(git - snap.emitterGroups.begin(),
                                 static_cast<std::ptrdiff_t>(snap.emitterGroups.size()) - 1));
    const EmitterGroup& g = snap.emitterGroups[gi];
    const float lo = gi == 0 ? 0.0f : snap.emitterGroups[gi - 1].cdf;
    const float within = g.cdf > lo ? std::clamp((v - lo) / (g.cdf - lo), 0.0f, 1.0f) : 0.5f;
    const auto tbegin = snap.emitterTriangles.begin() + g.triBegin;
    const auto tend = tbegin + g.triCount;
    auto tit = std::lower_bound(tbegin, tend, within, [](const EmitterTriangle& t, float x) { return t.cdf < x; });
    if (tit == tend) tit = tend - 1;

    const EmitterTriangleGeometry tri = emitterTriangle(snap, g.instanced, g.object, g.instance, tit->prim);
    if (!(tri.worldArea > 0.0f)) return s;

    // Uniform point on a triangle. The sqrt warp is what makes it uniform rather than bunched at v0.
    const float su = std::sqrt(std::clamp(u.x, 0.0f, 1.0f));
    const float b0 = 1.0f - su;
    const float b1 = u.y * su;
    const float b2 = 1.0f - b0 - b1;
    const glm::vec3 target = tri.v0 * b0 + tri.v1 * b1 + tri.v2 * b2;

    const glm::vec3 delta = target - p;
    const float dist2 = glm::dot(delta, delta);
    if (dist2 < 1e-12f) return s;
    const float dist = std::sqrt(dist2);
    const glm::vec3 dir = delta / dist;

    // The emitter radiates from the face the shading point is on. Two-sided, because a scene can
    // legitimately put an emissive shell around something and expect it to glow outward.
    glm::vec3 n = glm::normalize(glm::cross(tri.v1 - tri.v0, tri.v2 - tri.v0));
    const float cosEmitter = std::abs(glm::dot(n, -dir));
    if (cosEmitter <= kEps) return s;   // edge-on: zero projected area, infinite density
    if (glm::dot(n, -dir) < 0.0f) n = -n; // facing the receiver, as the raster's front-facing n faces its camera

    s.point.instanced = g.instanced;
    s.point.object = g.object;
    s.point.instance = g.instance;
    s.point.prim = tit->prim;
    s.point.b0 = b0;
    s.point.b1 = b1;
    s.point.b2 = b2;
    s.point.position = target;
    s.point.normal = n;
    s.radiance = shadeSurfaceMaterial(snap, s.point).emission;
    s.direction = dir;
    s.distance = dist;
    s.pdf = emitterAreaPdf(snap, g, tri) * dist2 / cosEmitter;
    s.valid = s.pdf > 0.0f && std::isfinite(s.pdf);
    return s;
}

float emissivePdf(const Snapshot& snap, const ShadingPoint& hit, const glm::vec3& direction, float distance) {
    if (!(distance > 0.0f) || !std::isfinite(distance)) return 0.0f;
    const EmitterGroup* g = groupOf(snap, hit);
    if (g == nullptr) return 0.0f; // not an emitter: only the BSDF strategy could have found it
    const EmitterTriangleGeometry tri = emitterTriangle(snap, hit.instanced, hit.object, hit.instance, hit.prim);
    if (!(tri.worldArea > 0.0f)) return 0.0f;
    const glm::vec3 n = glm::normalize(glm::cross(tri.v1 - tri.v0, tri.v2 - tri.v0));
    const float cosEmitter = std::abs(glm::dot(n, -direction));
    if (cosEmitter <= kEps) return 0.0f;
    return emitterAreaPdf(snap, *g, tri) * (distance * distance) / cosEmitter;
}

} // namespace avgen::pathtrace
