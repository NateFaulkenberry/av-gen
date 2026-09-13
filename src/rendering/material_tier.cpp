#include "rendering/material_tier.hpp"

#include <algorithm>

namespace avgen::rendering {

namespace {

// How cheap a tier is, so "never cheaper than" and "never more expensive than" are one comparison
// each rather than a table of special cases. Same shape as representation.cpp's `coarseness`, and
// for the same reason.
int cheapness(MaterialTier t) { return static_cast<int>(static_cast<std::uint8_t>(t)); }

MaterialTier clampTier(MaterialTier value, MaterialTier floorTier, MaterialTier worst) {
    // `floorTier` is a *quality* floor, so it is a ceiling on cheapness; `worst` is the same thing
    // stated by the policy rather than by the drawable.
    const int limit = std::min(cheapness(floorTier), cheapness(worst));
    return cheapness(value) <= limit ? value : static_cast<MaterialTier>(limit);
}

} // namespace

MaterialTierPolicy MaterialTierPolicy::forTier(QualityTier tier) {
    MaterialTierPolicy p;
    switch (tier) {
    case QualityTier::Preview:
        // The interactive/editor tier. Wider bands than realtime: an editor viewport is looked at
        // while something is being changed, and latency is what is being traded for.
        p.enabled = true;
        p.reducedRadius = 96.0f;
        p.flatRadius = 20.0f;
        break;
    case QualityTier::Realtime:
        p.enabled = true;
        break;
    case QualityTier::High:
        // The reference *live* picture. Assignment stays on -- turning it off entirely would make
        // High a different renderer from Realtime rather than a better-sampled one -- but the flat
        // rung, which is the one with a visible delta, is excluded by `worstTier`.
        p.enabled = true;
        p.reducedRadius = 24.0f;
        p.flatRadius = 0.0f;
        p.worstTier = MaterialTier::ReducedLights;
        break;
    case QualityTier::Offline:
        // §5.9. Not "generous thresholds" -- off, and forced off, so a later edit to the bands
        // cannot reach an offline render. A render is a deliverable and must not be a silently
        // lower-fidelity version of the preview it was approved from.
        p.enabled = false;
        p.forceTopTier = true;
        break;
    }
    return p;
}

MaterialTier MaterialTierSelector::select(const ImportanceRecord& record,
                                          const MaterialTierPolicy& policy) {
    if (!policy.enabled || policy.forceTopTier) {
        return MaterialTier::Full;
    }
    // Behind the camera is not this selector's business -- culling is somebody else's job, and
    // answering half of it here would be a second, disagreeing answer to the same question. A
    // drawable that reaches the shader at all is shaded on its size.
    const float radius = record.projectedRadius;
    MaterialTier chosen = MaterialTier::Full;
    if (radius <= policy.flatRadius) {
        chosen = MaterialTier::Flat;
    } else if (radius <= policy.reducedRadius) {
        chosen = MaterialTier::ReducedLights;
    }
    const MaterialTier floorTier = record.hero ? policy.heroFloor : MaterialTier::Flat;
    return clampTier(chosen, floorTier, policy.worstTier);
}

void MaterialTierSelector::select(std::span<const ImportanceRecord> records,
                                  const MaterialTierPolicy& policy, std::span<MaterialTier> out) {
    const std::size_t n = std::min(records.size(), out.size());
    for (std::size_t i = 0; i < n; ++i) {
        out[i] = select(records[i], policy);
    }
}

} // namespace avgen::rendering
