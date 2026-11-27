#pragma once

// Volumetric lighting's GPU-free half (ADR-2210/2211/2213): whether a scene's air goes to the froxel grid and why
// not, the grid a tier gives a frame, and the medium on the CPU. Apart from volumetric_light_renderer.hpp so the
// CPU tests, the path tracer and anything else without the GPU headers can reach it.

#include "rendering/render_quality.hpp"

#include <glm/glm.hpp>

#include <cstdint>

namespace avgen::scene {
struct Scene;
struct Environment;
} // namespace avgen::scene

namespace avgen::rendering {

// Why a scene that asked for volumetric lighting is not getting it. Never silent: the renderer logs the
// reason once and reports it in its stats (ADR-2213's "never a silent off").
enum class VolumetricLightDecline : std::uint8_t {
    None,         // active
    Off,          // the scene did not ask for it
    NoAir,        // volumeDensity is 0: there is no air to light
    DensityField, // the air names a density field, which the froxel does not carry yet; the march keeps the air
    ColorField,   // the same for a colour field
};
[[nodiscard]] const char* volumetricLightDeclineName(VolumetricLightDecline d);

[[nodiscard]] VolumetricLightDecline volumetricLightDecline(const scene::Scene& scene);
[[nodiscard]] inline bool volumetricLightActive(const scene::Scene& scene) {
    return volumetricLightDecline(scene) == VolumetricLightDecline::None;
}

// ADR-2211: the environment's air at a world point on the CPU -- the march's `volumeDensityAt` without the
// ground follow, density field and noise (the terms that need GPU data), times ADR-705's horizon growth at
// `distanceFromEye`. Extinction per metre is this times `volumeAbsorption`. Pure; for the tracer and tests.
[[nodiscard]] float airDensityAt(const scene::Environment& env, const glm::vec3& p, float distanceFromEye);
// The froxel grid's size for a frame and a tier (public for the unit test).
[[nodiscard]] glm::uvec3 volumetricLightGrid(std::uint32_t width, std::uint32_t height, const QualitySettings& q);

} // namespace avgen::rendering
