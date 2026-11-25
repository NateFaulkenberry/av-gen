#include "rendering/cloud_march_quality.hpp"

#include "rendering/render_quality.hpp"
#include "scene/clouds.hpp"

#include <algorithm>
#include <cmath>

namespace avgen::rendering {

CloudMarchQuality cloudMarchQuality(const scene::Clouds& c, const QualitySettings* q) {
    CloudMarchQuality m;
    m.resolution = std::clamp(c.resolution, 0.25f, 1.0f);
    m.steps = c.steps;
    m.lightSteps = c.lightSteps;
    m.stepGrowth = c.stepGrowth;
    m.erosionOctaves = c.erosionOctaves;
    if (q == nullptr) {
        return m;
    }
    // ADR-2134: each lever only ever lowers, and at 1 (or 3 octaves) leaves the authored value untouched -- the same
    // float, not one recomputed through it -- so a frame with neutral settings is the frame before this ADR.
    if (q->cloudResolutionScale < 1.0f) {
        m.resolution = std::max(m.resolution * std::max(q->cloudResolutionScale, 0.25f), 0.125f);
    }
    if (q->cloudStepScale < 1.0f && m.steps > 8) {
        const int authored = m.steps;
        m.steps = std::max(8, static_cast<int>(std::lround(static_cast<float>(authored) * q->cloudStepScale)));
        // The v2 march's steps grow with distance (at least `stepGrowth` of it), so a march with fewer of them would
        // stop short and the far cloud would go. Its reach grows as (1 + growth)^steps; the growth that reaches as far
        // in the steps left is (1 + growth)^(authored / steps) - 1. The even march divides its span by the count, so it
        // reaches as far already.
        if (!c.evenSteps && m.stepGrowth > 0.0f && m.steps < authored) {
            m.stepGrowth = std::pow(1.0f + m.stepGrowth, static_cast<float>(authored) / static_cast<float>(m.steps)) -
                           1.0f;
        }
    }
    if (q->cloudLightStepScale < 1.0f && m.lightSteps > 1) {
        m.lightSteps =
            std::max(1, static_cast<int>(std::lround(static_cast<float>(m.lightSteps) * q->cloudLightStepScale)));
    }
    if (q->cloudErosionOctaves < 3u) {
        m.erosionOctaves = std::min(m.erosionOctaves, static_cast<int>(std::max(q->cloudErosionOctaves, 1u)));
    }
    return m;
}

} // namespace avgen::rendering
