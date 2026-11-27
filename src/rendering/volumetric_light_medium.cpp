#include "rendering/volumetric_light_medium.hpp"

#include "scene/scene.hpp"

#include <algorithm>
#include <cmath>

namespace avgen::rendering {

const char* volumetricLightDeclineName(VolumetricLightDecline d) {
    switch (d) {
    case VolumetricLightDecline::None: return "active";
    case VolumetricLightDecline::Off: return "off";
    case VolumetricLightDecline::NoAir: return "no air (volumeDensity 0)";
    case VolumetricLightDecline::DensityField: return "the air names a density field, which the froxel grid does not carry";
    case VolumetricLightDecline::ColorField: return "the air names a colour field, which the froxel grid does not carry";
    }
    return "off";
}

float airDensityAt(const scene::Environment& env, const glm::vec3& p, float distanceFromEye) {
    // `fogHeightProfile` (height_fog.wgsl), the same expression on the CPU.
    const float b = std::max(env.fogHeightFalloff, 0.0f);
    const float upper = std::clamp(env.fogUpperDensity, 0.0f, 1.0f);
    const float curve = std::clamp(env.fogHeightCurve, 0.0f, 1.0f);
    const float d = std::max(0.0f, p.y - env.fogHeight);
    const float falling = std::exp(-b * d);
    const float capped = std::max(0.0f, 1.0f - b * d * 0.5f);
    const float shape = falling + (capped * capped - falling) * curve;
    float density = std::max(env.volumeDensity, 0.0f) * (upper + (1.0f - upper) * shape);
    const float horizon = std::clamp(env.horizonDensity, 0.0f, scene::kHorizonDensityMax);
    if (horizon > 0.0f) {
        density *= 1.0f + horizon * distanceFromEye * 0.001f;
    }
    return std::max(density, 0.0f);
}

glm::uvec3 volumetricLightGrid(std::uint32_t width, std::uint32_t height, const QualitySettings& q) {
    const float tile = std::clamp(q.volumetricLightTile, 2.0f, 64.0f);
    const auto axis = [&](std::uint32_t v) {
        return std::max<std::uint32_t>(static_cast<std::uint32_t>(std::ceil(static_cast<float>(v) / tile - 1e-4f)), 1);
    };
    return {std::min(axis(width), 512u), std::min(axis(height), 512u),
            std::clamp<std::uint32_t>(q.volumetricLightSlices, 8, 256)};
}

VolumetricLightDecline volumetricLightDecline(const scene::Scene& scene) {
    const scene::Environment& env = scene.environment;
    if (!env.volumetricLight.enabled) {
        return VolumetricLightDecline::Off;
    }
    if (env.volumeDensity <= 0.0f) {
        return VolumetricLightDecline::NoAir;
    }
    if (!env.volumeDensityField.empty()) {
        return VolumetricLightDecline::DensityField;
    }
    if (!env.volumeColorField.empty() && env.volumeEmission > 0.0f) {
        return VolumetricLightDecline::ColorField;
    }
    return VolumetricLightDecline::None;
}

} // namespace avgen::rendering
