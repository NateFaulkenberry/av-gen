#pragma once

// The Cosmic Ocean's World Effects panel, as data (ADR-390, ADR-382).
//
// The panel walks these tables and so does `test_cosmic_ocean.cpp`. That is the whole point, and it
// is ADR-382's lesson applied rather than agreed with: a panel that computes `atmos/<name>/<leaf>`
// inside an ImGui call cannot be asked, by anything, whether the leaf exists. A wrong path does not
// fail to compile and does not throw -- the section just draws an empty box, indistinguishable from
// "this scene has no such effect". The owner found one of those by eye today.
//
// Two tables rather than one, because §29's workflow is "add it, enable it, see something
// beautiful, then adjust the palette, the depth, the density, the motion" -- which is about
// thirty-five controls -- and §28's full outline is about a hundred and thirty. Burying the first
// thirty-five in the other ninety-five is how an artist-facing effect stops being one. The split is
// which *question* a row answers, not how advanced it is: everything in the first table changes
// what the sky looks like from the chair, and everything in the second changes how it is built.
//
// This lives in its own header rather than in `ui_logic.hpp` beside `vortexRows()` only because
// another agent is editing that file; there is no architectural reason for the separation and they
// belong together once the vortex branch has landed.

#include "ui/ui_logic.hpp"

#include <span>

namespace avgen::ui {

// §28's Master, Colour, Nebula, Stars and Planets: what somebody reaches for in the first minute.
[[nodiscard]] inline std::span<const EffectRow> cosmicOceanRows() {
    static constexpr EffectRow kRows[] = {
        // ---- master ----
        {"", "oceanEnabled", "Cosmic Ocean", ""},
        {"", "intensity", "Intensity", ""},
        {"", "brightness", "Overall brightness", ""},
        {"", "exposure", "Exposure", "%.2f stops"},
        {"", "contrast", "Contrast", ""},
        {"", "saturation", "Saturation", ""},
        {"", "seed", "Seed", "%.0f"},

        // ---- colour (§14) ----
        // The palette first, because it is what turns one cosmic ocean into another, and because
        // §14 insists these emerge from controls rather than being presets with hidden state.
        {"Colour", "colorDeepSpace", "Deep space", "", false, true},
        {"", "colorPrimary", "Primary", "", false, true},
        {"", "colorSecondary", "Secondary", "", false, true},
        {"", "colorAccent", "Accent", "", false, true},
        {"", "colorBlend", "Blend", ""},
        {"", "colorSaturation", "Palette saturation", ""},
        {"", "hueDrift", "Hue drift", ""},

        // ---- nebula (§4) ----
        // The mid nebula is the one an artist means by "the nebula": the far one is scenery behind
        // it. Both are here because §4's whole subject is the two of them disagreeing.
        {"Nebula", "colorNebula", "Nebula colour", "", false, true},
        {"", "nebulaMidDensity", "Density", ""},
        {"", "nebulaMidBrightness", "Brightness", ""},
        {"", "nebulaMidScale", "Scale", ""},
        {"", "nebulaMidTurbulence", "Turbulence", ""},
        {"", "nebulaMidWarp", "Warp", ""},
        {"", "nebulaMidContrast", "Contrast", ""},
        {"", "nebulaMidShimmer", "Shimmer", ""},
        {"", "nebulaMidEvolve", "Evolution", "%.3f /s"},
        {"", "nebulaFarDensity", "Far density", ""},
        {"", "nebulaFarBrightness", "Far brightness", ""},
        {"", "nebulaFarSoftness", "Far softness", ""},

        // ---- stars (§7, §8) ----
        {"Stars", "colorStar", "Star colour", "", false, true},
        {"", "starsNearDensity", "Near density", ""},
        {"", "starsMidDensity", "Mid density", ""},
        {"", "starsFarDensity", "Far density", ""},
        {"", "starsUltraDensity", "Ultra-distant density", ""},
        {"", "starSize", "Size", ""},
        {"", "starTwinkle", "Twinkle", ""},
        {"", "starTwinkleSpeed", "Twinkle speed", "%.2f /s"},
        {"", "starGlint", "Glint chance", ""},
        {"", "starColorVariation", "Colour variation", ""},

        // ---- planets (§5, §6) ----
        {"Planets", "colorPlanet", "Planet colour", "", false, true},
        {"", "planetsDensity", "Density", ""},
        {"", "planetScale", "Scale", ""},
        {"", "planetScaleVariation", "Scale variation", ""},
        {"", "planetsBrightness", "Brightness", ""},
        {"", "planetGlow", "Atmospheric glow", ""},
        {"", "planetRings", "Rings", ""},
        {"", "planetCloudBands", "Cloud bands", ""},
        {"", "planetDrift", "Drift", "%.4f rad/s"},
        {"", "planetRotation", "Rotation", "%.3f rad/s"},
        {"", "planetsParallax", "Parallax", ""},
    };
    return kRows;
}

// §28's Depth & Layers, Cosmic Dust, Galaxies, Atmosphere, Motion, Events, Mask and Quality: how
// the ocean is built rather than what it looks like.
[[nodiscard]] inline std::span<const EffectRow> cosmicOceanAdvancedRows() {
    static constexpr EffectRow kRows[] = {
        // ---- depth and layers (§10, §11) ----
        // Depths are logarithmic because they span four orders of magnitude -- dust at 900 m and
        // ultra-distant stars at 900 km on one linear slider is one usable end and one dead one.
        {"Depth & layers", "globalScale", "Global scale", "", true},
        {"", "dustDepth", "Dust depth", "%.0f m", true},
        {"", "planetsDepth", "Planet depth", "%.0f m", true},
        {"", "starsNearDepth", "Near star depth", "%.0f m", true},
        {"", "starsMidDepth", "Mid star depth", "%.0f m", true},
        {"", "starsFarDepth", "Far star depth", "%.0f m", true},
        {"", "starsUltraDepth", "Ultra star depth", "%.0f m", true},
        {"", "nebulaMidDepth", "Mid nebula depth", "%.0f m", true},
        {"", "nebulaFarDepth", "Far nebula depth", "%.0f m", true},
        {"", "galaxiesDepth", "Galaxy depth", "%.0f m", true},
        {"Parallax", "dustParallax", "Dust", ""},
        {"", "starsNearParallax", "Near stars", ""},
        {"", "starsMidParallax", "Mid stars", ""},
        {"", "starsFarParallax", "Far stars", ""},
        {"", "starsUltraParallax", "Ultra stars", ""},
        {"", "nebulaMidParallax", "Mid nebula", ""},
        {"", "nebulaFarParallax", "Far nebula", ""},
        {"", "galaxiesParallax", "Galaxies", ""},

        // ---- cosmic dust (§9) ----
        {"Cosmic dust", "dustDensity", "Density", ""},
        {"", "dustBrightness", "Brightness", ""},
        {"", "dustSize", "Size", ""},
        {"", "dustSizeVariation", "Size variation", ""},
        {"", "dustDrift", "Drift", "%.2f m/s"},
        {"", "dustTurbulence", "Turbulence", ""},
        {"", "dustFade", "Fade range", ""},

        // ---- galaxies (§19) ----
        {"Galaxies", "galaxiesDensity", "Density", ""},
        {"", "galaxiesBrightness", "Brightness", ""},
        {"", "galaxyScale", "Scale", ""},
        {"", "galaxySpiral", "Spiral amount", ""},
        {"", "galaxyCore", "Core brightness", ""},
        {"", "galaxyInclination", "Inclination", ""},
        {"", "galaxyRotation", "Rotation", "%.4f rad/s"},

        // ---- atmosphere (§13, §20) ----
        {"Atmosphere", "colorAtmosphere", "Atmospheric tint", "", false, true},
        {"", "haze", "Haze", ""},
        {"", "atmosphereDensity", "Density", ""},
        {"", "depthBrightnessFalloff", "Depth brightness falloff", ""},
        {"", "depthSaturationFalloff", "Depth saturation falloff", ""},
        {"", "depthContrastFalloff", "Depth contrast falloff", ""},
        {"", "scattering", "Scattering", ""},
        {"", "glow", "Glow", ""},

        // ---- colour evolution (§15) ----
        // Three rates, because the brief asks for minutes, tens of seconds and seconds, and one
        // slider cannot be all three.
        {"Colour evolution", "colorSlowSpeed", "Slow (minutes)", "%.4f /s"},
        {"", "colorMediumSpeed", "Medium (tens of seconds)", "%.3f /s"},
        {"", "colorFastSpeed", "Fast (seconds)", "%.2f /s"},
        {"", "colorFastAmount", "Fast amount", ""},

        // ---- motion (§16, §17) ----
        {"Motion", "globalSpeed", "Global speed", ""},
        {"", "flowStrength", "Flow strength", ""},
        {"", "flowAzimuth", "Flow azimuth", "%.0f deg"},
        {"", "flowElevation", "Flow elevation", "%.0f deg"},
        {"", "flowTurbulence", "Flow turbulence", ""},
        {"", "flowCurl", "Flow curl", ""},
        {"", "flowScale", "Flow scale", ""},
        {"", "flowEvolve", "Flow evolution", "%.3f /s"},
        {"", "starDrift", "Star drift", "%.4f rad/s"},
        {"", "nebulaMidFlow", "Mid nebula flow", "%.3f /s"},
        {"", "nebulaFarFlow", "Far nebula flow", "%.3f /s"},
        {"", "nebulaFarEvolve", "Far nebula evolution", "%.3f /s"},
        {"", "planetDriftDirection", "Planet drift direction", "%.0f deg"},

        // ---- events (§18) ----
        // Rates in events per minute, because "0.05 per minute" is a comet every twenty minutes and
        // an artist can reason about that; a period in seconds reads backwards.
        {"Events", "shootingStars", "Shooting stars", "%.2f /min"},
        {"", "eventComets", "Comets", "%.2f /min"},
        {"", "eventFlares", "Flares", "%.2f /min"},
        {"", "eventPulses", "Pulses", "%.2f /min"},
        {"", "eventProbability", "Probability", ""},
        {"", "eventIntensity", "Intensity", ""},
        {"", "eventSize", "Size", ""},
        {"", "eventSpeed", "Speed", ""},
        {"", "eventLifetime", "Lifetime", "%.2f s"},
        {"", "colorEvent", "Event colour", "", false, true},

        // ---- composition mask (§33) ----
        // Off by default, and the amount is first so that the four angles below are visibly
        // inoperative until somebody asks for them.
        {"Composition mask", "maskAmount", "Amount", ""},
        {"", "maskAzimuth", "Azimuth", "%.0f deg"},
        {"", "maskElevation", "Elevation", "%.0f deg"},
        {"", "maskInnerAngle", "Inner angle", "%.0f deg"},
        {"", "maskOuterAngle", "Outer angle", "%.0f deg"},
        {"", "maskSuppression", "Suppression", ""},
        {"", "maskHorizonBias", "Horizon bias", ""},

        // ---- quality (§23) ----
        // Sample counts, not visibility. The tier scales these; a scene may also set them, because
        // a scene may legitimately want a cheaper sky than its tier gives it. What a tier may never
        // do is hide a row above -- that is the rule `QualitySettings::particleSpawnScale` states.
        {"Quality", "qualityNebulaOctaves", "Nebula octaves", "%.0f"},
        {"", "qualityStarStrata", "Star layers", "%.0f"},
        {"", "qualityPlanetCells", "Planet samples", "%.0f"},
        {"", "qualityDustCells", "Dust samples", "%.0f"},

        // ---- placement ----
        {"Placement", "centerX", "Centre X", "%.1f m"},
        {"", "centerY", "Centre Y", "%.1f m"},
        {"", "centerZ", "Centre Z", "%.1f m"},
        {"", "nebulaMidColorMix", "Mid nebula colour mix", ""},
        {"", "nebulaFarColorMix", "Far nebula colour mix", ""},
        {"", "nebulaFarScale", "Far nebula scale", ""},
        {"", "nebulaFarTurbulence", "Far nebula turbulence", ""},
        {"", "nebulaFarWarp", "Far nebula warp", ""},
        {"", "nebulaFarContrast", "Far nebula contrast", ""},
        {"", "nebulaFarShimmer", "Far nebula shimmer", ""},
        {"", "nebulaFarDetail", "Far nebula detail", "%.0f"},
        {"", "nebulaMidDetail", "Mid nebula detail", "%.0f"},
        {"", "nebulaMidSoftness", "Mid nebula softness", ""},
        {"", "starSizeVariation", "Star size variation", ""},
        {"", "starsNearBrightness", "Near star brightness", ""},
        {"", "starsMidBrightness", "Mid star brightness", ""},
        {"", "starsFarBrightness", "Far star brightness", ""},
        {"", "starsUltraBrightness", "Ultra star brightness", ""},
        {"", "planetClustering", "Planet clustering", ""},
        {"", "planetTerminator", "Planet terminator", ""},
        {"", "planetNightSide", "Planet night side", ""},
        {"", "planetRingSize", "Ring size", "%.2f x"},
        {"", "planetRingBrightness", "Ring brightness", ""},
        {"", "planetColorVariation", "Planet colour variation", ""},
    };
    return kRows;
}

} // namespace avgen::ui
