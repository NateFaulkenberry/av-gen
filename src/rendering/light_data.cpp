#include "rendering/light_data.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace avgen::rendering {

namespace {

constexpr float kPi = std::numbers::pi_v<float>;

glm::vec3 safeNormalize(const glm::vec3& v, const glm::vec3& fallback) {
    const float len2 = glm::dot(v, v);
    return len2 > 1e-12f ? v * (1.0f / std::sqrt(len2)) : fallback;
}

// An orthonormal (right, up) pair around `forward`, seeded by the light's authored up axis so a
// rect emitter keeps the orientation the artist gave it.
void emitterBasis(const glm::vec3& forward, const glm::vec3& upHint, glm::vec3& right, glm::vec3& up) {
    glm::vec3 u = safeNormalize(upHint, glm::vec3(0.0f, 1.0f, 0.0f));
    if (std::abs(glm::dot(u, forward)) > 0.999f) {
        u = std::abs(forward.y) > 0.9f ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
    }
    right = safeNormalize(glm::cross(forward, u), glm::vec3(1.0f, 0.0f, 0.0f));
    up = glm::cross(right, forward);
}

bool isAreaType(scene::PunctualLight::Type type) {
    return type == scene::PunctualLight::Type::Rect || type == scene::PunctualLight::Type::Disk ||
           type == scene::PunctualLight::Type::Tube || type == scene::PunctualLight::Type::Sphere;
}

} // namespace

float lightInfluenceRadius(const scene::PunctualLight& light, float cutoff) {
    if (light.type == scene::PunctualLight::Type::Directional) {
        return 0.0f; // infinite; directional lights never enter the cluster grid
    }
    if (light.range > 0.0f) {
        return light.range;
    }
    const glm::vec3 c = light.color * std::max(light.intensity, 0.0f);
    const float peak = std::max({c.r, c.g, c.b, 0.0f});
    if (peak <= 0.0f) {
        return 0.0f;
    }
    // Inverse-square falloff: peak / d^2 = cutoff.
    const float radius = std::sqrt(peak / std::max(cutoff, 1e-6f));
    // An emitter with size still reaches at least its own extent.
    const float extent = std::max({light.width, light.height, light.radius}) * 0.5f;
    return std::clamp(radius + extent, 0.01f, 10000.0f);
}

GpuLight packLight(const scene::PunctualLight& light, int shadowView, bool cascaded) {
    GpuLight g{};
    const glm::vec3 dir = safeNormalize(light.direction, glm::vec3(0.0f, -1.0f, 0.0f));
    glm::vec3 right;
    glm::vec3 up;
    emitterBasis(dir, light.up, right, up);

    g.positionType = glm::vec4(light.position, static_cast<float>(light.type));
    g.directionRange = glm::vec4(dir, std::max(light.range, 0.0f));

    const glm::vec3 tint = scene::colorTemperatureToRgb(light.temperature, light.tint);
    g.colorIntensity = glm::vec4(light.color * tint * std::max(light.intensity, 0.0f),
                                 lightInfluenceRadius(light));

    const float cosOuter = std::cos(std::clamp(light.outerConeAngle, 0.0f, kPi * 0.5f));
    const float cosInner = std::cos(std::clamp(light.innerConeAngle, 0.0f, kPi * 0.5f));
    std::uint32_t flags = 0;
    if (light.castsShadow && shadowView >= 0) {
        flags |= kLightFlagCastsShadow;
    }
    if (cascaded) {
        flags |= kLightFlagCascaded;
    }
    if (isAreaType(light.type)) {
        flags |= kLightFlagArea;
    }
    g.cone = glm::vec4(cosOuter, 1.0f / std::max(cosInner - cosOuter, 1e-4f), static_cast<float>(shadowView),
                       static_cast<float>(flags));
    g.sizeSoft = glm::vec4(std::max(light.width, 0.0f), std::max(light.height, 0.0f),
                           std::max(light.radius, 0.0f), std::max(light.softness, 0.0f));
    g.up = glm::vec4(up, std::clamp(light.shadowStrength, 0.0f, 1.0f));
    g.tangent = glm::vec4(right, light.contactShadow ? 1.0f : 0.0f);
    g.extra = glm::vec4(light.diffuseOnly ? 1.0f : 0.0f, light.specularOnly ? 1.0f : 0.0f,
                        std::max(light.volumetricStrength, 0.0f), std::max(light.shadowBias, 0.0f));
    return g;
}

std::uint32_t orderLightsForShading(const std::vector<scene::PunctualLight>& lights,
                                    std::vector<const scene::PunctualLight*>& out) {
    out.clear();
    out.reserve(lights.size());
    for (const scene::PunctualLight& l : lights) {
        if (l.enabled && l.type == scene::PunctualLight::Type::Directional) {
            out.push_back(&l);
        }
    }
    const auto directional = static_cast<std::uint32_t>(out.size());
    for (const scene::PunctualLight& l : lights) {
        if (l.enabled && l.type != scene::PunctualLight::Type::Directional) {
            out.push_back(&l);
        }
    }
    if (out.size() > kMaxSceneLights) {
        out.resize(kMaxSceneLights);
    }
    return std::min<std::uint32_t>(directional, static_cast<std::uint32_t>(out.size()));
}

// ---- clusters ----------------------------------------------------------------------------------

float ClusterGrid::sliceScale() const {
    const float ratio = std::max(zFar / std::max(zNear, 1e-4f), 1.0001f);
    return static_cast<float>(z) / std::log2(ratio);
}

float ClusterGrid::sliceBias() const {
    const float ratio = std::max(zFar / std::max(zNear, 1e-4f), 1.0001f);
    return -(static_cast<float>(z) * std::log2(std::max(zNear, 1e-4f)) / std::log2(ratio));
}

float ClusterGrid::sliceNear(std::uint32_t k) const {
    const float n = std::max(zNear, 1e-4f);
    const float ratio = std::max(zFar / n, 1.0001f);
    return n * std::pow(ratio, static_cast<float>(k) / static_cast<float>(z));
}

std::uint32_t ClusterGrid::sliceOf(float viewDepth) const {
    const float d = std::max(viewDepth, std::max(zNear, 1e-4f));
    const auto slice = static_cast<int>(std::floor(std::log2(d) * sliceScale() + sliceBias()));
    return static_cast<std::uint32_t>(std::clamp(slice, 0, static_cast<int>(z) - 1));
}

void ClusterGrid::bounds(std::uint32_t i, std::uint32_t j, std::uint32_t k, glm::vec3& min,
                         glm::vec3& max) const {
    // Screen tile in NDC (y up).
    const float x0 = 2.0f * static_cast<float>(i) / static_cast<float>(x) - 1.0f;
    const float x1 = 2.0f * static_cast<float>(i + 1) / static_cast<float>(x) - 1.0f;
    const float y0 = 2.0f * static_cast<float>(j) / static_cast<float>(y) - 1.0f;
    const float y1 = 2.0f * static_cast<float>(j + 1) / static_cast<float>(y) - 1.0f;
    const float d0 = sliceNear(k);
    const float d1 = sliceNear(k + 1);
    const float tx = tanHalfFovY * aspect;
    const float ty = tanHalfFovY;
    // The camera looks down -Z: a point at view depth d sits at z = -d.
    const float xs[4] = {x0 * tx * d0, x1 * tx * d0, x0 * tx * d1, x1 * tx * d1};
    const float ys[4] = {y0 * ty * d0, y1 * ty * d0, y0 * ty * d1, y1 * ty * d1};
    min = glm::vec3(*std::min_element(xs, xs + 4), *std::min_element(ys, ys + 4), -d1);
    max = glm::vec3(*std::max_element(xs, xs + 4), *std::max_element(ys, ys + 4), -d0);
}

bool clusterTouchesSphere(const ClusterGrid& grid, std::uint32_t i, std::uint32_t j, std::uint32_t k,
                          const glm::vec3& viewPosition, float radius) {
    glm::vec3 lo;
    glm::vec3 hi;
    grid.bounds(i, j, k, lo, hi);
    const glm::vec3 closest = glm::clamp(viewPosition, lo, hi);
    const glm::vec3 delta = viewPosition - closest;
    return glm::dot(delta, delta) <= radius * radius;
}

std::vector<std::vector<std::uint32_t>> assignClusters(const ClusterGrid& grid,
                                                       const std::vector<glm::vec3>& viewPositions,
                                                       const std::vector<float>& radii) {
    std::vector<std::vector<std::uint32_t>> out(grid.count());
    const std::size_t n = std::min(viewPositions.size(), radii.size());
    for (std::uint32_t k = 0; k < grid.z; ++k) {
        for (std::uint32_t j = 0; j < grid.y; ++j) {
            for (std::uint32_t i = 0; i < grid.x; ++i) {
                std::vector<std::uint32_t>& list = out[grid.indexOf(i, j, k)];
                for (std::uint32_t n2 = 0; n2 < n; ++n2) {
                    if (list.size() >= kMaxLightsPerCluster) {
                        break;
                    }
                    if (radii[n2] > 0.0f && clusterTouchesSphere(grid, i, j, k, viewPositions[n2], radii[n2])) {
                        list.push_back(n2);
                    }
                }
            }
        }
    }
    return out;
}

float polygonIrradiance(const glm::vec3& point, const glm::vec3& normal, const glm::vec3& p0,
                        const glm::vec3& p1, const glm::vec3& p2, const glm::vec3& p3) {
    const glm::vec3 n = safeNormalize(normal, glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::vec3 l0 = p0 - point;
    const glm::vec3 l1 = p1 - point;
    const glm::vec3 l2 = p2 - point;
    const glm::vec3 l3 = p3 - point;
    if (glm::dot(l0, n) < 0.0f && glm::dot(l1, n) < 0.0f && glm::dot(l2, n) < 0.0f && glm::dot(l3, n) < 0.0f) {
        return 0.0f;
    }
    const glm::vec3 v0 = safeNormalize(l0, glm::vec3(0.0f, 0.0f, 1.0f));
    const glm::vec3 v1 = safeNormalize(l1, glm::vec3(0.0f, 0.0f, 1.0f));
    const glm::vec3 v2 = safeNormalize(l2, glm::vec3(0.0f, 0.0f, 1.0f));
    const glm::vec3 v3 = safeNormalize(l3, glm::vec3(0.0f, 0.0f, 1.0f));
    const auto edge = [&n](const glm::vec3& a, const glm::vec3& b) {
        const float cosTheta = std::clamp(glm::dot(a, b), -0.9999f, 0.9999f);
        const float theta = std::acos(cosTheta);
        return glm::dot(glm::cross(a, b), n) * (theta / std::max(std::sin(theta), 1e-4f));
    };
    const float sum = edge(v0, v1) + edge(v1, v2) + edge(v2, v3) + edge(v3, v0);
    return std::max(sum / (2.0f * kPi), 0.0f);
}

void areaLightCorners(const scene::PunctualLight& light, glm::vec3 corners[4]) {
    const glm::vec3 dir = safeNormalize(light.direction, glm::vec3(0.0f, -1.0f, 0.0f));
    glm::vec3 right;
    glm::vec3 up;
    emitterBasis(dir, light.up, right, up);
    const bool disk = light.type != scene::PunctualLight::Type::Rect;
    // A disk of radius r integrates like a square of side r * sqrt(pi): the same area.
    const float halfW = disk ? light.radius * 0.8862269f : light.width * 0.5f;
    const float halfH = disk ? light.radius * 0.8862269f : light.height * 0.5f;
    const glm::vec3 hx = right * halfW;
    const glm::vec3 hy = up * halfH;
    corners[0] = light.position - hx - hy;
    corners[1] = light.position + hx - hy;
    corners[2] = light.position + hx + hy;
    corners[3] = light.position - hx + hy;
}

// ---- LTC ---------------------------------------------------------------------------------------

namespace {

// Hammersley point set: deterministic, no state, identical on every machine.
glm::vec2 hammersley(std::uint32_t i, std::uint32_t n) {
    std::uint32_t bits = i;
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return {static_cast<float>(i) / static_cast<float>(n), static_cast<float>(bits) * 2.3283064365386963e-10f};
}

// GGX half-vector importance sample around +Z.
glm::vec3 importanceSampleGgx(const glm::vec2& xi, float alpha) {
    const float phi = 2.0f * kPi * xi.x;
    const float cosTheta = std::sqrt((1.0f - xi.y) / (1.0f + (alpha * alpha - 1.0f) * xi.y));
    const float sinTheta = std::sqrt(std::max(1.0f - cosTheta * cosTheta, 0.0f));
    return {sinTheta * std::cos(phi), sinTheta * std::sin(phi), cosTheta};
}

// Height-correlated Smith visibility, already divided by 4 NoV NoL.
float visibilitySmith(float nDotV, float nDotL, float alpha) {
    const float a2 = alpha * alpha;
    const float v = nDotL * std::sqrt(nDotV * nDotV * (1.0f - a2) + a2);
    const float l = nDotV * std::sqrt(nDotL * nDotL * (1.0f - a2) + a2);
    return 0.5f / std::max(v + l, 1e-5f);
}

} // namespace

LtcTable buildLtcTable(std::uint32_t size, std::uint32_t samples) {
    LtcTable table;
    table.size = std::max(size, 2u);
    table.matrix.assign(static_cast<std::size_t>(table.size) * table.size, glm::vec4(1.0f, 0.0f, 1.0f, 0.0f));
    table.terms.assign(static_cast<std::size_t>(table.size) * table.size, glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
    const std::uint32_t count = std::max(samples, 4u) * std::max(samples, 4u);

    for (std::uint32_t r = 0; r < table.size; ++r) {
        // Row = perceptual roughness in (0, 1]; the shader samples with the same parametrisation.
        const float roughness = std::max((static_cast<float>(r) + 0.5f) / static_cast<float>(table.size), 0.02f);
        const float alpha = std::max(roughness * roughness, 1e-3f);
        for (std::uint32_t c = 0; c < table.size; ++c) {
            const float nDotV = std::clamp((static_cast<float>(c) + 0.5f) / static_cast<float>(table.size), 0.02f, 1.0f);
            const glm::vec3 v(std::sqrt(std::max(1.0f - nDotV * nDotV, 0.0f)), 0.0f, nDotV);

            float energy = 0.0f;   // directional albedo with F = 1
            float fresnelA = 0.0f; // split-sum scale on F0
            float fresnelB = 0.0f; // split-sum bias
            glm::vec3 mean(0.0f);
            // Two passes: the mean direction, then the spread about it.
            std::vector<glm::vec3> dirs;
            std::vector<float> weights;
            dirs.reserve(count);
            weights.reserve(count);
            for (std::uint32_t s = 0; s < count; ++s) {
                const glm::vec3 h = importanceSampleGgx(hammersley(s, count), alpha);
                const glm::vec3 l = 2.0f * glm::dot(v, h) * h - v;
                if (l.z <= 0.0f) {
                    continue;
                }
                const float nDotL = l.z;
                const float nDotH = std::max(h.z, 1e-5f);
                const float vDotH = std::max(glm::dot(v, h), 1e-5f);
                const float w = 4.0f * visibilitySmith(nDotV, nDotL, alpha) * nDotL * vDotH / nDotH;
                if (!(w > 0.0f) || !std::isfinite(w)) {
                    continue;
                }
                const float fc = std::pow(1.0f - vDotH, 5.0f);
                energy += w;
                fresnelA += w * (1.0f - fc);
                fresnelB += w * fc;
                mean += w * l;
                dirs.push_back(l);
                weights.push_back(w);
            }
            const std::size_t index = static_cast<std::size_t>(r) * table.size + c;
            if (energy <= 1e-6f || dirs.empty()) {
                continue;
            }
            table.terms[index] = glm::vec4(fresnelA / static_cast<float>(count),
                                           fresnelB / static_cast<float>(count), 0.0f, 0.0f);

            const glm::vec3 zAxis = safeNormalize(mean / energy, glm::vec3(0.0f, 0.0f, 1.0f));
            // The lobe is symmetric about the XZ plane, so the frame stays in it.
            const glm::vec3 xAxis = safeNormalize(glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), zAxis),
                                                  glm::vec3(1.0f, 0.0f, 0.0f));
            const glm::vec3 yAxis(0.0f, 1.0f, 0.0f);
            float mx = 0.0f;
            float my = 0.0f;
            for (std::size_t s = 0; s < dirs.size(); ++s) {
                const float dx = glm::dot(dirs[s], xAxis);
                const float dy = glm::dot(dirs[s], yAxis);
                mx += weights[s] * dx * dx;
                my += weights[s] * dy * dy;
            }
            // Clamped-cosine reference spread: <(w . t)^2> = 1/4 for either tangent axis.
            constexpr float kCosineSigma = 0.5f;
            const float s1 = std::clamp(std::sqrt(mx / energy) / kCosineSigma, 0.02f, 12.0f);
            const float s2 = std::clamp(std::sqrt(my / energy) / kCosineSigma, 0.02f, 12.0f);
            const float cz = std::max(zAxis.z, 0.06f);
            const float sx = zAxis.x;
            // Minv rows: (m00, 0, m02), (0, m11, 0), (m20, 0, 1) after normalising by Minv[2][2].
            table.matrix[index] = glm::vec4(1.0f / s1, -sx / (s1 * cz), 1.0f / (s2 * cz), sx / cz);
        }
    }
    return table;
}

} // namespace avgen::rendering
