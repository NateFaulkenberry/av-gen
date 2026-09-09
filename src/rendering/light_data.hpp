#pragma once

// Light packing, the clustered-forward froxel grid and the linearly-transformed-cone table
// (ADR-033). Everything here is GPU-free and deterministic so the tests can check the packing,
// the cluster assignment and the LTC fit against a CPU reference without a device.

#include "scene/scene_types.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace avgen::rendering {

// ---- packed light ----------------------------------------------------------------------------

// One light as `shaders/lighting.wgsl` sees it (128 bytes, std430-friendly). `colorIntensity`
// already carries the colour-temperature product, so the shader never converts Kelvin.
struct GpuLight {
    glm::vec4 positionType;   // xyz = world position, w = type code (scene::PunctualLight::Type)
    glm::vec4 directionRange; // xyz = direction the light travels (unit), w = range (0 = infinite)
    glm::vec4 colorIntensity; // rgb = colour * temperature tint * intensity, w = influence radius
    glm::vec4 cone;           // x = cos(outer), y = 1 / max(cos(inner) - cos(outer)), z = shadow view index (-1), w = flag bits
    glm::vec4 sizeSoft;       // x = width, y = height, z = radius, w = softness
    glm::vec4 up;             // xyz = emitter up axis (unit), w = shadow strength
    glm::vec4 tangent;        // xyz = emitter right axis (unit), w = contact-shadow strength
    glm::vec4 extra;          // x = diffuseOnly, y = specularOnly, z = volumetric strength, w = shadow bias (world units)
};
static_assert(sizeof(GpuLight) == 128);

// `GpuLight::cone.w` bits.
enum LightFlagBits : std::uint32_t {
    kLightFlagCastsShadow = 1u << 0,
    kLightFlagCascaded = 1u << 1, // the shadow view index names the first of `cascadeCount` views
    kLightFlagArea = 1u << 2,     // Rect/Disk/Tube/Sphere: the shader takes the area path
};

// The distance past which a light contributes less than `cutoff` of its peak. Explicit `range`
// wins; otherwise it comes from the inverse-square falloff of the light's brightest channel.
[[nodiscard]] float lightInfluenceRadius(const scene::PunctualLight& light, float cutoff = 0.004f);

// Packs one scene light. `colorTemperatureToRgb` is applied here, so the GPU never sees Kelvin.
// `shadowView` is the index of its first shadow view, or -1.
[[nodiscard]] GpuLight packLight(const scene::PunctualLight& light, int shadowView = -1,
                                 bool cascaded = false);

// Scene lights ordered the way the shader expects: every enabled directional light first (they
// are evaluated for every fragment), then the local ones (which go through the cluster grid).
// Returns the number of leading directional lights.
[[nodiscard]] std::uint32_t orderLightsForShading(const std::vector<scene::PunctualLight>& lights,
                                                  std::vector<const scene::PunctualLight*>& out);

// ---- clustered forward -----------------------------------------------------------------------

constexpr std::uint32_t kClusterX = 16;
constexpr std::uint32_t kClusterY = 8;
constexpr std::uint32_t kClusterZ = 24;
constexpr std::uint32_t kClusterCount = kClusterX * kClusterY * kClusterZ;
constexpr std::uint32_t kMaxLightsPerCluster = 32;
constexpr std::uint32_t kMaxSceneLights = 256;

// The froxel grid of one camera. Depth is sliced exponentially (Olsson et al. 2012) so a slice is
// a constant ratio of the one before, which keeps cluster volumes roughly cubic across the range.
struct ClusterGrid {
    std::uint32_t x = kClusterX;
    std::uint32_t y = kClusterY;
    std::uint32_t z = kClusterZ;
    float zNear = 0.1f;     // view-space distance of the first slice boundary (> 0)
    float zFar = 200.0f;    // view-space distance of the last slice boundary
    float tanHalfFovY = 0.5f;
    float aspect = 16.0f / 9.0f;

    [[nodiscard]] std::uint32_t count() const { return x * y * z; }
    [[nodiscard]] float sliceScale() const;  // slice = log2(z) * sliceScale + sliceBias
    [[nodiscard]] float sliceBias() const;
    [[nodiscard]] float sliceNear(std::uint32_t k) const; // view-space depth of slice k's near plane
    [[nodiscard]] std::uint32_t sliceOf(float viewDepth) const;
    [[nodiscard]] std::uint32_t indexOf(std::uint32_t i, std::uint32_t j, std::uint32_t k) const {
        return (k * y + j) * x + i;
    }
    // View-space axis-aligned bounds of one froxel (the camera looks down -Z, so both z values
    // are negative and `min.z <= max.z`).
    void bounds(std::uint32_t i, std::uint32_t j, std::uint32_t k, glm::vec3& min, glm::vec3& max) const;
};

// True when the light's sphere of influence touches the froxel. `viewPosition` is the light's
// position in view space and `radius` its influence radius.
[[nodiscard]] bool clusterTouchesSphere(const ClusterGrid& grid, std::uint32_t i, std::uint32_t j,
                                        std::uint32_t k, const glm::vec3& viewPosition, float radius);

// CPU reference of `shaders/clusters.wgsl`: for every froxel, the indices of the local lights that
// reach it, capped at `kMaxLightsPerCluster`. `viewPositions[n]` is local light n in view space,
// `radii[n]` its influence radius; the returned list is `grid.count()` entries long and each is
// sorted ascending, exactly as the compute pass writes it.
[[nodiscard]] std::vector<std::vector<std::uint32_t>> assignClusters(const ClusterGrid& grid,
                                                                     const std::vector<glm::vec3>& viewPositions,
                                                                     const std::vector<float>& radii);

// The clamped-cosine irradiance of a quadrilateral emitter of unit radiance at `point` with
// surface normal `normal` (Heitz et al.'s polygon form factor, which is what `ltcEvaluate` in
// shaders/lighting.wgsl computes with the identity transform, so the CPU and the GPU agree on the
// diffuse term of a rect or disk light). One-sided: the back of the emitter contributes nothing.
[[nodiscard]] float polygonIrradiance(const glm::vec3& point, const glm::vec3& normal,
                                      const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2,
                                      const glm::vec3& p3);

// The four corners of a Rect emitter, in the order `polygonIrradiance` expects. Disk and Sphere
// emitters use the square of equal area, which is what the shader does.
void areaLightCorners(const scene::PunctualLight& light, glm::vec3 corners[4]);

// ---- linearly transformed cones (ADR-033) ----------------------------------------------------

// The LTC table is generated at startup (never shipped as binary data) by matching the first and
// second moments of the GGX lobe with a linearly transformed clamped-cosine lobe, which is the
// initial guess of Heitz et al.'s fit and is already smooth and energy-consistent. Two square
// RGBA32F textures indexed by (roughness, cos theta_v):
//   ltc1 = the inverse transform, packed as (m00, m02, m11, m20) of
//          [[m00, 0, m02], [0, m11, 0], [m20, 0, 1]]
//   ltc2 = (directional albedo, Fresnel bias, 0, 0) for the split-sum specular term.
constexpr std::uint32_t kLtcTableSize = 32;

struct LtcTable {
    std::uint32_t size = kLtcTableSize;
    std::vector<glm::vec4> matrix; // size * size, row = roughness, column = cos theta
    std::vector<glm::vec4> terms;  // size * size, x = albedo, y = Fresnel bias
};

// Deterministic; ~10 ms at the default size. `samples` is the square root of the hemisphere
// sample count used per cell.
[[nodiscard]] LtcTable buildLtcTable(std::uint32_t size = kLtcTableSize, std::uint32_t samples = 32);

} // namespace avgen::rendering
