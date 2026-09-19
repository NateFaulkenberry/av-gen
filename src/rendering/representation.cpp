#include "rendering/representation.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace avgen::rendering {

namespace {

// shaders/cull.wgsl's `instanceHash` / `ladderHash`, transcribed. Transcribed rather than
// approximated: the whole point of the spread is that an entity and the scattered copy of the same
// asset beside it pick their thresholds from the same sequence, and a second hash that is merely
// similar would make them disagree in exactly the band where it matters.
float instanceHash(std::uint32_t i) {
    std::uint32_t h = i * 0x9E3779B1u;
    h ^= h >> 15u;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12u;
    return static_cast<float>(h >> 8u) / 16777216.0f;
}

float ladderHash(std::uint32_t i) {
    return instanceHash(i * 0x9E3779B9u + 0x85EBCA6Bu);
}

// How coarse a representation is, so "never coarser than" is one comparison rather than a table of
// special cases.
int coarseness(Representation r) {
    switch (r) {
    case Representation::FullMesh: return 0;
    case Representation::MeshLod: return 1;
    case Representation::HlodProxy: return 2;
    case Representation::Impostor: return 3;
    case Representation::Culled: return 4;
    }
    return 0;
}

} // namespace

RepresentationPolicy RepresentationPolicy::forTier(QualityTier tier) {
    RepresentationPolicy p;
    switch (tier) {
    case QualityTier::Preview:
        // The interactive tier: the same bands, pulled in so a preview trades detail for latency.
        // The px/triangle target does not move -- it is a measured property of the hardware, not a
        // taste setting, and moving it would mean claiming a different machine.
        p.proxyRadius = 60.0f;
        p.impostorRadius = 12.0f;
        p.cullRadius = 3.0f;
        break;
    case QualityTier::Realtime:
        break;
    case QualityTier::High:
        p.proxyRadius = 24.0f;
        p.impostorRadius = 5.0f;
        p.cullRadius = 1.0f;
        break;
    case QualityTier::Offline:
        // §5.9. Not "generous thresholds" -- off. A render is a deliverable and must not be a
        // silently lower-fidelity version of the preview it was approved from.
        p.forceTopRepresentation = true;
        p.hysteresis = 0.0f;
        p.spread = 0.0f;
        break;
    }
    return p;
}

RepresentationChoice RepresentationSelector::decide(const ImportanceRecord& record,
                                                    std::span<const LodRung> rungs,
                                                    const RepresentationPolicy& policy,
                                                    const RepresentationChoice& previous) {
    RepresentationChoice out;
    if (!policy.enabled || policy.forceTopRepresentation) {
        out.changed = !out.sameAs(previous);
        return out;
    }

    const float h = std::clamp(policy.hysteresis, 0.0f, 0.5f);
    const float spreadAmount = std::clamp(policy.spread, 0.0f, 0.5f);
    const float spread = 1.0f + (ladderHash(record.index) - 0.5f) * spreadAmount;

    // ---- which kind: projected radius decides what a thing can still look like ------------------
    //
    // The dead zone is one-sided in the direction that matters: it takes a *larger* radius to come
    // back than it took to leave, so a drawable sitting on a threshold stays where it is. Same
    // construction as cull.wgsl's `minScreenRadius` test.
    const auto atOrBelow = [&](float threshold, bool already) {
        const float t = threshold * spread;
        const float limit = already ? t * (1.0f + h) : t * (1.0f - h);
        return record.projectedRadius <= limit;
    };
    const int was = coarseness(previous.kind);
    Representation kind = Representation::FullMesh;
    if (atOrBelow(policy.cullRadius, was >= coarseness(Representation::Culled))) {
        kind = Representation::Culled;
    } else if (atOrBelow(policy.impostorRadius, was >= coarseness(Representation::Impostor))) {
        kind = Representation::Impostor;
    } else if (atOrBelow(policy.proxyRadius, was >= coarseness(Representation::HlodProxy))) {
        kind = Representation::HlodProxy;
    }

    // A hero is never demoted past its floor (ADR-104 designates it; this honours the designation).
    if (record.hero && coarseness(kind) > coarseness(policy.heroFloor)) {
        kind = policy.heroFloor;
    }

    if (kind == Representation::HlodProxy || kind == Representation::Impostor ||
        kind == Representation::Culled) {
        out.kind = kind;
        out.lodLevel = 0;
        out.changed = !out.sameAs(previous);
        return out;
    }

    // ---- which rung: pixels per triangle decides cost -------------------------------------------
    //
    // The coarsest rung still at or under the target, which is the cheap band's own edge. Chosen
    // this way round rather than "the fewest triangles" because §4.5 measured that the fewest
    // triangles is *not* the cheapest point: two screen-filling triangles cost twice what 2,048 do.
    std::uint8_t level = 0;
    if (rungs.size() > 1) {
        float best = -std::numeric_limits<float>::infinity();
        bool accepted = false;
        const bool previousWasMesh = previous.kind == Representation::FullMesh ||
                                     previous.kind == Representation::MeshLod;
        for (std::size_t k = 0; k < rungs.size() && k < 256; ++k) {
            const float ppt =
                ImportanceEvaluator::pixelsPerTriangleFor(record, rungs[k].surfaceArea, rungs[k].triangles);
            if (ppt <= 0.0f) {
                continue; // a rung with no triangles is not a rung of the mesh ladder
            }
            const bool already = previousWasMesh && static_cast<std::size_t>(previous.lodLevel) >= k;
            // The quality floor (ADR-351), applied before the cost rule gets a say. The dead zone
            // runs the other way round here from the one on the cost limit, and for the same
            // reason: a rung this drawable is *already* on is allowed a little more error before it
            // is taken away, so the boundary does not strobe.
            const float errorLimit = policy.maxScreenError * (already ? (1.0f + h) : (1.0f - h));
            if (rungs[k].error > 0.0f && record.pixelsPerUnit > 0.0f &&
                rungs[k].error * record.pixelsPerUnit > errorLimit) {
                continue;
            }
            const float limit = policy.targetPixelsPerTriangle * spread *
                                (already ? (1.0f + h) : (1.0f - h));
            if (ppt <= limit && ppt > best) {
                best = ppt;
                level = static_cast<std::uint8_t>(k);
                accepted = true;
            }
        }
        if (!accepted) {
            level = 0; // every rung is already coarser than the target: the finest is the closest
        }
    }
    out.kind = level == 0 ? Representation::FullMesh : Representation::MeshLod;
    out.lodLevel = level;
    out.changed = !out.sameAs(previous);
    return out;
}

RepresentationChoice RepresentationSelector::select(const ImportanceRecord& record,
                                                    std::span<const LodRung> rungs,
                                                    const RepresentationPolicy& policy) {
    const RepresentationChoice prev = previous(record.index);
    RepresentationChoice choice = decide(record, rungs, policy, prev);

    // Whether the dead zone is what produced this answer, stated rather than inferred: run the same
    // decision with no dead zone and see if it differs. Only when hysteresis is actually on -- the
    // second evaluation is not paid by a configuration that is not using it.
    if (policy.hysteresis != 0.0f) {
        RepresentationPolicy flat = policy;
        flat.hysteresis = 0.0f;
        const RepresentationChoice without = decide(record, rungs, flat, prev);
        choice.held = !choice.sameAs(without);
    }

    const std::size_t i = record.index;
    if (previous_.size() <= i) {
        previous_.resize(i + 1);
        seen_.resize(i + 1, false);
    }
    previous_[i] = choice;
    seen_[i] = true;
    return choice;
}

void RepresentationSelector::reset() {
    previous_.clear();
    seen_.clear();
}

RepresentationChoice RepresentationSelector::previous(std::uint32_t index) const {
    if (index >= previous_.size() || !seen_[index]) {
        return RepresentationChoice{};
    }
    RepresentationChoice p = previous_[index];
    // `changed` and `held` describe the frame they were produced in, not this one. Carrying them
    // forward would make "did it change" mean "did it change at some point", which is exactly the
    // kind of quietly accumulating flag a transition system would then act on forever.
    p.changed = false;
    p.held = false;
    return p;
}

} // namespace avgen::rendering
