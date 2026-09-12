#pragma once

// The maths behind cascaded shadow maps (ADR-034), free of any GPU type so the tests can check it
// against a CPU reference without a device: the split scheme, the frustum corners, the stabilised
// per-cascade fit and the spot-light fit, plus the uniform block the shading pass reads.

#include "scene/scene_types.hpp"

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <vector>

namespace avgen::rendering {

constexpr std::uint32_t kMaxShadowViews = 8;
constexpr std::uint32_t kMaxCascades = 4;

// One shadow view: a light-space matrix, the world size of one of its texels (the normal-offset
// bias scale) and which light it belongs to.
struct ShadowView {
    glm::mat4 viewProj{1.0f};
    float texelWorldSize = 0.01f;
    float depthRange = 1.0f;  // world units the view's normalised depth spans (the bias scale)
    float farDistance = 0.0f; // cascade: the view depth this cascade covers up to
    int lightIndex = -1;      // index into the packed light buffer
    bool cascade = false;
    bool cube = false;        // one of six faces around a light with no single direction
};

// A light that shines in every direction needs six views, not one. They are ordinary views in the
// ordinary atlas -- the shading pass picks the face from the direction to the fragment -- so nothing
// else in the pipeline learns about cube maps: no cube texture, no cube sampler, and the depth
// passes the encoder already writes per view are unchanged.
constexpr std::uint32_t kPointShadowFaces = 6;

// `ShadowUniforms::views[i]`, mirrored by `shaders/lighting.wgsl`.
struct ShadowViewGpu {
    glm::mat4 viewProj;
    glm::vec4 params; // x = texel world size, y = depth bias, z = 1 when a cascade, w = far distance
};
static_assert(sizeof(ShadowViewGpu) == 80);

// Group 0 binding 3 of every scene pass (672 bytes).
struct ShadowUniforms {
    ShadowViewGpu views[kMaxShadowViews];
    glm::vec4 info;   // x = atlas resolution, y = cascade count, z = PCF taps, w = 1 when PCSS is on
    glm::vec4 splits; // the cascade far distances in view depth (unused entries hold the last one)
};
static_assert(sizeof(ShadowUniforms) == 672);

// Practical split scheme (Zhang et al. 2006): a blend of the logarithmic and uniform splits by
// `lambda` (0 = uniform, 1 = logarithmic). Returns `count` far distances, the last being `far`.
[[nodiscard]] std::vector<float> cascadeSplits(float nearPlane, float farPlane, std::uint32_t count,
                                               float lambda = 0.85f);

// The six faces around a point or area light, in the order the shader's dominant-axis test expects:
// +X, -X, +Y, -Y, +Z, -Z. Each is a 90-degree perspective from the light's own position, widened a
// hair so a PCF kernel at a face's edge still lands inside it rather than falling off and reading as
// unshadowed.
[[nodiscard]] std::array<ShadowView, kPointShadowFaces> fitPointShadow(const scene::PunctualLight& light,
                                                                       std::uint32_t resolution, float range);
// Which of the six a direction from the light falls in. Shared with the shader by construction:
// the test is the same one, and `tests/unit/test_shadow_math.cpp` walks a sphere of directions.
[[nodiscard]] std::uint32_t pointShadowFace(glm::vec3 fromLight);

// The eight world-space corners of a view frustum, near face first (counter-clockwise from the
// bottom left as seen from the camera), then the far face.
[[nodiscard]] std::array<glm::vec3, 8> frustumCorners(const glm::mat4& invViewProj);

// Fits one cascade to the camera sub-frustum between the view depths `nearDepth` and `farDepth`.
// `cameraNear` / `cameraFar` must be the planes `invViewProj` was built from, because the corners
// are interpolated between its near and far faces. The fit uses the sub-frustum's bounding sphere,
// so it is invariant to camera rotation, and the light-space origin is snapped to whole texels, so
// it is invariant to camera translation within a texel. `casterDistance` pulls the light's near
// plane back far enough to catch casters behind the visible range.
[[nodiscard]] ShadowView fitDirectionalCascade(const glm::mat4& invViewProj, float cameraNear,
                                               float cameraFar, float nearDepth, float farDepth,
                                               const glm::vec3& lightDirection, std::uint32_t resolution,
                                               float casterDistance);

// One perspective map covering a spot light's cone.
[[nodiscard]] ShadowView fitSpotShadow(const scene::PunctualLight& light, std::uint32_t resolution,
                                       float range);

} // namespace avgen::rendering
