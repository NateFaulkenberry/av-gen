#pragma once

// QualityPolicy (renderer upgrade, Deliverable 5 §5.6 and §5.8; Phase D, ADR-134).
//
// **The one place editor/realtime/high/offline differ.** Before this, a mode was a pile of
// independently-set fields: a `QualitySettings` on the renderer, a `RepresentationPolicy` the
// caller built, a `MaterialTierPolicy` nobody built yet, and a render scale that did not exist. Any
// one of them could be set without the others, so "why did this look different in a render?" had
// four places to look and no answer that was true by construction.
//
// A QualityPolicy is one object derived from one enum. `QualityPolicy::forTier(tier)` is the whole
// definition of a mode, and every consumer takes the object rather than the enum, so a subsystem
// cannot ask "which tier is this?" and answer it its own way. That is the §49 requirement -- every
// quality reduction explicit, measurable, policy-exposed and general -- stated as a type.
//
// ---- the four modes ------------------------------------------------------------------------------
//
//   Preview   the editor viewport: favour latency. Wider tier bands, fewer samples, a render scale
//             below 1 is available here and nowhere else by default.
//   Realtime  the shipping interactive mode: a 16.67 ms budget, representation on, tiers on, a
//             *fixed* render scale.
//   High      the reference live picture: full-resolution shadow mask and AO, four cascades, tier
//             assignment still on but never reaching the flat rung.
//   Offline   a deliverable. No budget, representation forced to the top, material tier forced to
//             Full, render scale forced to 1, and no temporal shortcut.
//
// ---- offline is the invariant, not a preset --------------------------------------------------------
//
// §5.9 and risk 4: *the failure this prevents is a render that is silently lower fidelity than the
// preview it was approved from.* So Offline does not merely choose generous values -- it sets the
// `force` flags that make the selectors return the top answer regardless of their own bands, and
// `assertOfflineIsUncompromised()` states the whole of that invariant in one place a test can call.
// A future edit that widens a band cannot reach an offline render, and a future edit that reaches
// it fails a test rather than a shot.
//
// ---- render scale (§34) ------------------------------------------------------------------------
//
// Fixed per tier. Dynamic resolution is explicitly deferred and this is the thing it was deferred
// *behind*; the evidence against doing it first is in QualitySettings::renderScale.

#include "rendering/material_tier.hpp"
#include "rendering/render_quality.hpp"
#include "rendering/representation.hpp"

namespace avgen::rendering {

struct QualityPolicy {
    QualityTier tier = QualityTier::Realtime;
    // §18: the per-subsystem sample, resolution and history budgets -- shadows, AO, contact march,
    // SDF steps, the froxel path -- and the material-tier light budgets the shader reads.
    QualitySettings settings{};
    // Which geometry a drawable is drawn as (Phase C, ADR-122 to ADR-125).
    RepresentationPolicy representation{};
    // How expensively it is shaded (Phase D, ADR-133).
    MaterialTierPolicy materialTier{};

    [[nodiscard]] static QualityPolicy forTier(QualityTier t) {
        QualityPolicy p;
        p.tier = t;
        p.settings = QualitySettings::forTier(t);
        p.representation = RepresentationPolicy::forTier(t);
        p.materialTier = MaterialTierPolicy::forTier(t);
        // The two tier tables state material-tier assignment twice -- QualitySettings so the frame
        // uniform and the A/B arms can reach it, MaterialTierPolicy so the selector can. They are
        // reconciled here, once, in the direction of the policy: a selector that is off must not
        // leave the shader's forced tier pointing somewhere else.
        p.settings.materialTiers = p.materialTier.enabled && !p.materialTier.forceTopTier;
        if (!p.settings.materialTiers) {
            p.settings.forcedMaterialTier = MaterialTier::Full;
        }
        return p;
    }

    // §5.8: the resolution the scene is rendered at, from a requested output size. One function, so
    // no caller invents its own rounding. Clamped to at least one pixel; a scale above 1 is
    // supersampling and is allowed, because an offline render may legitimately ask for it.
    struct RenderSize {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
    };
    [[nodiscard]] RenderSize renderSize(std::uint32_t outputWidth, std::uint32_t outputHeight) const {
        const float s = settings.renderScale > 0.0f ? settings.renderScale : 1.0f;
        const auto scaled = [s](std::uint32_t v) {
            const float f = static_cast<float>(v) * s + 0.5f;
            return f < 1.0f ? 1u : static_cast<std::uint32_t>(f);
        };
        return {scaled(outputWidth), scaled(outputHeight)};
    }

    // Every promise §5.9 makes about an offline render, in one predicate. Called by the test that
    // guards risk 4; written as a function rather than as a list in a test so the promise lives
    // next to the thing that has to keep it.
    [[nodiscard]] bool assertOfflineIsUncompromised() const {
        if (tier != QualityTier::Offline) {
            return true;
        }
        return representation.forceTopRepresentation && representation.hysteresis == 0.0f &&
               representation.spread == 0.0f && materialTier.forceTopTier &&
               !materialTier.enabled && !settings.materialTiers &&
               settings.forcedMaterialTier == MaterialTier::Full && settings.renderScale == 1.0f &&
               settings.shadowMaskScale == 1.0f && settings.aoResolutionScale == 1.0f;
    }
};

} // namespace avgen::rendering
