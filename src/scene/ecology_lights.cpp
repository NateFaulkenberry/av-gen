#include "scene/ecology_lights.hpp"

#include <algorithm>
#include <cmath>

namespace avgen::scene {

namespace {
// The light stands at this fraction of the pool's radius over the ground; its window reaches this
// multiple of it (see `glowPool`).
constexpr float kPoolHeight = 0.5f;
constexpr float kPoolWindow = 2.0f;
// A light fades in over the last quarter of score above the budget's cut, and out over the last 15%
// of the distance limit.
constexpr float kCutFadeBand = 0.25f;
constexpr float kDistanceFadeBand = 0.15f;

// 1 inside the distance limit's fade band, falling to 0 at the limit.
float distanceFade(const EcologyLightCandidate& c, const EcologyLightView& view) {
    const float distance = glm::length(c.position - view.eye);
    return std::clamp((view.farthest - distance) / (kDistanceFadeBand * view.farthest), 0.0f, 1.0f);
}
} // namespace

GlowPool glowPool(const world::GlowCluster& cluster, float lift, float reach) {
    const float organ = std::max(lift, 0.0f);
    const float radius = std::max({reach, cluster.radius, 2.0f * organ, 0.25f});
    const glm::vec3 ground = cluster.position - glm::vec3(0.0f, organ, 0.0f);
    GlowPool pool;
    pool.radius = radius;
    pool.position = ground + glm::vec3(0.0f, std::max(radius * kPoolHeight, organ), 0.0f);
    pool.range = radius * kPoolWindow;
    return pool;
}

float ecologyLightScore(const EcologyLightCandidate& c, const EcologyLightView& view) {
    if (c.power <= 0.0f || view.farthest <= 0.0f) {
        return 0.0f;
    }
    const glm::vec3 d = c.position - view.eye;
    const float d2 = glm::dot(d, d);
    if (d2 > view.farthest * view.farthest) {
        return 0.0f;
    }
    for (const glm::vec4& plane : view.planes) {
        if (glm::dot(glm::vec3(plane), c.position) + plane.w < -c.range) {
            return 0.0f; // the whole reach is off screen
        }
    }
    return distanceFade(c, view) * c.power / std::max(d2, c.range * c.range);
}

std::vector<EcologyLightChoice> chooseEcologyLights(std::span<const EcologyLightCandidate> candidates,
                                                    const EcologyLightView& view, std::size_t budget) {
    std::vector<EcologyLightChoice> scored;
    scored.reserve(candidates.size());
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        const float s = ecologyLightScore(candidates[i], view);
        if (s > 0.0f) {
            scored.push_back({static_cast<std::uint32_t>(i), s, distanceFade(candidates[i], view)});
        }
    }
    const auto better = [](const EcologyLightChoice& a, const EcologyLightChoice& b) {
        return a.score != b.score ? a.score > b.score : a.index < b.index;
    };
    float cut = 0.0f;
    if (scored.size() > budget) {
        std::nth_element(scored.begin(), scored.begin() + static_cast<std::ptrdiff_t>(budget), scored.end(), better);
        cut = scored[budget].score; // the best of the ones left out
        scored.resize(budget);
    }
    std::sort(scored.begin(), scored.end(), better);
    if (cut > 0.0f) {
        for (EcologyLightChoice& c : scored) {
            c.weight *= std::clamp((c.score / cut - 1.0f) / kCutFadeBand, 0.0f, 1.0f);
        }
    }
    return scored;
}

float layerGlowStrength(const world::ScatterLayer& layer, float gain) {
    const glm::vec3 colour = layer.emissiveColor;
    const float peak = std::max({colour.x, colour.y, colour.z, 0.0f});
    return std::max(layer.emissiveIntensity, 0.0f) * peak * (1.0f - std::clamp(layer.emissiveSparsity, 0.0f, 1.0f)) *
           std::max(gain, 0.0f);
}

} // namespace avgen::scene
