#pragma once

// Quality tiers (ADR-035). A tier scales sample counts, resolutions and history lengths only:
// the scene, its parameters and its determinism are identical across tiers, so an offline render
// of a world matches the preview frame by frame except in noise and resolution of the auxiliary
// passes. The tier is plain data so tests, the render job and the UI can all set it.

#include <cstdint>
#include <string_view>

namespace avgen::rendering {

// Preview is the tier Deliverable 5 SS5.8 calls "editor"; `qualityTierFromName` accepts both.
enum class QualityTier : std::uint8_t { Preview, Realtime, High, Offline };

[[nodiscard]] constexpr const char* qualityTierName(QualityTier tier) {
    switch (tier) {
    case QualityTier::Preview: return "preview";
    case QualityTier::Realtime: return "realtime";
    case QualityTier::High: return "high";
    case QualityTier::Offline: return "offline";
    }
    return "realtime";
}

[[nodiscard]] constexpr bool qualityTierFromName(std::string_view name, QualityTier& out) {
    // "editor" is the name Deliverable 5 §5.8 gives the interactive tier and "preview" is the name
    // this enum has always had. They are the same tier; accepting both costs one line and means a
    // reader of the plan can type what the plan says.
    if (name == "editor") {
        out = QualityTier::Preview;
        return true;
    }
    for (const auto tier : {QualityTier::Preview, QualityTier::Realtime, QualityTier::High, QualityTier::Offline}) {
        if (name == qualityTierName(tier)) {
            out = tier;
            return true;
        }
    }
    return false;
}

// ---- material tiers (Phase D, ADR-133) ---------------------------------------------------------
//
// How expensively one *draw* is shaded, as opposed to how finely the frame's shared passes are
// sampled. The two are orthogonal on purpose (Deliverable 5 §5.3): an object can be geometrically
// simplified without being shaded cheaply and the reverse, and conflating them is how quality
// settings become a single unusable slider.
//
// The ladder is monotone -- every rung removes work the rung above it did and adds nothing -- and
// it is deliberately short. Risk 7 in the plan is shader variant explosion; three rungs selected by
// a *uniform* value inside one shader is not a variant at all, which is the other reason for this
// shape. ADR-118 is the reason it must be uniform: a lane-varying branch around a loop with a
// dependent texture load in it measured 4.4% *slower* than the loop it skipped, so a per-fragment
// tier would be a pessimisation. A tier is per draw, so the branch is wave-uniform by construction.
enum class MaterialTier : std::uint8_t {
    Full = 0,          // every light in the froxel, every shadow term, IBL, normal map, AO
    ReducedLights = 1, // the local-light loop capped; no contact march on local lights
    Flat = 2,          // no shadow terms, no specular, no AO, no IBL, the local cap tightened
};

[[nodiscard]] constexpr const char* materialTierName(MaterialTier tier) {
    switch (tier) {
    case MaterialTier::Full: return "full";
    case MaterialTier::ReducedLights: return "reduced";
    case MaterialTier::Flat: return "flat";
    }
    return "full";
}

// Everything a tier scales. Resolutions are in texels, counts in samples/steps.
struct QualitySettings {
    std::uint32_t shadowResolution = 2048; // one cascade / spot map, square
    std::uint32_t cascadeCount = 3;

    // ADR-112: the largest world size one shadow-map texel of the *coarsest* cascade may cover, in
    // metres, at the reference resolution `kShadowRangeReference` (2048). The shadowed range is
    // chosen to honour it -- see `directionalShadowRange`.
    //
    // Read it as "how small a thing may be and still cast": a caster has to be a few texels across
    // to survive the PCF filter, so 8 cm is about a 30 cm object at the far end of the range, which
    // is a rock or a fence post. Raising it lengthens the range and coarsens the far shadows;
    // lowering it does the reverse.
    //
    // A tier with a smaller or larger map keeps the same shadowed *range* and gets a proportionally
    // coarser or finer texel. Where the shadows stop is composition, and it must not move between a
    // preview and a final.
    //
    // The range is the only lever on the coarsest texel there is. A camera frustum widens linearly
    // with distance, so the last cascade's world width is proportional to how far it reaches
    // whatever the split scheme does, and its texel is that width over the resolution. Measured on
    // the real fit at a fixed range, the coarsest texel moves by under 15% as the split lambda
    // sweeps 0.5 to 1.0, and by under 15% between two, three and four cascades.
    //
    // Zero switches the rule off and restores the pre-ADR-112 range, which was three scene radii.
    float shadowTexelTarget = 0.08f;

    std::uint32_t shadowPcfTaps = 12;
    // NOT READ. The §15 parity audit grepped every field here for a reader and found none: the
    // How many taps the PCSS blocker search takes, through `ShadowUniforms::info2.x` (ADR-227).
    // Its own budget rather than the filter's, because they are two costs: the search is
    // uninterpolated `textureLoad`s over a fixed radius and ADR-111 measured it as the largest
    // single contributor to the shadow mask's residual, while the filter is hardware comparison
    // samples over a radius the search chose. Clamped to 1..32 where it is written.
    //
    // Only the High tier sets the two differently today (20 PCF, 16 blocker), so this moves one
    // tier's picture and no other.
    std::uint32_t pcssBlockerTaps = 12;
    bool softShadows = true;               // percentage-closer soft shadows for the key light
    std::uint32_t contactSteps = 12;       // screen-space contact-shadow march
    bool contactShadows = true;
    std::uint32_t aoSlices = 3;            // GTAO direction slices
    std::uint32_t aoStepsPerSlice = 6;
    bool ambientOcclusion = true;
    float aoResolutionScale = 0.5f;        // half resolution + bilateral upsample
    std::uint32_t aoHistoryFrames = 8;     // temporal accumulation length
    // ADR-394: the fraction of the scene's resolution the temporal history ring is stored at.
    // Placed here beside `aoHistoryFrames` and `volumeResolutionScale` because those two are the
    // existing precedents for exactly this pair -- a temporal accumulation count and a
    // fraction-of-scene-resolution auxiliary buffer -- and the header's own opening sentence has
    // always said a tier scales "sample counts, resolutions and history lengths".
    //
    // Halving the linear resolution quarters the memory, and the ring's contents are about to be
    // blurred, smeared or advected, so this is the cheapest lever the family has. The *length* of
    // the history is NOT a tier setting: it is the effect's declared bound, and shortening it
    // between a preview and a final would change the picture rather than its sampling. §18 allows
    // a tier to scale resolution and sample counts; it does not allow it to remove an artistic
    // control, and how far an echo reaches is one.
    float temporalHistoryScale = 0.5f;
    // ADR-087: the fraction of the scene's resolution the directional lights' combined shadow term
    // (cascade lookup + contact march) is computed at, before a bilateral upsample in the lit
    // pass. 1.0 means "no mask pass": the lit pass computes the term per pixel, exactly as it did
    // before the mask existed, which is what keeps offline renders unchanged.
    float shadowMaskScale = 0.5f;

    // ADR-255, DIAGNOSTIC ONLY: build the mask at full resolution *and let the lit pass read it*,
    // whatever `shadowMaskScale` says. No tier sets this and nothing ships with it. It exists so
    // the question ADR-087 answered by assertion -- whether the mask is "exactly the combined
    // visibility the lit pass would otherwise compute" -- can be answered by a difference between
    // two frames instead. Reached as `--quality-arm maskconsume`.
    bool shadowMaskFullConsume = false;

    // ADR-139: the fraction of the scene's resolution the volumetric march (shaders/volume.wgsl
    // `fs_volume`) runs at, before the depth-aware upsample the composite pass does. 0.5 is the
    // half-resolution march ADR-032 shipped with and is what every tier below High still uses.
    // 1.0 means "march per pixel": the composite's bilinear footprint collapses to the one texel
    // under the pixel with weight 1, so the upsample becomes an exact copy rather than a filter.
    //
    // This is the volumetric equivalent of `aoResolutionScale` and `shadowMaskScale`, and it is
    // here rather than in the scene because it is a *quality* decision and not a compositional
    // one: the fog a scene authors -- its density, colour, height and extent -- is identical at
    // every scale, and only how finely it is sampled moves. A scene may not set it (§49: a
    // scene-specific quality reduction is the thing that rule forbids).
    float volumeResolutionScale = 0.5f;
    // ADR-139: multiplier on the scene's authored `Environment::volumeSteps` -- the number of
    // samples the march takes along each ray. Resolution and step count are the volume's two
    // independent scalability axes and they buy different things: resolution trades spatial
    // detail at silhouettes, steps trade depth banding along the ray. 1.0 is the authored count.
    // Offline never scales either (§5.9).
    float volumeStepScale = 1.0f;

    // ADR-382, the brief's §18 quality ladder. Multiplier on every particle system's `spawnRate`;
    // capacity is untouched, because changing it destroys and recreates the pool (ADR-015) and a
    // tier change would then empty every system mid-shot.
    //
    // WHAT NOT TO REACH FOR, and the reason this comment exists: **the volume's step count is not
    // the lever it looks like.** Measured on the Tree of Life's cosmic vortex, minima of three runs
    // at 1920x1080: 1 km march at 32 steps is 14.615 ms and at 48 steps is 14.549 -- inside the
    // noise -- while a 4 km march at 48 steps is 12.911, *cheaper* than the 1 km one. The volume's
    // cost is how many pixels have non-zero density and therefore evaluate their noise, not how far
    // or how finely the ray is marched. `volumeResolutionScale` is what moves that number;
    // `volumeStepScale` buys depth banding back and almost no time. Everybody reaches for the step
    // count first, so it is written here rather than in an ADR nobody will open.
    //
    // ONLY PREVIEW REDUCES THIS. Realtime is the reference live picture, and a tier that silently
    // removed 40% of every existing scene's particles would be changing what the engine looks like
    // by default rather than offering a cheaper view of it -- `tests/rendering/test_particles_gpu`
    // caught exactly that, its CPU emission model reading 18 alive where it predicted 31.
    //
    // §18 also forbids something these must not become: a tier may scale resolution, sample counts
    // and particle counts, and may NOT remove an artistic control. A Preview that hides the
    // vortex's colour knobs is a different product, not a cheaper one -- which is why every field
    // here is a renderer setting and none of them is a parameter's visibility.
    float particleSpawnScale = 1.0f;

    bool clusteredLighting = true;         // false = the 8-light uniform fallback path
    // Raymarched SDFs in the shadow-map pass. Was also unread until the same audit: the shader
    // derived its own budget as `maxSteps / 4` and these four numbers evaluated to nothing.
    std::uint32_t sdfShadowSteps = 24;

    // ---- material tiers (ADR-133) --------------------------------------------------------------
    //
    // `materialTiers` is whether importance-driven *assignment* runs at all; `forcedMaterialTier`
    // is the tier every draw is at least, whatever importance says, and is what an A/B arm and the
    // debug UI move. Offline sets `materialTiers = false` and `forcedMaterialTier = Full`, which is
    // the §5.9 guarantee spelled as data rather than as a special case in the assignment code.
    bool materialTiers = false;
    MaterialTier forcedMaterialTier = MaterialTier::Full;
    // ADR-138: a tier for procedural draws alone, or < 0 for "whatever the frame is using". The
    // frame-global tier measures a ceiling; this measures the share assignment could reach, because
    // on this content the assignable geometry is the scatter. Not a shipping setting -- an arm.
    int proceduralMaterialTier = -1;

    // ADR-155: the first LOD rung that shades at the flat tier, or < 0 for "no rung does". Rung
    // tracks projected size, so this demotes distant scatter and leaves the foreground alone --
    // measured at 5.64 ms for *every* procedural draw (ADR-138), of which this recovers the part
    // that is not in front of the camera. Explicit, general, and exposed through the tier table
    // rather than being a property of any one scene (§49).
    int flatTierFromRung = -1;

    // ADR-125 / §5.9: hysteresis makes the image depend on the camera's history, so an offline
    // render may not have it -- two renders of the same frame must agree whatever route the camera
    // took to get there. That rule was expressed in the CPU representation selector and *not* in
    // the GPU cull ladder, which read the scene's authored `lodHysteresis` at every tier; ADR-146
    // measured 30 of 582 instances landing on a different rung by approach at QualityTier::Offline.
    // Expressed here as policy so both paths read one field rather than each remembering the rule.
    bool lodHysteresisAllowed = true;
    // How many *local* (clustered) lights a fragment of each tier evaluates. Directional lights are
    // never capped: there are at most three of them, they reach every fragment, and dropping one is
    // a lighting change rather than a cost reduction. `kUnlimitedLocalLights` is the Full tier's
    // value and means "whatever the froxel holds".
    static constexpr std::uint32_t kUnlimitedLocalLights = 0xffffffffu;
    std::uint32_t reducedTierLocalLights = 6;
    std::uint32_t flatTierLocalLights = 2;

    // ---- §34 fixed render scale ------------------------------------------------------------------
    //
    // The fraction of the requested output resolution the scene is rendered at, resolved once when
    // the targets are sized. Fixed per tier and never adaptive: dynamic resolution is deferred with
    // its evidence (§5.8), and the evidence is that this renderer's scene pass is only 44%
    // resolution-dependent -- 640x400 is 0.26x the pixels of 1280x800 and 0.66x the scene time --
    // so the naive model that makes dynamic resolution attractive is wrong here by a factor of two.
    float renderScale = 1.0f;

    // The local-light budget of one tier. One place, so the shader's table and the CPU's cannot
    // drift apart.
    [[nodiscard]] std::uint32_t localLightBudget(MaterialTier tier) const {
        switch (tier) {
        case MaterialTier::Full: return kUnlimitedLocalLights;
        case MaterialTier::ReducedLights: return reducedTierLocalLights;
        case MaterialTier::Flat: return flatTierLocalLights;
        }
        return kUnlimitedLocalLights;
    }

    [[nodiscard]] static QualitySettings forTier(QualityTier tier) {
        QualitySettings q;
        switch (tier) {
        case QualityTier::Preview:
            q.shadowResolution = 1024;
            q.cascadeCount = 2;
            q.shadowPcfTaps = 6;
            q.pcssBlockerTaps = 6;
            q.softShadows = false;
            q.contactSteps = 8;
            q.aoSlices = 2;
            q.aoStepsPerSlice = 4;
            q.aoHistoryFrames = 6;
            q.sdfShadowSteps = 16;
            q.materialTiers = true;
            q.reducedTierLocalLights = 4;
            q.flatTierLocalLights = 1;
            q.flatTierFromRung = 1; // ADR-155
            q.volumeResolutionScale = 0.25f;
            q.temporalHistoryScale = 0.25f;
            q.volumeStepScale = 0.5f;
            // A quarter of the particles. Deliberately not zero: the Tree of Life's motes take 30
            // to 50 seconds of playback to reach the vortex (ADR-380), so a tier that cut them
            // hard would make a working effect look broken to anyone previewing it.
            q.particleSpawnScale = 0.25f;
            break;
        case QualityTier::Realtime:
            q.materialTiers = true;
            // ADR-155: procedural draws below the foreground rung shade flat. Measured at +1.11 ms
            // (7.46%) on Glowmere with 3.07% of pixels differing and no visible change at full
            // size -- against 39.47% and an obviously flattened scene for demoting every rung.
            q.flatTierFromRung = 1;
            break;
        case QualityTier::High:
            q.shadowMaskScale = 1.0f; // the reference live picture: the term at full resolution
            q.shadowResolution = 2048;
            q.cascadeCount = 4;
            q.shadowPcfTaps = 20;
            q.pcssBlockerTaps = 16;
            q.contactSteps = 16;
            q.aoSlices = 4;
            q.aoStepsPerSlice = 8;
            q.aoHistoryFrames = 12;
            q.sdfShadowSteps = 32;
            // High keeps assignment on but only ever reaches the middle rung: the flat rung is a
            // visible reduction and High is the reference *live* picture. ADR-155: and it demotes
            // no LOD rung, for the same reason.
            q.materialTiers = true;
            q.reducedTierLocalLights = 12;
            q.flatTierLocalLights = 12;
            // The reference live picture, for the same reason `shadowMaskScale` is 1.0 here: the
            // tier exists to say what the frame looks like with no auxiliary pass downsampled.
            q.temporalHistoryScale = 0.5f;
            q.volumeResolutionScale = 1.0f;
            break;
        case QualityTier::Offline:
            q.shadowResolution = 4096;
            q.cascadeCount = 4;
            q.shadowPcfTaps = 24;
            q.pcssBlockerTaps = 24;
            q.contactSteps = 24;
            q.aoSlices = 6;
            q.aoStepsPerSlice = 12;
            q.aoResolutionScale = 1.0f;
            q.shadowMaskScale = 1.0f;
            q.aoHistoryFrames = 16;
            q.sdfShadowSteps = 48;
            // §5.9: offline takes no representation or shading shortcut, and says so as data.
            q.materialTiers = false;
            q.forcedMaterialTier = MaterialTier::Full;
            q.renderScale = 1.0f;
            // §5.9: an offline render takes no temporal or resolution shortcut. The march runs
            // per pixel at the authored step count, and the composite is then an exact copy.
            // §5.9: offline takes no temporal shortcut -- the ring is full resolution, so an
            // offline echo is the preview's echo without its downsample.
            q.temporalHistoryScale = 1.0f;
            q.volumeResolutionScale = 1.0f;
            q.volumeStepScale = 1.0f;
            // §5.9 / ADR-146: and it carries no history in the LOD ladder either, so the frame
            // does not depend on which way the camera arrived at it.
            q.lodHysteresisAllowed = false;
            break;
        }
        return q;
    }
};

} // namespace avgen::rendering
