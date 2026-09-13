#pragma once

// MaterialTierSelector (renderer upgrade, Deliverable 5 §5.3/§5.6; Phase D, ADR-133 to ADR-136).
//
// How expensively one drawable is *shaded* this frame. Its sibling, RepresentationSelector, decides
// what geometry is drawn; the two are deliberately orthogonal (§5.3), read the same
// ImportanceRecord, and answer to the same QualityPolicy.
//
// ---- why this reads a different number from the representation selector ------------------------
//
// importance.hpp states the split and it is the whole reason there are two selectors: *geometry
// wants pixels per triangle, shading wants pixels*, and those two orderings genuinely disagree. A
// terrain chunk and a fern at the same projected radius have triangle counts four orders of
// magnitude apart, so the representation selector is right to ignore radius for its rung choice --
// and the material tier is right to ignore pixels per triangle, because a fragment costs the same
// whichever triangle produced it. What a shading budget can spend less on is *how many fragments*
// there are, and that is projected area.
//
// So the bands here are in projected radius, which is the square root of the thing that actually
// scales, and is the same measure `cull.wgsl` and the representation selector's kind bands use. A
// tier is per *drawable*, never per fragment: ADR-118 measured a lane-varying branch around a loop
// with a dependent texture load in it running 4.4% *slower* than the loop it was skipping, so a
// per-fragment tier would be a pessimisation rather than an optimisation. A per-draw tier makes
// every branch that reads it wave-uniform by construction.
//
// ---- what a rung actually removes ---------------------------------------------------------------
//
// The ladder is in render_quality.hpp (`MaterialTier`) so the shader's table and the CPU's cannot
// live in two places. Restated here because a selector that cannot say what it is buying is a
// selector nobody can calibrate:
//
//   Full          every light in the froxel, every shadow term, IBL, normal map, AO.
//   ReducedLights the clustered local-light loop capped at `QualitySettings::reducedTierLocalLights`
//                 and no contact march on local lights. Directional lights are untouched -- there
//                 are at most three, they reach every fragment, and dropping one is a lighting
//                 change and not a cost reduction.
//   Flat          no shadow term at all (no mask read, no cascade lookup, no blocker search, no
//                 contact march), no specular, no AO, no IBL, and a tighter local-light cap.
//
// ---- what this deliberately does not do ----------------------------------------------------------
//
// No hysteresis and no history. The representation selector needs a dead zone because a rung change
// moves a silhouette; a tier change moves a shading term smoothly, and the thing it is most likely
// to be visible as -- a local light popping in and out of a distant object's cap -- is not fixed by
// remembering last frame's tier either. Off by default beats a mechanism nobody measured, and a
// frame that depends on how the camera arrived is a promise this engine does not make (§5.9).

#include "rendering/importance.hpp"
#include "rendering/render_quality.hpp"

#include <cstdint>
#include <span>

namespace avgen::rendering {

struct MaterialTierPolicy {
    // The rollback the plan asks the tests to assert: every drawable shades at Full, which is byte
    // for byte the pre-ADR-133 renderer.
    bool enabled = false;
    // §5.9: offline forces Full for every object whatever its screen size. Set from the quality
    // tier; never from a scene file.
    bool forceTopTier = false;

    // ---- the bands, in projected radius (px) ----------------------------------------------------
    //
    // At or below `reducedRadius` a drawable shades at ReducedLights; at or below `flatRadius`, at
    // Flat. Placeholders calibrated in ADR-136 against the visual gate, not proposals: the numbers
    // that matter are the ones a captured frame survives.
    float reducedRadius = 64.0f;
    float flatRadius = 12.0f;

    // A hero (ADR-104) is never shaded below this. A hero is the subject of the shot and the saving
    // that costs the shot is not a saving.
    MaterialTier heroFloor = MaterialTier::Full;

    // The worst tier this policy will ever return, whatever the bands say. `High` uses it to keep
    // the flat rung out of the reference live picture without needing a second band table.
    MaterialTier worstTier = MaterialTier::Flat;

    [[nodiscard]] static MaterialTierPolicy forTier(QualityTier tier);
};

class MaterialTierSelector {
public:
    // The whole decision. Pure: no state, no memory, no device -- so a test can assert it exactly,
    // which is the reason `RepresentationSelector::decide` exists as well.
    [[nodiscard]] static MaterialTier select(const ImportanceRecord& record,
                                             const MaterialTierPolicy& policy);

    // A batch, in order. `out` must be at least as long as `records`.
    static void select(std::span<const ImportanceRecord> records, const MaterialTierPolicy& policy,
                       std::span<MaterialTier> out);
};

} // namespace avgen::rendering
