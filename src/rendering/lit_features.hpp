#pragma once
// ADR-1815: lit-shader specialisation.
//
// The lit shader (pbr_shade.wgsl, lighting.wgsl) carries every feature a surface might use -- material programs, the
// entity effect lanes, cel and stylised lighting, thin film and anisotropy, reflection bands, surface waves, the sky's
// ground illumination, area lights -- behind uniform branches. A branch that is never taken still costs: its code and
// the registers it holds stay in the light loop, and on the render audit's material lab the general shader's lit pass
// was 8.3 ms where the same pixels, with the unused features compiled out, took 2.7 ms.
//
// Each feature is a WGSL `override` bool (`LIT_*`), `true` by default, so a pipeline that sets none of them is the
// general shader exactly. A specialised variant sets to `false` every feature its frame provably does not use; the
// mask is derived from what the GPU will read (the frame uniforms, the drawn objects' uniforms, the packed lights), so
// a gate is closed only where the general shader would have skipped the same branch.

#include <webgpu/webgpu_cpp.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace avgen::rendering {

enum LitFeature : std::uint32_t {
    kLitPrograms = 1u << 0,    // LIT_PROGRAMS: a material program on some drawn surface
    kLitFx = 1u << 1,          // LIT_FX: entity effect lanes (fxA.z != 0)
    kLitToon = 1u << 2,        // LIT_TOON: a cel material (toon0.x > 0.5)
    kLitStyled = 1u << 3,      // LIT_STYLED: the scene-wide stylised path (frame.lightCounts.z > 0.5)
    kLitOptics = 1u << 4,      // LIT_OPTICS: thin film or anisotropy (optics.x > 0 or optics.z != 0)
    kLitBands = 1u << 5,       // LIT_BANDS: reflection bands (frame.bandsSoft2.w > 0.5)
    kLitWaves = 1u << 6,       // LIT_WAVES: surface waves (frame.waveCount.x > 0)
    kLitAtmosGround = 1u << 7, // LIT_ATMOS_GROUND: the sky effects' ground illumination
    kLitAreaLights = 1u << 8,  // LIT_AREA_LIGHTS: a rect, disk, tube or sphere light
};
inline constexpr std::uint32_t kAllLitFeatures = (1u << 9) - 1u;

// Appends one constant per feature (1 = on, 0 = off) for a specialised pipeline; appends nothing for the general mask,
// so the general pipelines are created exactly as before.
inline void appendLitFeatureConstants(std::uint32_t mask, std::vector<wgpu::ConstantEntry>& out) {
    static constexpr std::array<const char*, 9> kNames = {"LIT_PROGRAMS", "LIT_FX",    "LIT_TOON",
                                                          "LIT_STYLED",   "LIT_OPTICS", "LIT_BANDS",
                                                          "LIT_WAVES",    "LIT_ATMOS_GROUND", "LIT_AREA_LIGHTS"};
    if ((mask & kAllLitFeatures) == kAllLitFeatures) {
        return;
    }
    for (std::size_t i = 0; i < kNames.size(); ++i) {
        wgpu::ConstantEntry e{};
        e.key = kNames[i];
        e.value = (mask & (1u << i)) != 0 ? 1.0 : 0.0;
        out.push_back(e);
    }
}

// "programs+fx+..." for a log line; "general" for every feature.
inline std::string litFeatureNames(std::uint32_t mask) {
    static constexpr std::array<const char*, 9> kShort = {"programs", "fx",    "toon",       "styled", "optics",
                                                          "bands",    "waves", "atmosGround", "areaLights"};
    if ((mask & kAllLitFeatures) == kAllLitFeatures) {
        return "general";
    }
    std::string s;
    for (std::size_t i = 0; i < kShort.size(); ++i) {
        if ((mask & (1u << i)) != 0) {
            s += s.empty() ? kShort[i] : std::string("+") + kShort[i];
        }
    }
    return s.empty() ? "none" : s;
}

} // namespace avgen::rendering
