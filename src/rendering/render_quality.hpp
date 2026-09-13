#pragma once

// Quality tiers (ADR-035). A tier scales sample counts, resolutions and history lengths only:
// the scene, its parameters and its determinism are identical across tiers, so an offline render
// of a world matches the preview frame by frame except in noise and resolution of the auxiliary
// passes. The tier is plain data so tests, the render job and the UI can all set it.

#include <cstdint>
#include <string_view>

namespace avgen::rendering {

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
    for (const auto tier : {QualityTier::Preview, QualityTier::Realtime, QualityTier::High, QualityTier::Offline}) {
        if (name == qualityTierName(tier)) {
            out = tier;
            return true;
        }
    }
    return false;
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
    std::uint32_t pcssBlockerTaps = 12;
    bool softShadows = true;               // percentage-closer soft shadows for the key light
    std::uint32_t contactSteps = 12;       // screen-space contact-shadow march
    bool contactShadows = true;
    std::uint32_t aoSlices = 3;            // GTAO direction slices
    std::uint32_t aoStepsPerSlice = 6;
    bool ambientOcclusion = true;
    float aoResolutionScale = 0.5f;        // half resolution + bilateral upsample
    std::uint32_t aoHistoryFrames = 8;     // temporal accumulation length
    // ADR-087: the fraction of the scene's resolution the directional lights' combined shadow term
    // (cascade lookup + contact march) is computed at, before a bilateral upsample in the lit
    // pass. 1.0 means "no mask pass": the lit pass computes the term per pixel, exactly as it did
    // before the mask existed, which is what keeps offline renders unchanged.
    float shadowMaskScale = 0.5f;

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

    bool clusteredLighting = true;         // false = the 8-light uniform fallback path
    std::uint32_t sdfShadowSteps = 24;     // raymarched SDFs in the depth-only passes

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
            q.volumeResolutionScale = 0.25f;
            q.volumeStepScale = 0.5f;
            break;
        case QualityTier::Realtime:
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
            // The reference live picture, for the same reason `shadowMaskScale` is 1.0 here: the
            // tier exists to say what the frame looks like with no auxiliary pass downsampled.
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
            // §5.9: an offline render takes no temporal or resolution shortcut. The march runs
            // per pixel at the authored step count, and the composite is then an exact copy.
            q.volumeResolutionScale = 1.0f;
            q.volumeStepScale = 1.0f;
            break;
        }
        return q;
    }
};

} // namespace avgen::rendering
