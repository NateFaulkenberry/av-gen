#include "rendering/shadow_math.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace avgen::rendering {

namespace {

glm::vec3 safeNormalize(const glm::vec3& v, const glm::vec3& fallback) {
    const float len2 = glm::dot(v, v);
    return len2 > 1e-12f ? v * (1.0f / std::sqrt(len2)) : fallback;
}

glm::vec3 stableUp(const glm::vec3& direction) {
    return std::abs(direction.y) > 0.99f ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
}

} // namespace

std::vector<float> cascadeSplits(float nearPlane, float farPlane, std::uint32_t count, float lambda) {
    const std::uint32_t n = std::clamp(count, 1u, kMaxCascades);
    const float near = std::max(nearPlane, 1e-3f);
    const float far = std::max(farPlane, near * 1.001f);
    const float ratio = far / near;
    std::vector<float> splits;
    splits.reserve(n);
    for (std::uint32_t i = 1; i <= n; ++i) {
        const float p = static_cast<float>(i) / static_cast<float>(n);
        const float logSplit = near * std::pow(ratio, p);
        const float uniformSplit = near + (far - near) * p;
        splits.push_back(std::clamp(lambda, 0.0f, 1.0f) * logSplit + (1.0f - std::clamp(lambda, 0.0f, 1.0f)) * uniformSplit);
    }
    splits.back() = far;
    return splits;
}

std::array<glm::vec3, 8> frustumCorners(const glm::mat4& invViewProj) {
    // WebGPU clip space: x, y in [-1, 1], z in [0, 1].
    constexpr std::array<glm::vec3, 8> ndc = {
        glm::vec3(-1.0f, -1.0f, 0.0f), glm::vec3(1.0f, -1.0f, 0.0f), glm::vec3(1.0f, 1.0f, 0.0f),
        glm::vec3(-1.0f, 1.0f, 0.0f),  glm::vec3(-1.0f, -1.0f, 1.0f), glm::vec3(1.0f, -1.0f, 1.0f),
        glm::vec3(1.0f, 1.0f, 1.0f),   glm::vec3(-1.0f, 1.0f, 1.0f)};
    std::array<glm::vec3, 8> out{};
    for (std::size_t i = 0; i < ndc.size(); ++i) {
        const glm::vec4 h = invViewProj * glm::vec4(ndc[i], 1.0f);
        out[i] = glm::vec3(h) / h.w;
    }
    return out;
}

ShadowView fitDirectionalCascade(const glm::mat4& invViewProj, float cameraNear, float cameraFar,
                                 float nearDepth, float farDepth, const glm::vec3& lightDirection,
                                 std::uint32_t resolution, float casterDistance) {
    const std::array<glm::vec3, 8> corners = frustumCorners(invViewProj);
    const float span = std::max(cameraFar - cameraNear, 1e-4f);
    const float t0 = std::clamp((nearDepth - cameraNear) / span, 0.0f, 1.0f);
    const float t1 = std::clamp((farDepth - cameraNear) / span, 0.0f, 1.0f);

    // The sub-frustum corners: view depth varies linearly along each frustum edge.
    std::array<glm::vec3, 8> slice{};
    for (std::size_t i = 0; i < 4; ++i) {
        const glm::vec3 rayStart = corners[i];
        const glm::vec3 rayEnd = corners[i + 4];
        slice[i] = rayStart + (rayEnd - rayStart) * t0;
        slice[i + 4] = rayStart + (rayEnd - rayStart) * t1;
    }
    // Bounding sphere: stable under camera rotation, which is what stops the shadow edges swimming.
    glm::vec3 center(0.0f);
    for (const glm::vec3& c : slice) {
        center += c;
    }
    center /= static_cast<float>(slice.size());
    float radius = 0.0f;
    for (const glm::vec3& c : slice) {
        radius = std::max(radius, glm::length(c - center));
    }
    radius = std::max(std::ceil(radius * 16.0f) / 16.0f, 1e-3f); // quantised so it stops jittering

    const glm::vec3 dir = safeNormalize(lightDirection, glm::vec3(0.0f, -1.0f, 0.0f));
    // How far behind the cascade the light's near plane sits, so casters outside the visible range
    // still reach it. Every metre of pull-back costs depth precision, so it is kept tight.
    const float back = std::clamp(casterDistance, radius * 0.5f, radius * 3.0f);
    const glm::vec3 eye = center - dir * (radius + back);
    const glm::mat4 lightView = glm::lookAt(eye, center, stableUp(dir));

    // Texel snapping in light space: the projection window moves in whole-texel steps only.
    const float texel = 2.0f * radius / static_cast<float>(std::max(resolution, 1u));
    glm::vec3 centerLs = glm::vec3(lightView * glm::vec4(center, 1.0f));
    centerLs.x = std::floor(centerLs.x / texel) * texel;
    centerLs.y = std::floor(centerLs.y / texel) * texel;

    const float zFar = back + 2.0f * radius;
    const glm::mat4 proj = glm::ortho(centerLs.x - radius, centerLs.x + radius, centerLs.y - radius,
                                      centerLs.y + radius, 0.01f, zFar);

    ShadowView view;
    view.viewProj = proj * lightView;
    view.texelWorldSize = texel;
    view.depthRange = zFar;
    view.farDistance = farDepth;
    view.cascade = true;
    return view;
}

ShadowView fitSpotShadow(const scene::PunctualLight& light, std::uint32_t resolution, float range) {
    const glm::vec3 dir = safeNormalize(light.direction, glm::vec3(0.0f, -1.0f, 0.0f));
    const float far = std::max(range, 0.5f);
    const float near = std::max(far * 0.005f, 0.02f);
    const float fov = std::clamp(light.outerConeAngle * 2.0f * 1.05f, 0.05f, 3.0f);
    const glm::mat4 view = glm::lookAt(light.position, light.position + dir, stableUp(dir));
    const glm::mat4 proj = glm::perspective(fov, 1.0f, near, far);
    ShadowView out;
    out.viewProj = proj * view;
    // A texel at the far plane, which is the conservative end of the range.
    out.texelWorldSize = 2.0f * std::tan(fov * 0.5f) * far / static_cast<float>(std::max(resolution, 1u));
    out.depthRange = far - near;
    out.farDistance = far;
    out.cascade = false;
    return out;
}

} // namespace avgen::rendering
