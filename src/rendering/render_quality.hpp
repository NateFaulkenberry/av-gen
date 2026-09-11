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
            break;
        }
        return q;
    }
};

} // namespace avgen::rendering
