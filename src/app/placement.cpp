#include "app/placement.hpp"

#include <algorithm>
#include <cmath>

namespace avgen::app {
namespace {

// The same small hash the composer uses, for the same reason: a placement has to be reproducible
// from its seed, and std::mt19937 seeded per call is both slower and no more random for this.
float hash01(std::uint32_t seed, std::uint32_t salt) {
    std::uint32_t h = seed * 747796405u + salt * 2891336453u;
    h = ((h >> ((h >> 28) + 4)) ^ h) * 277803737u;
    h = (h >> 22) ^ h;
    return static_cast<float>(h & 0xffffffu) / static_cast<float>(0x1000000u);
}

glm::vec3 safeNormalize(glm::vec3 v, glm::vec3 fallback) {
    const float length = glm::length(v);
    return length > 1e-6f ? v / length : fallback;
}

// Two axes spanning the surface, so a brush paints along the ground rather than along the XZ plane.
// On a steep hillside those are very different, and painting on XZ puts half the brush underground.
void surfaceBasis(glm::vec3 normal, glm::vec3& u, glm::vec3& v) {
    const glm::vec3 reference =
        std::abs(normal.y) > 0.95f ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
    u = safeNormalize(glm::cross(reference, normal), glm::vec3(1.0f, 0.0f, 0.0f));
    v = safeNormalize(glm::cross(normal, u), glm::vec3(0.0f, 0.0f, 1.0f));
}

// `surface` is which way is out of the ground; `orientation` is which way the object stands. They
// are the same thing only when the object lies along the slope, and conflating them makes an object
// placed on a wall sink *downward* -- leaving it hanging in the air in front of the wall instead of
// bedded into it.
Placement makeOne(const PlacementSettings& settings, glm::vec3 position, glm::vec3 surface,
                  glm::vec3 orientation, std::uint32_t seed, std::uint32_t salt) {
    Placement p;
    p.normal = orientation;
    p.position = position - surface * settings.sink;
    p.yaw = hash01(seed, salt) * 6.2831853f * std::clamp(settings.yawJitter, 0.0f, 1.0f);
    const float jitter = std::clamp(settings.scaleJitter, 0.0f, 0.95f);
    p.scale = 1.0f + (hash01(seed, salt + 5000u) * 2.0f - 1.0f) * jitter;
    return p;
}
} // namespace

const char* placementModeName(PlacementMode mode) {
    switch (mode) {
    case PlacementMode::Single:
        return "single";
    case PlacementMode::Brush:
        return "brush";
    case PlacementMode::Cluster:
        return "cluster";
    case PlacementMode::Landmark:
        return "landmark";
    case PlacementMode::Eraser:
        return "eraser";
    case PlacementMode::Replace:
        return "replace";
    }
    return "single";
}

std::vector<Placement> planPlacements(const PlacementSettings& settings, glm::vec3 center,
                                      glm::vec3 normal, std::uint32_t seed) {
    std::vector<Placement> out;
    normal = safeNormalize(normal, glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::vec3 upright(0.0f, 1.0f, 0.0f);
    const glm::vec3 orientation = settings.alignToNormal ? normal : upright;

    switch (settings.mode) {
    case PlacementMode::Single:
        out.push_back(makeOne(settings, center, normal, orientation, seed, 1u));
        break;

    case PlacementMode::Landmark: {
        Placement p = makeOne(settings, center, normal, orientation, seed, 2u);
        // A landmark is not a big instance of the population -- it is a different decision -- so
        // the multiplier stacks on top of the jitter rather than replacing it.
        p.scale *= std::max(settings.landmarkScale, 0.01f);
        out.push_back(p);
        break;
    }

    case PlacementMode::Cluster: {
        glm::vec3 u;
        glm::vec3 v;
        surfaceBasis(normal, u, v);
        const int count = std::max(settings.clusterCount, 1);
        const float radius = std::max(settings.clusterRadius, 0.0f);
        const float clustering = std::clamp(settings.clustering, 0.0f, 1.0f);
        for (int i = 0; i < count; ++i) {
            const auto salt = static_cast<std::uint32_t>(100 + i * 13);
            const float angle = hash01(seed, salt) * 6.2831853f;
            // sqrt so the disc fills evenly. Without it a cluster is dense in the middle and thins
            // toward its edge, which reads as a target rather than as a patch of something growing.
            // `clustering` interpolates back toward that uneven fill on purpose: at 1 the sqrt is
            // dropped and the instances pile toward the centre, which is what "clumpy" means.
            const float u01 = hash01(seed, salt + 1u);
            const float spread = glm::mix(std::sqrt(u01), u01 * u01, clustering);
            const float distance = spread * radius;
            const glm::vec3 offset = (u * std::cos(angle) + v * std::sin(angle)) * distance;
            out.push_back(makeOne(settings, center + offset, normal, orientation, seed, salt + 2u));
        }
        break;
    }

    case PlacementMode::Eraser:
        // Nothing is laid out. The editor removes what the brush covers; the radius is the whole
        // of the eraser's geometry and it is not this function's to draw.
        break;

    case PlacementMode::Replace:
    case PlacementMode::Brush: {
        glm::vec3 u;
        glm::vec3 v;
        surfaceBasis(normal, u, v);
        const float radius = std::max(settings.brushRadius, 0.0f);
        const float spacing = std::max(settings.spacing, 1e-3f);
        // How many would fit if they packed perfectly, with a generous allowance for the fact that
        // dart throwing does not pack perfectly. The attempt budget is what bounds the loop; the
        // spacing test is what decides the result.
        const float density = std::clamp(settings.density, 0.0f, 1.0f);
        const auto packed = static_cast<int>(std::ceil((radius * radius) / (spacing * spacing) * 1.2f)) + 1;
        // Density scales how many of the available slots are filled, never the spacing: two plants
        // are never closer together than the spacing says, whatever the density. A density that
        // moved the spacing would make "thinner" and "more scattered" the same control, and they
        // are not -- a thin even meadow and a thin clumpy one look nothing alike.
        const int target = std::max(1, static_cast<int>(std::lround(static_cast<float>(packed) * density)));
        const int attempts = target * 12;
        for (int i = 0; i < attempts && static_cast<int>(out.size()) < target; ++i) {
            const auto salt = static_cast<std::uint32_t>(1000 + i * 7);
            const float angle = hash01(seed, salt) * 6.2831853f;
            const float distance = std::sqrt(hash01(seed, salt + 1u)) * radius;
            const glm::vec3 candidate = center + (u * std::cos(angle) + v * std::sin(angle)) * distance;
            // Dart throwing: reject anything too close to what is already down. A uniform scatter
            // clumps, and clumping is the one thing a brush must not do -- the user is already
            // saying where the density goes by moving the mouse, and a brush that clumps overrules
            // them.
            const bool tooClose = std::any_of(out.begin(), out.end(), [&](const Placement& p) {
                return glm::length(p.position - candidate) < spacing;
            });
            if (tooClose) {
                continue;
            }
            out.push_back(makeOne(settings, candidate, normal, orientation, seed, salt + 2u));
        }
        break;
    }
    }
    return out;
}

float normalisingScale(const assets::AssetDescriptor& asset, float height) {
    const float wanted = height > 0.0f ? height : asset.effectiveHeight();
    if (!(wanted > 0.0f) || !(asset.naturalSize.y > 1e-4f)) {
        return 1.0f;
    }
    return wanted / asset.naturalSize.y;
}

std::string uniquePlacementName(const std::string& assetId, const std::vector<std::string>& existing) {
    const std::string base = assetId.empty() ? std::string("asset") : assetId;
    if (std::find(existing.begin(), existing.end(), base) == existing.end()) {
        return base;
    }
    for (int i = 1; i < 100000; ++i) {
        std::string candidate = base + "_" + std::to_string(i);
        if (std::find(existing.begin(), existing.end(), candidate) == existing.end()) {
            return candidate;
        }
    }
    return base;
}

} // namespace avgen::app
