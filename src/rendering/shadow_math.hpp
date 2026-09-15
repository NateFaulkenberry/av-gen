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

// Group 0 binding 3 of every scene pass (688 bytes).
struct ShadowUniforms {
    ShadowViewGpu views[kMaxShadowViews];
    glm::vec4 info;   // x = atlas resolution, y = cascade count, z = PCF taps, w = 1 when PCSS is on
    glm::vec4 splits; // the cascade far distances in view depth (unused entries hold the last one)
    // ADR-227. x = PCSS blocker-search taps. `info` and `splits` were both full, which is why
    // `QualitySettings::pcssBlockerTaps` sat unread for as long as it did -- wiring it was a
    // uniform-layout change rather than a line. yzw are free and deliberately named nothing:
    // the next field to want a lane takes one rather than overloading a meaning onto an existing
    // one, which is the mistake `info.z` serving two tap counts already was.
    glm::vec4 info2;
};
static_assert(sizeof(ShadowUniforms) == 688);

// ADR-112: the shadow-map resolution the shadowed range is sized against, whatever resolution the
// tier actually renders it at.
//
// The range decides *where the shadows stop*, which is composition, not quality. A preview that
// showed shadows to 40 m and a final that showed them to 150 m would not be the same shot, and a
// preview that does not predict the final is worse than no preview. So every tier gets the same
// range and they differ in how sharp it is: preview's 1024 gets twice the texel this reference
// implies, offline's 4096 gets half. This is the one place a tier is deliberately not allowed to
// scale something.
constexpr std::uint32_t kShadowRangeReference = 2048;

// ADR-112: how far the directional cascades should reach, in view depth.
//
// Two rules, and the smaller wins. The first is the old one: three scene radii, clamped to the
// camera's own planes -- enough to cover the world, never more than the camera can see. The second
// is new, and it is the one that makes a wide scene of small objects cast anything: the range at
// which the *coarsest* cascade's texel is still no larger than `texelTarget` metres.
//
// The second rule exists because the coarsest texel has exactly one lever. A camera frustum widens
// linearly with distance, so the last cascade's bounding sphere is close to `k * range` -- k is
// about 0.8 whatever the split scheme -- and its texel is `2 * k * range / resolution`. The split
// lambda and the cascade count redistribute texels among the *near* cascades and leave that
// unmoved: measured on the real fit, the range that yields an 8 cm coarsest texel is 101 m at two
// cascades, 105 m at three and 108 m at four. Shortening the range is the only thing that shortens
// the texel, and raising the resolution is the move this is here to avoid.
//
// `resolution` is `kShadowRangeReference`, not the tier's own map size -- see above.
// `texelTarget <= 0` disables the second rule and returns the first, which is the pre-ADR-112
// behaviour.
[[nodiscard]] float directionalShadowRange(float cameraNear, float cameraFar, float sceneRadius,
                                           std::uint32_t resolution, float texelTarget);

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
