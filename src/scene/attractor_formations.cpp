#include "scene/attractor_formations.hpp"

#include <algorithm>
#include <cmath>
#include <thread>

namespace avgen::scene {

namespace {

glm::vec3 flow(AttractorSystem system, glm::vec3 s) {
    switch (system) {
    case AttractorSystem::Thomas: { // Thomas (1999), b in the chaotic range (< 0.208186)
        constexpr float b = 0.19f;
        return {std::sin(s.y) - b * s.x, std::sin(s.z) - b * s.y, std::sin(s.x) - b * s.z};
    }
    case AttractorSystem::Rossler:
    default: { // Rossler (1976), the canonical a = b = 0.2, c = 5.7
        constexpr float a = 0.2f, b = 0.2f, c = 5.7f;
        return {-s.y - s.z, s.x + a * s.y, b + s.z * (s.x - c)};
    }
    }
}

glm::vec3 rk4(AttractorSystem system, glm::vec3 s, float dt) {
    const glm::vec3 k1 = flow(system, s);
    const glm::vec3 k2 = flow(system, s + 0.5f * dt * k1);
    const glm::vec3 k3 = flow(system, s + 0.5f * dt * k2);
    const glm::vec3 k4 = flow(system, s + dt * k3);
    return s + (dt / 6.0f) * (k1 + 2.0f * k2 + 2.0f * k3 + k4);
}

bool finite3(glm::vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

float segmentDistance(glm::vec3 p, glm::vec3 a, glm::vec3 b, float& t) {
    const glm::vec3 ab = b - a;
    t = std::clamp(glm::dot(p - a, ab) / std::max(glm::dot(ab, ab), 1e-9f), 0.0f, 1.0f);
    return glm::length(p - (a + ab * t));
}

template <class Fn>
void parallelFor(int n, Fn&& fn) {
    const unsigned threads = std::max(1u, std::min(16u, std::thread::hardware_concurrency()));
    std::vector<std::thread> pool;
    for (unsigned k = 0; k < threads; ++k) {
        pool.emplace_back([&, k] {
            for (int i = static_cast<int>(k); i < n; i += static_cast<int>(threads)) fn(i);
        });
    }
    for (auto& t : pool) t.join();
}

} // namespace

Trajectory integrateAttractor(AttractorSystem system, glm::vec3 start, float dt, int transientSteps, int keepSteps) {
    Trajectory out;
    glm::vec3 s = start;
    constexpr float kLimit = 1e4f; // a state this large is a diverging parameter set: stop rather than overflow
    for (int i = 0; i < transientSteps; ++i) {
        s = rk4(system, s, dt);
        if (!finite3(s) || glm::length(s) > kLimit) {
            out.finite = false;
            return out;
        }
    }
    out.points.reserve(static_cast<std::size_t>(std::max(0, keepSteps)));
    for (int i = 0; i < keepSteps; ++i) {
        s = rk4(system, s, dt);
        if (!finite3(s) || glm::length(s) > kLimit) {
            out.finite = false;
            break;
        }
        out.points.push_back(s);
    }
    return out;
}

std::vector<glm::vec3> resampleByArcLength(const std::vector<glm::vec3>& points, int maxSegments) {
    std::vector<glm::vec3> out;
    if (points.size() < 2 || maxSegments < 1) {
        return out;
    }
    std::vector<float> arc(points.size(), 0.0f);
    for (std::size_t i = 1; i < points.size(); ++i) {
        arc[i] = arc[i - 1] + glm::length(points[i] - points[i - 1]);
    }
    const float total = arc.back();
    if (!(total > 0.0f)) {
        return out;
    }
    const int n = maxSegments;
    out.reserve(static_cast<std::size_t>(n) + 1);
    std::size_t j = 1;
    for (int k = 0; k <= n; ++k) {
        const float target = total * static_cast<float>(k) / static_cast<float>(n);
        while (j + 1 < points.size() && arc[j] < target) ++j;
        const float a0 = arc[j - 1], a1 = arc[j];
        const float t = a1 > a0 ? std::clamp((target - a0) / (a1 - a0), 0.0f, 1.0f) : 0.0f;
        out.push_back(glm::mix(points[j - 1], points[j], t));
    }
    return out;
}

float tubeDistance(const AttractorTube& tube, glm::vec3 p) {
    float best = 1e30f;
    for (std::size_t i = 0; i + 1 < tube.points.size(); ++i) {
        float t = 0.0f;
        const float d = segmentDistance(p, glm::vec3(tube.points[i]), glm::vec3(tube.points[i + 1]), t);
        const float r = glm::mix(tube.points[i].w, tube.points[i + 1].w, t);
        best = std::min(best, d - r);
    }
    return best;
}

void bakeTubeVolume(AttractorTube& tube, int res) {
    tube.volumeRes = res;
    tube.volume.assign(static_cast<std::size_t>(res) * res * res, glm::vec2(1e4f, 0.0f));
    if (tube.points.size() < 2 || res < 2) {
        return;
    }
    const glm::vec3 lo = tube.boundsMin, hi = tube.boundsMax;
    const glm::vec3 cell = (hi - lo) / static_cast<float>(res - 1);
    // Coarse culling: per block of 8^3 voxels, only the segments that can be nearest to any voxel in it. A segment
    // whose distance to the block's centre exceeds (the best such distance + the block's diagonal * 2) cannot be.
    constexpr int B = 8;
    const int blocks = (res + B - 1) / B;
    const int nseg = static_cast<int>(tube.points.size()) - 1;
    parallelFor(blocks * blocks * blocks, [&](int bi) {
        const int bx = bi % blocks, by = (bi / blocks) % blocks, bz = bi / (blocks * blocks);
        const glm::vec3 c = lo + cell * (glm::vec3(bx, by, bz) * static_cast<float>(B) + 0.5f * (B - 1));
        const float diag = glm::length(cell) * static_cast<float>(B) * 0.5f;
        std::vector<float> dist(static_cast<std::size_t>(nseg));
        float nearest = 1e30f;
        for (int s = 0; s < nseg; ++s) {
            float t = 0.0f;
            dist[s] = segmentDistance(c, glm::vec3(tube.points[s]), glm::vec3(tube.points[s + 1]), t) -
                      std::max(tube.points[s].w, tube.points[s + 1].w);
            nearest = std::min(nearest, dist[s]);
        }
        std::vector<int> cand;
        for (int s = 0; s < nseg; ++s) {
            if (dist[s] <= nearest + 2.0f * diag + 1e-3f) cand.push_back(s);
        }
        for (int z = bz * B; z < std::min(res, bz * B + B); ++z) {
            for (int y = by * B; y < std::min(res, by * B + B); ++y) {
                for (int x = bx * B; x < std::min(res, bx * B + B); ++x) {
                    const glm::vec3 p = lo + cell * glm::vec3(x, y, z);
                    float best = 1e30f;
                    int bestSeg = 0;
                    for (int s : cand) {
                        float t = 0.0f;
                        const float d = segmentDistance(p, glm::vec3(tube.points[s]), glm::vec3(tube.points[s + 1]), t) -
                                        glm::mix(tube.points[s].w, tube.points[s + 1].w, t);
                        if (d < best) {
                            best = d;
                            bestSeg = s;
                        }
                    }
                    tube.volume[(static_cast<std::size_t>(z) * res + y) * res + x] =
                        glm::vec2(best, static_cast<float>(bestSeg));
                }
            }
        }
    });
}

} // namespace avgen::scene
