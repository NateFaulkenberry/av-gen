#pragma once

// Deterministic hashing and noise shared by the CPU and the GPU (shaders/common noise in
// procedural.wgsl / fields.wgsl are transliterations of these functions; tests compare them).
// Everything is a pure function of its arguments: no state, no wall clock.
//
//   pcg3d       : Jarzynski & Olano, "Hash Functions for GPU Rendering" (JCGT 2020)
//   hash01      : one uniform in [0, 1) from three integers + seed (per-axis seed mixing)
//   hashIndex   : uniform in [0, 1) for (seed, index, channel) — the per-instance random
//   valueNoise  : trilinear value noise with a smoothstep fade, in [0, 1)
//   fbm3        : 3-octave fBM of valueNoise, normalised to [0, 1)
//   fbm3Vec     : three decorrelated channels (offsets 31.7 / 67.3), each in [-1, 1)
//   curlNoise   : curl of the fbm3Vec potential by central differences (divergence-free)
//   voronoiF1   : distance to the nearest jittered cell point (Worley F1), in [0, ~1.4)

#include <glm/glm.hpp>

#include <cstdint>

namespace avgen::noise {

struct U3 {
    std::uint32_t x, y, z;
};
[[nodiscard]] U3 pcg3d(U3 v);
[[nodiscard]] float hash01(std::int32_t cx, std::int32_t cy, std::int32_t cz, std::uint32_t seed);
[[nodiscard]] float hashIndex(std::uint32_t seed, std::uint32_t index, std::uint32_t channel);
[[nodiscard]] float valueNoise(const glm::vec3& p, std::uint32_t seed);
[[nodiscard]] float fbm3(glm::vec3 p, std::uint32_t seed);
[[nodiscard]] glm::vec3 fbm3Vec(const glm::vec3& p, std::uint32_t seed);
[[nodiscard]] glm::vec3 curlNoise(const glm::vec3& p, std::uint32_t seed, float epsilon = 0.01f);
[[nodiscard]] float voronoiF1(const glm::vec3& p, std::uint32_t seed);

// A smooth signed field in [-1, 1] for "which region of the world is this", used where a setting
// means "how far this swings across the map". Raw fbm3 bunches around 0.5, so feeding it straight
// in delivers about a third of the swing the caller asked for; this stretches it about the
// midpoint and clamps, so the amplitude a caller passes is the amplitude they get. Callers that
// must agree with each other (a plant and the light it casts) share this.
[[nodiscard]] float regionField(const glm::vec3& p, std::uint32_t seed);

} // namespace avgen::noise
