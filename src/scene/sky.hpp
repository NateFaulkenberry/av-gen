#pragma once

// Procedural sky environment (ADR-036). A scene with no HDR environment map still needs something
// for metals and rough surfaces to reflect, so the renderer synthesises one: an analytic gradient
// sky with a sun disc placed from the scene's key light, rendered into the same cubemap /
// irradiance / prefiltered chain the HDR path already uses (rendering::EnvironmentProcessor).
//
// `SkySettings` lives on scene::Environment (parameters `env/sky/*`). `SkyRuntime` is the resolved
// form the CPU reference and the GPU pass both evaluate: the sun direction and colour are already
// folded in from the key light, so the shader takes no light data. `skyRadiance` is the reference
// implementation `shaders/environment.wgsl`'s fs_sky transliterates.

#include "scene/scene_types.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace avgen::scene {

// The resolved sky one build evaluates. Deterministic in the settings and the light list.
struct SkyRuntime {
    glm::vec3 zenithColor{0.0f};
    glm::vec3 horizonColor{0.0f};
    glm::vec3 groundColor{0.0f};
    float hazeWidth = 0.25f;
    glm::vec3 sunColor{1.0f};   // already multiplied by the key light's colour
    float sunIntensity = 8.0f;
    float sunAngularRadius = 0.045f;
    float sunGlowWidth = 0.18f;
    float intensity = 1.0f;
    glm::vec3 sunDirection{0.0f, 1.0f, 0.0f}; // unit, pointing *towards* the sun
    [[nodiscard]] std::uint64_t hash() const; // changes exactly when a rebuild is needed
};

// The key light a sky takes its sun from: the first enabled directional light with role Key, else
// the first enabled directional light, else null.
[[nodiscard]] const PunctualLight* skyKeyLight(const std::vector<PunctualLight>& lights);

// Resolves `settings` against the scene's lights. With `useKeyLight` and a key light present the
// sun direction is -normalize(light.direction) and the sun colour is tinted by the light's colour
// (normalised to luminance 1, so a light's intensity never doubles as sky brightness).
[[nodiscard]] SkyRuntime resolveSky(const SkySettings& settings, const std::vector<PunctualLight>& lights);

// Radiance along `dir` (need not be normalised). The exact model:
//   h    = saturate(dir.y),  haze = exp(-h / max(hazeWidth, 1e-3))
//   sky  = mix(zenith, horizon, haze)
//   band = smoothstep(-0.03, 0.03, dir.y)          (a soft horizon, so the cube has no seam)
//   base = mix(ground, sky, band)
//   theta = angle between dir and sunDirection
//   disc = 1 - smoothstep(r * 0.85, r * 1.15, theta) with r = max(sunAngularRadius, minRadius),
//          scaled by (sunAngularRadius / r)^2 so widening the disc conserves its energy
//   glow = exp(-theta / max(sunGlowWidth, 1e-3)) * 0.02   (the aureole around the disc)
//   out  = (base + sunColor * sunIntensity * (disc + glow) * band) * intensity
// `minRadius` is how wide one texel of the target is in radians; it keeps the disc from falling
// between texels in the coarse mips (the analytic stand-in for downsampling the cube).
[[nodiscard]] glm::vec3 skyRadiance(const SkyRuntime& sky, const glm::vec3& dir, float minRadius = 0.0f);

// The world direction of an equirectangular environment map's brightest feature -- its sun or its
// moon (ADR-049). Found as the radiance-weighted centroid of every texel within `coreFraction` of
// the brightest one, which lands on the disc's centre rather than on whichever single texel won,
// and so barely moves between a 2K and an 8K copy of the same sky. `rotationRadians` is the
// scene's `environmentRotation`: the returned direction is in world space, already un-rotated, so
// it can be handed straight to a light.
//
// The map's parameterisation is the one shaders/environment.wgsl's `equirectUv` inverts:
// u = 0.5 + atan2(z, x) / 2pi, v = acos(y) / pi. Returns +Y for an empty or non-HDR map.
[[nodiscard]] glm::vec3 environmentDominantDirection(const TextureData& equirect, float rotationRadians = 0.0f,
                                                     float coreFraction = 0.25f);

// Cosine-weighted irradiance arriving at a surface with normal `n`, by a fixed Fibonacci-hemisphere
// quadrature of `samples` directions. Deterministic: the same arguments always give the same value.
// This is the CPU reference for the irradiance cube the GPU builds.
[[nodiscard]] glm::vec3 skyIrradiance(const SkyRuntime& sky, const glm::vec3& n, int samples = 512);

} // namespace avgen::scene
