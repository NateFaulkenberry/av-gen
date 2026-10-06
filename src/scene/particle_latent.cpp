#include "scene/particle_latent.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace avgen::scene {

namespace {

float wgslSmoothstep(float e0, float e1, float x) {
    const float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

float fractf(float x) { return x - std::floor(x); }

// sdf.wgsl's sdfSafeNormalize: +Y for a zero vector.
glm::vec3 safeNormalize(const glm::vec3& v) {
    const float len = glm::length(v);
    return len > 1e-8f ? v / len : glm::vec3(0.0f, 1.0f, 0.0f);
}

constexpr std::array<glm::vec3, 4> kTetra = {glm::vec3(1.0f, -1.0f, -1.0f), glm::vec3(-1.0f, -1.0f, 1.0f),
                                             glm::vec3(-1.0f, 1.0f, -1.0f), glm::vec3(1.0f, 1.0f, 1.0f)};

} // namespace

float latentTheta(float seed, float width) {
    const float h = fractf(seed * 61.0f);
    return width + (1.0f - width - width) * h; // WGSL mix(a, b, t) = a + (b - a) * t
}

float latentBinding(float theta, float coherence, float width) {
    return wgslSmoothstep(theta - width, theta + width, coherence);
}

float latentStiffness(float strength, float coherence, float dt) {
    float k = strength * (18.0f + 70.0f * coherence * coherence);
    if (dt > 0.0f) {
        k = std::min(k, 0.8f / (dt * dt));
    }
    return k;
}

float latentGradientEpsilon(const glm::vec3& boundsMin, const glm::vec3& boundsMax) {
    return std::max(1e-3f * glm::length(boundsMax - boundsMin), 1e-4f);
}

LatentSample latentProject(std::span<const spatial::SdfNodeGpu> program, const glm::vec3& pLocal, double time,
                           float epsilon, const spatial::FieldSet* fields) {
    std::array<float, 4> d{};
    for (std::size_t i = 0; i < 4; ++i) {
        d[i] = spatial::evaluatePacked(program, pLocal + kTetra[i] * epsilon, time, fields);
    }
    const glm::vec3 g = kTetra[0] * d[0] + kTetra[1] * d[1] + kTetra[2] * d[2] + kTetra[3] * d[3];
    LatentSample s;
    s.distance = 0.25f * (d[0] + d[1] + d[2] + d[3]);
    s.normal = safeNormalize(g);
    s.projection = pLocal - s.normal * s.distance;
    return s;
}

glm::vec3 latentVelocityStep(const LatentStep& step, std::span<const spatial::SdfNodeGpu> program,
                             const glm::vec3& position, const glm::vec3& velocity, float seed,
                             const spatial::FieldSet* fields) {
    const float theta = latentTheta(seed, step.width);
    const float b = latentBinding(theta, step.coherence, step.width);
    const float release = std::max(latentBinding(theta, step.prevCoherence, step.width) - b, 0.0f);
    if (b <= 0.0f && release <= 0.0f) {
        return velocity;
    }
    const glm::vec3 pl = glm::vec3(step.inverse * glm::vec4(position, 1.0f));
    const LatentSample s = latentProject(program, pl, step.time, step.epsilon, fields);
    const glm::vec3 target = glm::vec3(step.model * glm::vec4(s.projection, 1.0f));
    const glm::vec3 nW = safeNormalize(glm::vec3(step.normal * glm::vec4(s.normal, 0.0f)));
    const float k = latentStiffness(step.strength, step.coherence, step.dt);
    const float c = 2.0f * 0.55f * std::sqrt(k);
    const glm::vec3 acc = b * (k * (target - position) - c * velocity);
    glm::vec3 v = velocity + acc * step.dt;
    if (release > 0.0f) {
        const float h = fractf(seed * 173.0f);
        const float sgn = fractf(seed * 29.0f) < 0.2f ? -1.0f : 1.0f;
        v += release * step.release * nW * (sgn * (0.6f + 0.8f * h));
    }
    return v;
}

void splatDensity(std::span<const glm::vec3> positions, const glm::vec3& lo, const glm::vec3& hi, int res,
                  std::vector<std::uint32_t>& grid) {
    const auto n = static_cast<std::size_t>(res);
    grid.assign(n * n * n, 0u);
    const glm::vec3 cell = (hi - lo) / static_cast<float>(res);
    for (const glm::vec3& p : positions) {
        const glm::vec3 g = (p - lo) / cell - 0.5f;
        const glm::ivec3 g0 = glm::ivec3(glm::floor(g));
        if (g0.x < 0 || g0.y < 0 || g0.z < 0 || g0.x >= res - 1 || g0.y >= res - 1 || g0.z >= res - 1) {
            continue;
        }
        const glm::vec3 f = g - glm::vec3(g0);
        for (int k = 0; k < 8; ++k) {
            const glm::ivec3 o(k & 1, (k >> 1) & 1, (k >> 2) & 1);
            const float wx = o.x == 1 ? f.x : 1.0f - f.x;
            const float wy = o.y == 1 ? f.y : 1.0f - f.y;
            const float wz = o.z == 1 ? f.z : 1.0f - f.z;
            const float w = wx * wy * wz;
            const glm::ivec3 c = g0 + o;
            const std::size_t idx = static_cast<std::size_t>(c.x) + n * (static_cast<std::size_t>(c.y) + n * c.z);
            grid[idx] += static_cast<std::uint32_t>(w * static_cast<float>(kDensityFixedScale) + 0.5f);
        }
    }
}

std::vector<float> resolveDensity(std::span<const std::uint32_t> grid, int res, float weight) {
    const auto n = static_cast<std::size_t>(res);
    std::vector<float> out(n * n * n, 0.0f);
    auto at = [&](int x, int y, int z) {
        x = std::clamp(x, 0, res - 1);
        y = std::clamp(y, 0, res - 1);
        z = std::clamp(z, 0, res - 1);
        return static_cast<float>(grid[static_cast<std::size_t>(x) + n * (static_cast<std::size_t>(y) + n * z)]);
    };
    for (int z = 0; z < res; ++z) {
        for (int y = 0; y < res; ++y) {
            for (int x = 0; x < res; ++x) {
                float sum = 0.0f;
                for (int dz = -1; dz <= 1; ++dz) {
                    for (int dy = -1; dy <= 1; ++dy) {
                        for (int dx = -1; dx <= 1; ++dx) {
                            const float w = static_cast<float>((2 - std::abs(dx)) * (2 - std::abs(dy)) * (2 - std::abs(dz)));
                            sum += w * at(x + dx, y + dy, z + dz);
                        }
                    }
                }
                out[static_cast<std::size_t>(x) + n * (static_cast<std::size_t>(y) + n * z)] =
                    sum / (64.0f * static_cast<float>(kDensityFixedScale)) * weight;
            }
        }
    }
    return out;
}

} // namespace avgen::scene
