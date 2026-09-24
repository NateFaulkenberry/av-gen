#include "world/effects/effect_lights.hpp"

#include "world/effects/effect_instance.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>

namespace avgen::world {

float projectedIntensity(const EffectLight& light, const glm::vec3& cameraPosition) {
    const glm::vec3 d = light.position - cameraPosition;
    const float floor = std::max(light.range, 1e-3f);
    const float distanceSq = std::max(glm::dot(d, d), floor * floor);
    return std::max(light.intensity, 0.0f) / distanceSq;
}

void resolveEffectLights(std::span<EffectLightRequest> requests, const glm::vec3& cameraPosition,
                         EffectLightFrame& out, std::span<EffectStatus> status,
                         std::span<std::string> reasons) {
    out.count = 0;
    out.dropped = 0;
    // Stale slots are cleared rather than left: the renderer reads `count`, but a frame block that
    // carries last frame's light past its count is one refactor from being drawn.
    for (EffectLight& l : out.lights) {
        l = EffectLight{};
    }
    if (requests.empty()) {
        return;
    }
    // Priority first (a type that matters more keeps its light), then how bright the light looks
    // from where the camera is, then stack order. The last key makes the order total, so the same
    // frame always keeps the same lights -- a ranking that depended on sort stability or on the
    // order builders happened to append in would flicker a light on and off between two renders
    // of one second.
    std::sort(requests.begin(), requests.end(), [&](const EffectLightRequest& a, const EffectLightRequest& b) {
        if (a.priority != b.priority) {
            return a.priority < b.priority;
        }
        const float pa = projectedIntensity(a.light, cameraPosition);
        const float pb = projectedIntensity(b.light, cameraPosition);
        if (pa != pb) {
            return pa > pb;
        }
        return a.order < b.order;
    });
    const std::size_t kept = std::min(requests.size(), kEffectLightBudget);
    for (std::size_t i = 0; i < kept; ++i) {
        out.lights[i] = requests[i].light;
    }
    out.count = static_cast<std::uint32_t>(kept);
    out.dropped = static_cast<std::uint32_t>(requests.size() - kept);

    // The losers. `Partial`, not `Dropped`: the effect's own contribution drew; only its light is
    // missing, and saying `Dropped` would send somebody looking for why the whole effect vanished.
    // A status already worse than Drawn (a builder dropped the effect outright) is left alone.
    for (std::size_t i = kept; i < requests.size(); ++i) {
        const std::uint32_t at = requests[i].instance;
        if (at < status.size() && status[at] == EffectStatus::Drawn) {
            status[at] = EffectStatus::Partial;
        }
        if (at < reasons.size()) {
            // Formatted into a stack buffer and assigned: `assign` reuses the string's storage, so a
            // reason said every frame allocates once, not per frame.
            std::array<char, 192> buffer{};
            const auto result =
                fmt::format_to_n(buffer.data(), buffer.size(),
                                 "Its light did not fit: the effect light budget ({}) is full, and {} "
                                 "request(s) outranked it this frame.",
                                 kEffectLightBudget, kept);
            reasons[at].assign(buffer.data(), std::min<std::size_t>(result.size, buffer.size()));
        }
    }
}

} // namespace avgen::world
