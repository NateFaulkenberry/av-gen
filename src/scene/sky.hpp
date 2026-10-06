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
    float mirror = 0.0f;                        // ADR-1167
    [[nodiscard]] std::uint64_t hash() const; // changes exactly when a rebuild is needed
};

// ADR-1022: whether the lighting cube built from `built` still stands for `current`, so the
// renderer can skip the rebuild. A sky driven by routes through slow chains moves by a few parts in
// a million a frame as the chains settle, and every one of those moves used to rebuild the whole
// cube, irradiance and prefilter chain (about 20 ms a frame at 1080p). The tolerances:
//   * each colour (zenith, horizon, ground, sun) within 1/128 of its own largest channel, so a
//     channel near zero beside a bright one cannot force a rebuild on its own, plus 1e-5 absolute
//     for a sky that is nearly black;
//   * the intensity, sun intensity, haze, glow and disc widths within 1/128 relative plus 1e-5;
//   * the sun direction within 0.25 mrad, a quarter of a texel of the offline tier's 1024 face.
// Measured against a rebuild on every frame (Sonic Garden pad, morph and perc, 1140 frames): no
// pixel differs by more than 1 in 8 bits, and the builds fell from 1143 to 355 (ADR-1022).
// Comparing against the sky that was *built*, not last frame's, means a slow drift still rebuilds
// once it has added up; nothing lags by more than the tolerance.
[[nodiscard]] bool skyWithinRebuildTolerance(const SkyRuntime& built, const SkyRuntime& current);

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
//   ADR-1167, with mirror m > 0: below the horizon the sky above it is reflected in, sun included --
//   out += m * (1 - band) * (sky(dir') + sun(dir') - ground) * intensity, dir' = (dir.x, -dir.y, dir.z),
//   where sky() and sun() are the terms above without the band. At m = 0 nothing changes.
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
