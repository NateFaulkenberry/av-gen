#include "core/noise.hpp"

#include <algorithm>

#include <bit>
#include <cmath>

namespace avgen::noise {

U3 pcg3d(U3 v) {
    v.x = v.x * 1664525u + 1013904223u;
    v.y = v.y * 1664525u + 1013904223u;
    v.z = v.z * 1664525u + 1013904223u;
    v.x += v.y * v.z;
    v.y += v.z * v.x;
    v.z += v.x * v.y;
    v.x ^= v.x >> 16u;
    v.y ^= v.y >> 16u;
    v.z ^= v.z >> 16u;
    v.x += v.y * v.z;
    v.y += v.z * v.x;
    v.z += v.x * v.y;
    return v;
}

float hash01(std::int32_t cx, std::int32_t cy, std::int32_t cz, std::uint32_t seed) {
    const U3 h = pcg3d({std::bit_cast<std::uint32_t>(cx) + seed * 7919u,
                        std::bit_cast<std::uint32_t>(cy) + seed * 104729u,
                        std::bit_cast<std::uint32_t>(cz) + seed * 1299709u});
    return static_cast<float>(h.x) * (1.0f / 4294967296.0f);
}

float hashIndex(std::uint32_t seed, std::uint32_t index, std::uint32_t channel) {
    const U3 h = pcg3d({index, channel, seed});
    return static_cast<float>(h.x) * (1.0f / 4294967296.0f);
}

namespace {
float wgslMix(float a, float b, float t) {
    return a * (1.0f - t) + b * t;
}
} // namespace

float valueNoise(const glm::vec3& p, std::uint32_t seed) {
    const glm::vec3 c(std::floor(p.x), std::floor(p.y), std::floor(p.z));
    const glm::vec3 f = p - c;
    const glm::vec3 u = f * f * (3.0f - 2.0f * f);
    const auto ix = static_cast<std::int32_t>(c.x);
    const auto iy = static_cast<std::int32_t>(c.y);
    const auto iz = static_cast<std::int32_t>(c.z);
    const float n000 = hash01(ix, iy, iz, seed);
    const float n100 = hash01(ix + 1, iy, iz, seed);
    const float n010 = hash01(ix, iy + 1, iz, seed);
    const float n110 = hash01(ix + 1, iy + 1, iz, seed);
    const float n001 = hash01(ix, iy, iz + 1, seed);
    const float n101 = hash01(ix + 1, iy, iz + 1, seed);
    const float n011 = hash01(ix, iy + 1, iz + 1, seed);
    const float n111 = hash01(ix + 1, iy + 1, iz + 1, seed);
    const float x00 = wgslMix(n000, n100, u.x);
    const float x10 = wgslMix(n010, n110, u.x);
    const float x01 = wgslMix(n001, n101, u.x);
    const float x11 = wgslMix(n011, n111, u.x);
    const float y0 = wgslMix(x00, x10, u.y);
    const float y1 = wgslMix(x01, x11, u.y);
    return wgslMix(y0, y1, u.z);
}

float fbm3(glm::vec3 p, std::uint32_t seed) {
    return (0.5f * valueNoise(p, seed) + 0.25f * valueNoise(p * 2.03f + 17.0f, seed) +
            0.125f * valueNoise(p * 4.11f + 31.0f, seed)) /
           0.875f;
}

glm::vec3 fbm3Vec(const glm::vec3& p, std::uint32_t seed) {
    return glm::vec3(fbm3(p, seed) * 2.0f - 1.0f, fbm3(p + glm::vec3(31.7f), seed) * 2.0f - 1.0f,
                     fbm3(p + glm::vec3(67.3f), seed) * 2.0f - 1.0f);
}

glm::vec3 curlNoise(const glm::vec3& p, std::uint32_t seed, float epsilon) {
    const float e = epsilon;
    const glm::vec3 dx(e, 0.0f, 0.0f), dy(0.0f, e, 0.0f), dz(0.0f, 0.0f, e);
    const glm::vec3 px1 = fbm3Vec(p + dx, seed), px0 = fbm3Vec(p - dx, seed);
    const glm::vec3 py1 = fbm3Vec(p + dy, seed), py0 = fbm3Vec(p - dy, seed);
    const glm::vec3 pz1 = fbm3Vec(p + dz, seed), pz0 = fbm3Vec(p - dz, seed);
    const float inv = 1.0f / (2.0f * e);
    // curl F = (dFz/dy - dFy/dz, dFx/dz - dFz/dx, dFy/dx - dFx/dy)
    return glm::vec3((py1.z - py0.z) - (pz1.y - pz0.y), (pz1.x - pz0.x) - (px1.z - px0.z),
                     (px1.y - px0.y) - (py1.x - py0.x)) *
           inv;
}

glm::vec4 valueNoiseGrad(const glm::vec3& p, std::uint32_t seed) {
    const glm::vec3 c(std::floor(p.x), std::floor(p.y), std::floor(p.z));
    const glm::vec3 f = p - c;
    const glm::vec3 u = f * f * (3.0f - 2.0f * f);
    const glm::vec3 du = 6.0f * f * (1.0f - f);
    const auto ix = static_cast<std::int32_t>(c.x);
    const auto iy = static_cast<std::int32_t>(c.y);
    const auto iz = static_cast<std::int32_t>(c.z);
    const float n000 = hash01(ix, iy, iz, seed);
    const float n100 = hash01(ix + 1, iy, iz, seed);
    const float n010 = hash01(ix, iy + 1, iz, seed);
    const float n110 = hash01(ix + 1, iy + 1, iz, seed);
    const float n001 = hash01(ix, iy, iz + 1, seed);
    const float n101 = hash01(ix + 1, iy, iz + 1, seed);
    const float n011 = hash01(ix, iy + 1, iz + 1, seed);
    const float n111 = hash01(ix + 1, iy + 1, iz + 1, seed);
    const float k1 = n100 - n000;
    const float k2 = n010 - n000;
    const float k3 = n001 - n000;
    const float k4 = n000 - n100 - n010 + n110;
    const float k5 = n000 - n010 - n001 + n011;
    const float k6 = n000 - n100 - n001 + n101;
    const float k7 = -n000 + n100 + n010 - n110 + n001 - n101 - n011 + n111;
    const float value = n000 + k1 * u.x + k2 * u.y + k3 * u.z + k4 * u.x * u.y + k5 * u.y * u.z +
                        k6 * u.z * u.x + k7 * u.x * u.y * u.z;
    const glm::vec3 grad = du * glm::vec3(k1 + k4 * u.y + k6 * u.z + k7 * u.y * u.z,
                                          k2 + k5 * u.z + k4 * u.x + k7 * u.z * u.x,
                                          k3 + k6 * u.x + k5 * u.y + k7 * u.x * u.y);
    return glm::vec4(grad, value);
}

glm::vec3 flowCurl(const glm::vec3& p, float t, std::uint32_t seed) {
    const glm::vec3 da = glm::vec3(0.31f, 0.17f, -0.23f) * t;
    const glm::vec3 db = glm::vec3(-0.19f, 0.27f, 0.13f) * t;
    const glm::vec3 ga = glm::vec3(valueNoiseGrad(p + da, seed)) +
                         0.5f * glm::vec3(valueNoiseGrad(p * 2.03f + glm::vec3(17.0f) - da, seed));
    const glm::vec3 gb = glm::vec3(valueNoiseGrad(p + glm::vec3(31.7f) + db, seed + 1u)) +
                         0.5f * glm::vec3(valueNoiseGrad(p * 2.03f + glm::vec3(47.3f) - db, seed + 1u));
    return glm::cross(ga, gb);
}

float voronoiF1(const glm::vec3& p, std::uint32_t seed) {
    const glm::vec3 c(std::floor(p.x), std::floor(p.y), std::floor(p.z));
    const glm::vec3 f = p - c;
    float best = 8.0f;
    for (int z = -1; z <= 1; ++z) {
        for (int y = -1; y <= 1; ++y) {
            for (int x = -1; x <= 1; ++x) {
                const auto ix = static_cast<std::int32_t>(c.x) + x;
                const auto iy = static_cast<std::int32_t>(c.y) + y;
                const auto iz = static_cast<std::int32_t>(c.z) + z;
                const glm::vec3 jitter(hash01(ix, iy, iz, seed), hash01(ix, iy, iz, seed + 1u),
                                       hash01(ix, iy, iz, seed + 2u));
                const glm::vec3 d = glm::vec3(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)) +
                                    jitter - f;
                best = std::fmin(best, glm::dot(d, d));
            }
        }
    }
    return std::sqrt(best);
}

float regionField(const glm::vec3& p, std::uint32_t seed) {
    return std::clamp((fbm3(p, seed) - 0.5f) * 2.6f, -1.0f, 1.0f);
}

} // namespace avgen::noise
