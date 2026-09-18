#pragma once

// The path tracer's sampler (ADR-340, spec section 26).
//
// Seeded from (seed, pixel, sample, dimension) so any pixel of any frame can be regenerated in
// isolation and two runs of the same render agree exactly. `std::random_device`, `rand()` and the
// `<random>` distributions are banned in engine code -- research `offline-rendering.md` section
// 12.3 -- and this header does not reach for them; it wraps `avgen::Rng`, the project's PCG32,
// which already exists for precisely this reason.
//
// The decorrelation matters more than it looks. A sampler seeded on the pixel alone puts every
// sample of one pixel on the same sequence; a sampler seeded on the sample alone puts the same
// sequence under every pixel and the image gets structured noise that does not average out. The
// hash below mixes all three before the generator sees them.

#include "core/rng.hpp"

#include <glm/glm.hpp>

#include <cstdint>

namespace avgen::pathtrace {

// SplitMix64 finaliser. Avalanches, so two pixels one apart do not start on adjacent states.
[[nodiscard]] inline std::uint64_t mixSeed(std::uint64_t x) {
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}

class Sampler {
public:
    // `pixelIndex` is y * width + x; `sampleIndex` is which sample of that pixel.
    Sampler(std::uint64_t seed, std::uint32_t pixelIndex, std::uint32_t sampleIndex)
        : rng_(mixSeed(seed ^ (static_cast<std::uint64_t>(pixelIndex) * 0xD1B54A32D192ED03ULL)),
               mixSeed(static_cast<std::uint64_t>(sampleIndex) * 0xA0761D6478BD642FULL + 1)) {}

    [[nodiscard]] float next1D() { return rng_.nextFloat(); }

    [[nodiscard]] glm::vec2 next2D() {
        const float u = rng_.nextFloat();
        const float v = rng_.nextFloat();
        return {u, v};
    }

private:
    Rng rng_;
};

// ---- sampling warps, each with the PDF the integrator must divide by ----------------------------

// Uniform point on a unit disc, by the concentric map (Shirley & Chiu): equal-area, and unlike the
// polar map it does not stretch samples near the centre, which is visible in a low-sample render.
[[nodiscard]] inline glm::vec2 sampleUniformDisc(glm::vec2 u) {
    const float a = 2.0f * u.x - 1.0f;
    const float b = 2.0f * u.y - 1.0f;
    if (a == 0.0f && b == 0.0f) return {0.0f, 0.0f};
    float r = 0.0f;
    float phi = 0.0f;
    if (std::abs(a) > std::abs(b)) {
        r = a;
        phi = 0.7853981633974483f * (b / a);           // pi/4 * b/a
    } else {
        r = b;
        phi = 1.5707963267948966f - 0.7853981633974483f * (a / b); // pi/2 - pi/4 * a/b
    }
    return {r * std::cos(phi), r * std::sin(phi)};
}

// Cosine-weighted direction in the hemisphere about +Z. PDF = cos(theta) / pi.
[[nodiscard]] inline glm::vec3 sampleCosineHemisphere(glm::vec2 u) {
    const glm::vec2 d = sampleUniformDisc(u);
    const float z = std::sqrt(std::max(0.0f, 1.0f - d.x * d.x - d.y * d.y));
    return {d.x, d.y, z};
}

[[nodiscard]] inline float cosineHemispherePdf(float cosTheta) {
    return cosTheta <= 0.0f ? 0.0f : cosTheta * 0.31830988618379067f; // 1/pi
}

// An orthonormal basis around `n`, branchless and stable at the poles (Duff et al. 2017). The
// naive "cross with +Y unless n is +Y" version degenerates exactly where a ground plane's normal
// lives, which is the worst possible place for it.
inline void orthonormalBasis(const glm::vec3& n, glm::vec3& t, glm::vec3& b) {
    const float sign = n.z >= 0.0f ? 1.0f : -1.0f;
    const float a = -1.0f / (sign + n.z);
    const float d = n.x * n.y * a;
    t = glm::vec3(1.0f + sign * n.x * n.x * a, sign * d, -sign * n.x);
    b = glm::vec3(d, sign + n.y * n.y * a, -n.y);
}

[[nodiscard]] inline glm::vec3 toWorld(const glm::vec3& local, const glm::vec3& n) {
    glm::vec3 t{};
    glm::vec3 b{};
    orthonormalBasis(n, t, b);
    return local.x * t + local.y * b + local.z * n;
}

} // namespace avgen::pathtrace
