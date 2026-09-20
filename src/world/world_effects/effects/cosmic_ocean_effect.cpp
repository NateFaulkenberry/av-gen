// The Cosmic Ocean (ADR-390, ported onto the ADR-500 registry).
//
// **What it is.** A procedural deep-space background: nebulae in two strata, four shells of stars,
// planets, cosmic dust and galaxies, every one of them a hash evaluated against the view ray, with
// per-layer depth and parallax so the whole thing has real depth as the camera moves.
//
// **Why it is in this family at all**, which is the same argument the vortex makes: a scene AUTHORS
// it. It wants instances with names, a table of parameters under `atmos/<name>/`, serialisation,
// modulation, presets and a panel. That is what this family is for. Which pass rasterises a kind is
// a rendering detail -- see `AtmosphericFrame::any()` for the one place the distinction is
// load-bearing.
//
// **And it is the case ADR-500's own preamble says the registry cannot collapse.** That header is
// explicit: a kind reaching the picture through an integrator the engine already has is the easy
// half, and "a kind that needs a NEW integrator needs shader work, which is the one part of an
// effect this registry does not and cannot move into a single C++ file". The Cosmic Ocean is that
// kind. It has `rendering::CosmicOceanRenderer`, its own WGSL and its own GPU struct, and they are
// not in this file because they cannot be. What the registry does buy it is everything above the
// draw, and that is not a small remainder: 149 fields with their ranges, labels, units and panel
// sections; the JSON round trip; the validation clamps; six presets; default audio routes; the Add
// button; and the conformance entry -- all of which were, before this, spread across
// `atmospheric_params.cpp`, `ui_logic.hpp`, `cosmic_ocean_rows.hpp`, `atmospherics.cpp` and the
// panel, and had to agree by hand.
//
// **On how the fields got here.** They were not retyped. ADR-390 already held the ranges and
// accessors in `cosmicFloatFields()` / `cosmicColorFields()` / `cosmicBoolFields()` and the labels,
// sections and formats in `cosmicOceanRows()` -- two tables joined by leaf name, which is exactly
// the by-hand agreement ADR-392 counted the cost of. The rows below are that join, performed
// mechanically: 139 floats + 9 colours + 1 bool against 149 rows, with **no row lacking a field and
// no field lacking a row**. That bijection is why the transcription can be trusted, and it is the
// same check `checkRegistry` now makes on every load.
//
// **The one number that is authored here rather than carried**: the default audio routes. ADR-390
// shipped none, and the registry requires at least one -- so they are chosen, and chosen small and
// `Add`, so silence leaves the authored sky exactly as written.

#include "world/cosmic_ocean.hpp"
#include "world/world_effects/effect_registry.hpp"

#include <array>
#include <string>
#include <string_view>

namespace avgen::world {
namespace {

using E = AtmosphericEffect;

#define GET(expr) +[](const E& e) { return (expr); }
#define SETF(lhs) +[](E& e, float v) { (lhs) = v; }
#define SETC(lhs) +[](E& e, glm::vec3 v) { (lhs) = v; }
#define SETB(lhs) +[](E& e, bool v) { (lhs) = v; }

// ---- the 149 rows, joined from ADR-390's two tables (see the note above) ------------------------
constexpr EffectField kFields[] = {
    boolField("oceanEnabled", "Cosmic Ocean",
              GET(e.cosmicOcean.enabled), SETB(e.cosmicOcean.enabled)).main(),
    floatField("intensity", "Intensity", 0.0f, 8.0f, 0.0f, 2.0f,
               GET(e.cosmicOcean.intensity), SETF(e.cosmicOcean.intensity)).main(),
    floatField("brightness", "Overall brightness", 0.0f, 8.0f, 0.0f, 3.0f,
               GET(e.cosmicOcean.brightness), SETF(e.cosmicOcean.brightness)).main(),
    floatField("exposure", "Exposure", -8.0f, 8.0f, -3.0f, 3.0f,
               GET(e.cosmicOcean.exposure), SETF(e.cosmicOcean.exposure)).fmt("%.2f stops").main(),
    floatField("contrast", "Contrast", 0.0f, 4.0f, 0.2f, 2.0f,
               GET(e.cosmicOcean.contrast), SETF(e.cosmicOcean.contrast)).main(),
    floatField("saturation", "Saturation", 0.0f, 4.0f, 0.0f, 2.0f,
               GET(e.cosmicOcean.saturation), SETF(e.cosmicOcean.saturation)).main(),
    floatField("seed", "Seed", 0.0f, 100000.0f, 0.0f, 999.0f,
               GET(e.cosmicOcean.seed), SETF(e.cosmicOcean.seed)).fmt("%.0f").main(),
    colorField("colorDeepSpace", "Deep space",
               GET(e.cosmicOcean.color.deepSpace), SETC(e.cosmicOcean.color.deepSpace)).sec("Colour").main(),
    colorField("colorPrimary", "Primary",
               GET(e.cosmicOcean.color.primary), SETC(e.cosmicOcean.color.primary)).main(),
    colorField("colorSecondary", "Secondary",
               GET(e.cosmicOcean.color.secondary), SETC(e.cosmicOcean.color.secondary)).main(),
    colorField("colorAccent", "Accent",
               GET(e.cosmicOcean.color.accent), SETC(e.cosmicOcean.color.accent)).main(),
    floatField("colorBlend", "Blend", 0.0f, 1.0f, 0.0f, 1.0f,
               GET(e.cosmicOcean.color.blend), SETF(e.cosmicOcean.color.blend)).main(),
    floatField("colorSaturation", "Palette saturation", 0.0f, 4.0f, 0.0f, 2.0f,
               GET(e.cosmicOcean.color.saturation), SETF(e.cosmicOcean.color.saturation)).main(),
    floatField("hueDrift", "Hue drift", 0.0f, 1.0f, 0.0f, 0.5f,
               GET(e.cosmicOcean.color.hueDrift), SETF(e.cosmicOcean.color.hueDrift)).main(),
    colorField("colorNebula", "Nebula colour",
               GET(e.cosmicOcean.color.nebulaTint), SETC(e.cosmicOcean.color.nebulaTint)).sec("Nebula").main(),
    floatField("nebulaMidDensity", "Density", 0.0f, 4.0f, 0.0f, 1.0f,
               GET(e.cosmicOcean.nebulaMid.stratum.density), SETF(e.cosmicOcean.nebulaMid.stratum.density)).main(),
    floatField("nebulaMidBrightness", "Brightness", 0.0f, 40.0f, 0.0f, 1.0f,
               GET(e.cosmicOcean.nebulaMid.stratum.brightness), SETF(e.cosmicOcean.nebulaMid.stratum.brightness)).main(),
    floatField("nebulaMidScale", "Scale", 0.01f, 40.0f, 0.2f, 8.0f,
               GET(e.cosmicOcean.nebulaMid.scale), SETF(e.cosmicOcean.nebulaMid.scale)).main(),
    floatField("nebulaMidTurbulence", "Turbulence", 0.0f, 3.0f, 0.0f, 1.0f,
               GET(e.cosmicOcean.nebulaMid.turbulence), SETF(e.cosmicOcean.nebulaMid.turbulence)).main(),
    floatField("nebulaMidWarp", "Warp", 0.0f, 4.0f, 0.0f, 1.5f,
               GET(e.cosmicOcean.nebulaMid.warp), SETF(e.cosmicOcean.nebulaMid.warp)).main(),
    floatField("nebulaMidContrast", "Contrast", 0.05f, 8.0f, 0.5f, 4.0f,
               GET(e.cosmicOcean.nebulaMid.contrast), SETF(e.cosmicOcean.nebulaMid.contrast)).main(),
    floatField("nebulaMidShimmer", "Shimmer", 0.0f, 4.0f, 0.0f, 1.0f,
               GET(e.cosmicOcean.nebulaMid.shimmer), SETF(e.cosmicOcean.nebulaMid.shimmer)).main(),
    floatField("nebulaMidEvolve", "Evolution", 0.0f, 2.0f, 0.0f, 0.2f,
               GET(e.cosmicOcean.nebulaMid.evolveSpeed), SETF(e.cosmicOcean.nebulaMid.evolveSpeed)).fmt("%.3f /s").main(),
    floatField("nebulaFarDensity", "Far density", 0.0f, 4.0f, 0.0f, 1.0f,
               GET(e.cosmicOcean.nebulaFar.stratum.density), SETF(e.cosmicOcean.nebulaFar.stratum.density)).main(),
    floatField("nebulaFarBrightness", "Far brightness", 0.0f, 40.0f, 0.0f, 1.0f,
               GET(e.cosmicOcean.nebulaFar.stratum.brightness), SETF(e.cosmicOcean.nebulaFar.stratum.brightness)).main(),
    floatField("nebulaFarSoftness", "Far softness", 0.0f, 1.0f, 0.0f, 1.0f,
               GET(e.cosmicOcean.nebulaFar.softness), SETF(e.cosmicOcean.nebulaFar.softness)).main(),
    colorField("colorStar", "Star colour",
               GET(e.cosmicOcean.color.starTint), SETC(e.cosmicOcean.color.starTint)).sec("Stars").main(),
    floatField("starsNearDensity", "Near density", 0.0f, 4.0f, 0.0f, 1.0f,
               GET(e.cosmicOcean.starsNear.stratum.density), SETF(e.cosmicOcean.starsNear.stratum.density)).main(),
    floatField("starsMidDensity", "Mid density", 0.0f, 4.0f, 0.0f, 1.0f,
               GET(e.cosmicOcean.starsMid.stratum.density), SETF(e.cosmicOcean.starsMid.stratum.density)).main(),
    floatField("starsFarDensity", "Far density", 0.0f, 4.0f, 0.0f, 1.0f,
               GET(e.cosmicOcean.starsFar.stratum.density), SETF(e.cosmicOcean.starsFar.stratum.density)).main(),
    floatField("starsUltraDensity", "Ultra-distant density", 0.0f, 4.0f, 0.0f, 1.0f,
               GET(e.cosmicOcean.starsUltra.stratum.density), SETF(e.cosmicOcean.starsUltra.stratum.density)).main(),
    floatField("starSize", "Size", 0.0f, 16.0f, 0.0f, 4.0f,
               GET(e.cosmicOcean.starsNear.size), SETF(e.cosmicOcean.starsNear.size)).main(),
    floatField("starTwinkle", "Twinkle", 0.0f, 4.0f, 0.0f, 1.0f,
               GET(e.cosmicOcean.starsNear.twinkle), SETF(e.cosmicOcean.starsNear.twinkle)).main(),
    floatField("starTwinkleSpeed", "Twinkle speed", 0.0f, 12.0f, 0.0f, 3.0f,
               GET(e.cosmicOcean.starsNear.twinkleSpeed), SETF(e.cosmicOcean.starsNear.twinkleSpeed)).fmt("%.2f /s").main(),
    floatField("starGlint", "Glint chance", 0.0f, 1.0f, 0.0f, 0.5f,
               GET(e.cosmicOcean.starsNear.glint), SETF(e.cosmicOcean.starsNear.glint)).main(),
    floatField("starColorVariation", "Colour variation", 0.0f, 1.0f, 0.0f, 1.0f,
               GET(e.cosmicOcean.starsNear.colorVariation), SETF(e.cosmicOcean.starsNear.colorVariation)).main(),
    colorField("colorPlanet", "Planet colour",
               GET(e.cosmicOcean.color.planetTint), SETC(e.cosmicOcean.color.planetTint)).sec("Planets").main(),
    floatField("planetsDensity", "Density", 0.0f, 4.0f, 0.0f, 1.0f,
               GET(e.cosmicOcean.planets.stratum.density), SETF(e.cosmicOcean.planets.stratum.density)).main(),
    floatField("planetScale", "Scale", 0.0f, 20.0f, 0.1f, 5.0f,
               GET(e.cosmicOcean.planets.scale), SETF(e.cosmicOcean.planets.scale)).main(),
    floatField("planetScaleVariation", "Scale variation", 0.0f, 1.0f, 0.0f, 1.0f,
               GET(e.cosmicOcean.planets.scaleVariance), SETF(e.cosmicOcean.planets.scaleVariance)).main(),
    floatField("planetsBrightness", "Brightness", 0.0f, 40.0f, 0.0f, 4.0f,
               GET(e.cosmicOcean.planets.stratum.brightness), SETF(e.cosmicOcean.planets.stratum.brightness)).main(),
    floatField("planetGlow", "Atmospheric glow", 0.0f, 8.0f, 0.0f, 2.0f,
               GET(e.cosmicOcean.planets.atmosphereGlow), SETF(e.cosmicOcean.planets.atmosphereGlow)).main(),
    floatField("planetRings", "Rings", 0.0f, 1.0f, 0.0f, 1.0f,
               GET(e.cosmicOcean.planets.rings), SETF(e.cosmicOcean.planets.rings)).main(),
    floatField("planetCloudBands", "Cloud bands", 0.0f, 1.0f, 0.0f, 1.0f,
               GET(e.cosmicOcean.planets.cloudBands), SETF(e.cosmicOcean.planets.cloudBands)).main(),
    floatField("planetDrift", "Drift", -2.0f, 2.0f, -0.05f, 0.05f,
               GET(e.cosmicOcean.planets.driftSpeed), SETF(e.cosmicOcean.planets.driftSpeed)).fmt("%.4f rad/s").main(),
    floatField("planetRotation", "Rotation", -2.0f, 2.0f, -0.1f, 0.1f,
               GET(e.cosmicOcean.planets.rotationSpeed), SETF(e.cosmicOcean.planets.rotationSpeed)).fmt("%.3f rad/s").main(),
    floatField("planetsParallax", "Parallax", 0.0f, 1.0f, 0.0f, 1.0f,
               GET(e.cosmicOcean.planets.stratum.parallax), SETF(e.cosmicOcean.planets.stratum.parallax)).main(),
    floatField("globalScale", "Global scale", 0.01f, 20.0f, 0.25f, 4.0f,
               GET(e.cosmicOcean.globalScale), SETF(e.cosmicOcean.globalScale)).sec("Depth & layers").log(),
    floatField("dustDepth", "Dust depth", 1.0f, 5.0e6f, 50.0f, 20000.0f,
               GET(e.cosmicOcean.dust.stratum.depth), SETF(e.cosmicOcean.dust.stratum.depth)).fmt("%.0f m").log(),
    floatField("planetsDepth", "Planet depth", 1.0f, 5.0e6f, 2000.0f, 2.0e5f,
               GET(e.cosmicOcean.planets.stratum.depth), SETF(e.cosmicOcean.planets.stratum.depth)).fmt("%.0f m").log(),
    floatField("starsNearDepth", "Near star depth", 1.0f, 5.0e6f, 1000.0f, 2.0e5f,
               GET(e.cosmicOcean.starsNear.stratum.depth), SETF(e.cosmicOcean.starsNear.stratum.depth)).fmt("%.0f m").log(),
    floatField("starsMidDepth", "Mid star depth", 1.0f, 5.0e6f, 5000.0f, 5.0e5f,
               GET(e.cosmicOcean.starsMid.stratum.depth), SETF(e.cosmicOcean.starsMid.stratum.depth)).fmt("%.0f m").log(),
    floatField("starsFarDepth", "Far star depth", 1.0f, 5.0e6f, 20000.0f, 1.0e6f,
               GET(e.cosmicOcean.starsFar.stratum.depth), SETF(e.cosmicOcean.starsFar.stratum.depth)).fmt("%.0f m").log(),
    floatField("starsUltraDepth", "Ultra star depth", 1.0f, 5.0e6f, 50000.0f, 2.0e6f,
               GET(e.cosmicOcean.starsUltra.stratum.depth), SETF(e.cosmicOcean.starsUltra.stratum.depth)).fmt("%.0f m").log(),
    floatField("nebulaMidDepth", "Mid nebula depth", 1.0f, 5.0e6f, 5000.0f, 5.0e5f,
               GET(e.cosmicOcean.nebulaMid.stratum.depth), SETF(e.cosmicOcean.nebulaMid.stratum.depth)).fmt("%.0f m").log(),
    floatField("nebulaFarDepth", "Far nebula depth", 1.0f, 5.0e6f, 10000.0f, 1.0e6f,
               GET(e.cosmicOcean.nebulaFar.stratum.depth), SETF(e.cosmicOcean.nebulaFar.stratum.depth)).fmt("%.0f m").log(),
    floatField("galaxiesDepth", "Galaxy depth", 1.0f, 5.0e6f, 50000.0f, 2.0e6f,
               GET(e.cosmicOcean.galaxies.stratum.depth), SETF(e.cosmicOcean.galaxies.stratum.depth)).fmt("%.0f m").log(),
    floatField("dustParallax", "Dust", 0.0f, 1.0f, 0.0f, 1.0f,
               GET(e.cosmicOcean.dust.stratum.parallax), SETF(e.cosmicOcean.dust.stratum.parallax)).sec("Parallax"),
    floatField("starsNearParallax", "Near stars", 0.0f, 1.0f, 0.0f, 1.0f,
               GET(e.cosmicOcean.starsNear.stratum.parallax), SETF(e.cosmicOcean.starsNear.stratum.parallax)),
    floatField("starsMidParallax", "Mid stars", 0.0f, 1.0f, 0.0f, 1.0f,
               GET(e.cosmicOcean.starsMid.stratum.parallax), SETF(e.cosmicOcean.starsMid.stratum.parallax)),
    floatField("starsFarParallax", "Far stars", 0.0f, 1.0f, 0.0f, 1.0f,
               GET(e.cosmicOcean.starsFar.stratum.parallax), SETF(e.cosmicOcean.starsFar.stratum.parallax)),
    floatField("starsUltraParallax", "Ultra stars", 0.0f, 1.0f, 0.0f, 1.0f,
               GET(e.cosmicOcean.starsUltra.stratum.parallax), SETF(e.cosmicOcean.starsUltra.stratum.parallax)),
    floatField("nebulaMidParallax", "Mid nebula", 0.0f, 1.0f, 0.0f, 1.0f,
               GET(e.cosmicOcean.nebulaMid.stratum.parallax), SETF(e.cosmicOcean.nebulaMid.stratum.parallax)),
    floatField("nebulaFarParallax", "Far nebula", 0.0f, 1.0f, 0.0f, 1.0f,
               GET(e.cosmicOcean.nebulaFar.stratum.parallax), SETF(e.cosmicOcean.nebulaFar.stratum.parallax)),
    floatField("galaxiesParallax", "Galaxies", 0.0f, 1.0f, 0.0f, 1.0f,
               GET(e.cosmicOcean.galaxies.stratum.parallax), SETF(e.cosmicOcean.galaxies.stratum.parallax)),
    floatField("dustDensity", "Density", 0.0f, 4.0f, 0.0f, 1.0f,
               GET(e.cosmicOcean.dust.stratum.density), SETF(e.cosmicOcean.dust.stratum.density)).sec("Cosmic dust"),
    floatField("dustBrightness", "Brightness", 0.0f, 40.0f, 0.0f, 2.0f,
               GET(e.cosmicOcean.dust.stratum.brightness), SETF(e.cosmicOcean.dust.stratum.brightness)),
    floatField("dustSize", "Size", 0.0f, 16.0f, 0.0f, 4.0f,
               GET(e.cosmicOcean.dust.size), SETF(e.cosmicOcean.dust.size)),
    floatField("dustSizeVariation", "Size variation", 0.0f, 1.0f, 0.0f, 1.0f,
               GET(e.cosmicOcean.dust.sizeVariance), SETF(e.cosmicOcean.dust.sizeVariance)),
    floatField("dustDrift", "Drift", 0.0f, 200.0f, 0.0f, 8.0f,
               GET(e.cosmicOcean.dust.driftSpeed), SETF(e.cosmicOcean.dust.driftSpeed)).fmt("%.2f m/s"),
    floatField("dustTurbulence", "Turbulence", 0.0f, 4.0f, 0.0f, 1.0f,
               GET(e.cosmicOcean.dust.turbulence), SETF(e.cosmicOcean.dust.turbulence)),
    floatField("dustFade", "Fade range", 0.001f, 1.0f, 0.05f, 1.0f,
               GET(e.cosmicOcean.dust.fadeDistance), SETF(e.cosmicOcean.dust.fadeDistance)),
    floatField("galaxiesDensity", "Density", 0.0f, 4.0f, 0.0f, 1.0f,
               GET(e.cosmicOcean.galaxies.stratum.density), SETF(e.cosmicOcean.galaxies.stratum.density)).sec("Galaxies"),
    floatField("galaxiesBrightness", "Brightness", 0.0f, 40.0f, 0.0f, 3.0f,
               GET(e.cosmicOcean.galaxies.stratum.brightness), SETF(e.cosmicOcean.galaxies.stratum.brightness)),
    floatField("galaxyScale", "Scale", 0.0f, 20.0f, 0.1f, 5.0f,
               GET(e.cosmicOcean.galaxies.scale), SETF(e.cosmicOcean.galaxies.scale)),
    floatField("galaxySpiral", "Spiral amount", 0.0f, 1.0f, 0.0f, 1.0f,
               GET(e.cosmicOcean.galaxies.spiral), SETF(e.cosmicOcean.galaxies.spiral)),
    floatField("galaxyCore", "Core brightness", 0.0f, 20.0f, 0.0f, 6.0f,
               GET(e.cosmicOcean.galaxies.coreBrightness), SETF(e.cosmicOcean.galaxies.coreBrightness)),
    floatField("galaxyInclination", "Inclination", 0.0f, 1.0f, 0.0f, 1.0f,
               GET(e.cosmicOcean.galaxies.inclination), SETF(e.cosmicOcean.galaxies.inclination)),
    floatField("galaxyRotation", "Rotation", -1.0f, 1.0f, -0.02f, 0.02f,
               GET(e.cosmicOcean.galaxies.rotationSpeed), SETF(e.cosmicOcean.galaxies.rotationSpeed)).fmt("%.4f rad/s"),
    colorField("colorAtmosphere", "Atmospheric tint",
               GET(e.cosmicOcean.color.atmosphereTint), SETC(e.cosmicOcean.color.atmosphereTint)).sec("Atmosphere"),
    floatField("haze", "Haze", 0.0f, 4.0f, 0.0f, 1.0f,
               GET(e.cosmicOcean.atmosphere.haze), SETF(e.cosmicOcean.atmosphere.haze)),
    floatField("atmosphereDensity", "Density", 0.0f, 4.0f, 0.0f, 1.0f,
               GET(e.cosmicOcean.atmosphere.density), SETF(e.cosmicOcean.atmosphere.density)),
    floatField("depthBrightnessFalloff", "Depth brightness falloff", 0.0f, 1.0f, 0.0f, 1.0f,
               GET(e.cosmicOcean.atmosphere.brightnessFalloff), SETF(e.cosmicOcean.atmosphere.brightnessFalloff)),
    floatField("depthSaturationFalloff", "Depth saturation falloff", 0.0f, 1.0f, 0.0f, 1.0f,
               GET(e.cosmicOcean.atmosphere.saturationFalloff), SETF(e.cosmicOcean.atmosphere.saturationFalloff)),
    floatField("depthContrastFalloff", "Depth contrast falloff", 0.0f, 1.0f, 0.0f, 1.0f,
               GET(e.cosmicOcean.atmosphere.contrastFalloff), SETF(e.cosmicOcean.atmosphere.contrastFalloff)),
    floatField("scattering", "Scattering", 0.0f, 1.0f, 0.0f, 1.0f,
               GET(e.cosmicOcean.atmosphere.scattering), SETF(e.cosmicOcean.atmosphere.scattering)),
    floatField("glow", "Glow", 0.0f, 8.0f, 0.0f, 2.0f,
               GET(e.cosmicOcean.atmosphere.glow), SETF(e.cosmicOcean.atmosphere.glow)),
    floatField("colorSlowSpeed", "Slow (minutes)", 0.0f, 1.0f, 0.0f, 0.05f,
               GET(e.cosmicOcean.color.slowSpeed), SETF(e.cosmicOcean.color.slowSpeed)).sec("Colour evolution").fmt("%.4f /s"),
    floatField("colorMediumSpeed", "Medium (tens of seconds)", 0.0f, 2.0f, 0.0f, 0.2f,
               GET(e.cosmicOcean.color.mediumSpeed), SETF(e.cosmicOcean.color.mediumSpeed)).fmt("%.3f /s"),
    floatField("colorFastSpeed", "Fast (seconds)", 0.0f, 20.0f, 0.0f, 4.0f,
               GET(e.cosmicOcean.color.fastSpeed), SETF(e.cosmicOcean.color.fastSpeed)).fmt("%.2f /s"),
    floatField("colorFastAmount", "Fast amount", 0.0f, 1.0f, 0.0f, 0.5f,
               GET(e.cosmicOcean.color.fastAmount), SETF(e.cosmicOcean.color.fastAmount)),
    floatField("globalSpeed", "Global speed", 0.0f, 20.0f, 0.0f, 4.0f,
               GET(e.cosmicOcean.flow.speed), SETF(e.cosmicOcean.flow.speed)).sec("Motion"),
    floatField("flowStrength", "Flow strength", 0.0f, 4.0f, 0.0f, 1.0f,
               GET(e.cosmicOcean.flow.strength), SETF(e.cosmicOcean.flow.strength)),
    floatField("flowAzimuth", "Flow azimuth", -360.0f, 360.0f, 0.0f, 360.0f,
               GET(e.cosmicOcean.flow.directionAzimuth), SETF(e.cosmicOcean.flow.directionAzimuth)).fmt("%.0f deg"),
    floatField("flowElevation", "Flow elevation", -90.0f, 90.0f, -90.0f, 90.0f,
               GET(e.cosmicOcean.flow.directionElevation), SETF(e.cosmicOcean.flow.directionElevation)).fmt("%.0f deg"),
    floatField("flowTurbulence", "Flow turbulence", 0.0f, 4.0f, 0.0f, 1.0f,
               GET(e.cosmicOcean.flow.turbulence), SETF(e.cosmicOcean.flow.turbulence)),
    floatField("flowCurl", "Flow curl", 0.0f, 1.0f, 0.0f, 1.0f,
               GET(e.cosmicOcean.flow.curl), SETF(e.cosmicOcean.flow.curl)),
    floatField("flowScale", "Flow scale", 0.01f, 20.0f, 0.1f, 4.0f,
               GET(e.cosmicOcean.flow.scale), SETF(e.cosmicOcean.flow.scale)),
    floatField("flowEvolve", "Flow evolution", 0.0f, 2.0f, 0.0f, 0.2f,
               GET(e.cosmicOcean.flow.evolveSpeed), SETF(e.cosmicOcean.flow.evolveSpeed)).fmt("%.3f /s"),
    floatField("starDrift", "Star drift", -1.0f, 1.0f, -0.05f, 0.05f,
               GET(e.cosmicOcean.starsNear.drift), SETF(e.cosmicOcean.starsNear.drift)).fmt("%.4f rad/s"),
    floatField("nebulaMidFlow", "Mid nebula flow", 0.0f, 2.0f, 0.0f, 0.2f,
               GET(e.cosmicOcean.nebulaMid.flowSpeed), SETF(e.cosmicOcean.nebulaMid.flowSpeed)).fmt("%.3f /s"),
    floatField("nebulaFarFlow", "Far nebula flow", 0.0f, 2.0f, 0.0f, 0.2f,
               GET(e.cosmicOcean.nebulaFar.flowSpeed), SETF(e.cosmicOcean.nebulaFar.flowSpeed)).fmt("%.3f /s"),
    floatField("nebulaFarEvolve", "Far nebula evolution", 0.0f, 2.0f, 0.0f, 0.2f,
               GET(e.cosmicOcean.nebulaFar.evolveSpeed), SETF(e.cosmicOcean.nebulaFar.evolveSpeed)).fmt("%.3f /s"),
    floatField("planetDriftDirection", "Planet drift direction", -360.0f, 360.0f, 0.0f, 360.0f,
               GET(e.cosmicOcean.planets.driftDirection), SETF(e.cosmicOcean.planets.driftDirection)).fmt("%.0f deg"),
    floatField("shootingStars", "Shooting stars", 0.0f, 120.0f, 0.0f, 6.0f,
               GET(e.cosmicOcean.events.shootingStars), SETF(e.cosmicOcean.events.shootingStars)).sec("Events").fmt("%.2f /min"),
    floatField("eventComets", "Comets", 0.0f, 60.0f, 0.0f, 2.0f,
               GET(e.cosmicOcean.events.comets), SETF(e.cosmicOcean.events.comets)).fmt("%.2f /min"),
    floatField("eventFlares", "Flares", 0.0f, 60.0f, 0.0f, 3.0f,
               GET(e.cosmicOcean.events.flares), SETF(e.cosmicOcean.events.flares)).fmt("%.2f /min"),
    floatField("eventPulses", "Pulses", 0.0f, 60.0f, 0.0f, 3.0f,
               GET(e.cosmicOcean.events.pulses), SETF(e.cosmicOcean.events.pulses)).fmt("%.2f /min"),
    floatField("eventProbability", "Probability", 0.0f, 1.0f, 0.0f, 1.0f,
               GET(e.cosmicOcean.events.probability), SETF(e.cosmicOcean.events.probability)),
    floatField("eventIntensity", "Intensity", 0.0f, 16.0f, 0.0f, 4.0f,
               GET(e.cosmicOcean.events.intensity), SETF(e.cosmicOcean.events.intensity)),
    floatField("eventSize", "Size", 0.0f, 16.0f, 0.0f, 4.0f,
               GET(e.cosmicOcean.events.size), SETF(e.cosmicOcean.events.size)),
    floatField("eventSpeed", "Speed", 0.0f, 16.0f, 0.0f, 4.0f,
               GET(e.cosmicOcean.events.speed), SETF(e.cosmicOcean.events.speed)),
    floatField("eventLifetime", "Lifetime", 0.01f, 60.0f, 0.2f, 8.0f,
               GET(e.cosmicOcean.events.lifetime), SETF(e.cosmicOcean.events.lifetime)).fmt("%.2f s"),
    colorField("colorEvent", "Event colour",
               GET(e.cosmicOcean.events.color), SETC(e.cosmicOcean.events.color)),
    floatField("maskAmount", "Amount", 0.0f, 1.0f, 0.0f, 1.0f,
               GET(e.cosmicOcean.mask.amount), SETF(e.cosmicOcean.mask.amount)).sec("Composition mask"),
    floatField("maskAzimuth", "Azimuth", -360.0f, 360.0f, 0.0f, 360.0f,
               GET(e.cosmicOcean.mask.azimuth), SETF(e.cosmicOcean.mask.azimuth)).fmt("%.0f deg"),
    floatField("maskElevation", "Elevation", -90.0f, 90.0f, -90.0f, 90.0f,
               GET(e.cosmicOcean.mask.elevation), SETF(e.cosmicOcean.mask.elevation)).fmt("%.0f deg"),
    floatField("maskInnerAngle", "Inner angle", 0.0f, 180.0f, 0.0f, 90.0f,
               GET(e.cosmicOcean.mask.innerAngle), SETF(e.cosmicOcean.mask.innerAngle)).fmt("%.0f deg"),
    floatField("maskOuterAngle", "Outer angle", 0.0f, 180.0f, 0.0f, 120.0f,
               GET(e.cosmicOcean.mask.outerAngle), SETF(e.cosmicOcean.mask.outerAngle)).fmt("%.0f deg"),
    floatField("maskSuppression", "Suppression", 0.0f, 1.0f, 0.0f, 1.0f,
               GET(e.cosmicOcean.mask.suppression), SETF(e.cosmicOcean.mask.suppression)),
    floatField("maskHorizonBias", "Horizon bias", -1.0f, 1.0f, -1.0f, 1.0f,
               GET(e.cosmicOcean.mask.horizonBias), SETF(e.cosmicOcean.mask.horizonBias)),
    floatField("qualityNebulaOctaves", "Nebula octaves", 1.0f, 6.0f, 1.0f, 6.0f,
               GET(e.cosmicOcean.quality.nebulaOctaves), SETF(e.cosmicOcean.quality.nebulaOctaves)).sec("Quality").fmt("%.0f"),
    floatField("qualityStarStrata", "Star layers", 1.0f, 4.0f, 1.0f, 4.0f,
               GET(e.cosmicOcean.quality.starStrata), SETF(e.cosmicOcean.quality.starStrata)).fmt("%.0f"),
    floatField("qualityPlanetCells", "Planet samples", 1.0f, 5.0f, 1.0f, 5.0f,
               GET(e.cosmicOcean.quality.planetCells), SETF(e.cosmicOcean.quality.planetCells)).fmt("%.0f"),
    floatField("qualityDustCells", "Dust samples", 0.0f, 5.0f, 0.0f, 5.0f,
               GET(e.cosmicOcean.quality.dustCells), SETF(e.cosmicOcean.quality.dustCells)).fmt("%.0f"),
    floatField("centerX", "Centre X", -1.0e6f, 1.0e6f, -2000.0f, 2000.0f,
               GET(e.cosmicOcean.center.x), SETF(e.cosmicOcean.center.x)).sec("Placement").fmt("%.1f m"),
    floatField("centerY", "Centre Y", -1.0e6f, 1.0e6f, -2000.0f, 2000.0f,
               GET(e.cosmicOcean.center.y), SETF(e.cosmicOcean.center.y)).fmt("%.1f m"),
    floatField("centerZ", "Centre Z", -1.0e6f, 1.0e6f, -2000.0f, 2000.0f,
               GET(e.cosmicOcean.center.z), SETF(e.cosmicOcean.center.z)).fmt("%.1f m"),
    floatField("nebulaMidColorMix", "Mid nebula colour mix", 0.0f, 1.0f, 0.0f, 1.0f,
               GET(e.cosmicOcean.nebulaMid.colorMix), SETF(e.cosmicOcean.nebulaMid.colorMix)),
    floatField("nebulaFarColorMix", "Far nebula colour mix", 0.0f, 1.0f, 0.0f, 1.0f,
               GET(e.cosmicOcean.nebulaFar.colorMix), SETF(e.cosmicOcean.nebulaFar.colorMix)),
    floatField("nebulaFarScale", "Far nebula scale", 0.01f, 40.0f, 0.2f, 8.0f,
               GET(e.cosmicOcean.nebulaFar.scale), SETF(e.cosmicOcean.nebulaFar.scale)),
    floatField("nebulaFarTurbulence", "Far nebula turbulence", 0.0f, 3.0f, 0.0f, 1.0f,
               GET(e.cosmicOcean.nebulaFar.turbulence), SETF(e.cosmicOcean.nebulaFar.turbulence)),
    floatField("nebulaFarWarp", "Far nebula warp", 0.0f, 4.0f, 0.0f, 1.5f,
               GET(e.cosmicOcean.nebulaFar.warp), SETF(e.cosmicOcean.nebulaFar.warp)),
    floatField("nebulaFarContrast", "Far nebula contrast", 0.05f, 8.0f, 0.5f, 4.0f,
               GET(e.cosmicOcean.nebulaFar.contrast), SETF(e.cosmicOcean.nebulaFar.contrast)),
    floatField("nebulaFarShimmer", "Far nebula shimmer", 0.0f, 4.0f, 0.0f, 1.0f,
               GET(e.cosmicOcean.nebulaFar.shimmer), SETF(e.cosmicOcean.nebulaFar.shimmer)),
    floatField("nebulaFarDetail", "Far nebula detail", 1.0f, 6.0f, 1.0f, 6.0f,
               GET(e.cosmicOcean.nebulaFar.detail), SETF(e.cosmicOcean.nebulaFar.detail)).fmt("%.0f"),
    floatField("nebulaMidDetail", "Mid nebula detail", 1.0f, 6.0f, 1.0f, 6.0f,
               GET(e.cosmicOcean.nebulaMid.detail), SETF(e.cosmicOcean.nebulaMid.detail)).fmt("%.0f"),
    floatField("nebulaMidSoftness", "Mid nebula softness", 0.0f, 1.0f, 0.0f, 1.0f,
               GET(e.cosmicOcean.nebulaMid.softness), SETF(e.cosmicOcean.nebulaMid.softness)),
    floatField("starSizeVariation", "Star size variation", 0.0f, 1.0f, 0.0f, 1.0f,
               GET(e.cosmicOcean.starsNear.sizeVariance), SETF(e.cosmicOcean.starsNear.sizeVariance)),
    floatField("starsNearBrightness", "Near star brightness", 0.0f, 40.0f, 0.0f, 10.0f,
               GET(e.cosmicOcean.starsNear.stratum.brightness), SETF(e.cosmicOcean.starsNear.stratum.brightness)),
    floatField("starsMidBrightness", "Mid star brightness", 0.0f, 40.0f, 0.0f, 6.0f,
               GET(e.cosmicOcean.starsMid.stratum.brightness), SETF(e.cosmicOcean.starsMid.stratum.brightness)),
    floatField("starsFarBrightness", "Far star brightness", 0.0f, 40.0f, 0.0f, 4.0f,
               GET(e.cosmicOcean.starsFar.stratum.brightness), SETF(e.cosmicOcean.starsFar.stratum.brightness)),
    floatField("starsUltraBrightness", "Ultra star brightness", 0.0f, 40.0f, 0.0f, 2.0f,
               GET(e.cosmicOcean.starsUltra.stratum.brightness), SETF(e.cosmicOcean.starsUltra.stratum.brightness)),
    floatField("planetClustering", "Planet clustering", 0.0f, 1.0f, 0.0f, 1.0f,
               GET(e.cosmicOcean.planets.clustering), SETF(e.cosmicOcean.planets.clustering)),
    floatField("planetTerminator", "Planet terminator", 0.001f, 1.0f, 0.02f, 0.6f,
               GET(e.cosmicOcean.planets.terminator), SETF(e.cosmicOcean.planets.terminator)),
    floatField("planetNightSide", "Planet night side", 0.0f, 1.0f, 0.0f, 0.4f,
               GET(e.cosmicOcean.planets.nightSide), SETF(e.cosmicOcean.planets.nightSide)),
    floatField("planetRingSize", "Ring size", 1.0f, 6.0f, 1.2f, 3.5f,
               GET(e.cosmicOcean.planets.ringSize), SETF(e.cosmicOcean.planets.ringSize)).fmt("%.2f x"),
    floatField("planetRingBrightness", "Ring brightness", 0.0f, 8.0f, 0.0f, 2.0f,
               GET(e.cosmicOcean.planets.ringBrightness), SETF(e.cosmicOcean.planets.ringBrightness)),
    floatField("planetColorVariation", "Planet colour variation", 0.0f, 1.0f, 0.0f, 1.0f,
               GET(e.cosmicOcean.planets.colorVariation), SETF(e.cosmicOcean.planets.colorVariation)),
};

// ---- presets -------------------------------------------------------------------------------
//
// Six, and they are palettes rather than compositions: ADR-390 §14 insists a style must emerge from
// controls an artist can then move, not carry hidden state. `applyCosmicOceanStyle` is the function
// that already did this and it is kept -- wrapping it rather than reimplementing it is what stops
// the preset drifting from the one the file format already names.
constexpr std::string_view kStyleNames[] = {
    "Blue Cosmic Ocean", "Emerald Cosmic Ocean", "Magenta Nebula Ocean",
    "Golden Cosmic Ocean", "Alien Dream", "Deep Void",
};

template <std::size_t N>
void applyNamed(AtmosphericEffect& e) {
    applyCosmicOceanStyle(e.cosmicOcean, kStyleNames[N]);
}

constexpr EffectStyle kStyles[] = {
    {kStyleNames[0].data(), applyNamed<0>}, {kStyleNames[1].data(), applyNamed<1>},
    {kStyleNames[2].data(), applyNamed<2>}, {kStyleNames[3].data(), applyNamed<3>},
    {kStyleNames[4].data(), applyNamed<4>}, {kStyleNames[5].data(), applyNamed<5>},
};

// ---- default audio routes ------------------------------------------------------------------
//
// Authored here, because ADR-390 shipped none and the registry asks for at least one. The depths
// are small fractions of each leaf's soft range and every one is `Add`, so silence leaves the
// authored sky exactly as it was written -- the rule the whole family follows, and the reason an
// unplayed project does not look wrong.
//
// The nebula is the one that reads: a cloud that brightens on the low end is the sky answering the
// music, and it is the largest thing in frame. Twinkle on the treble is the smallest, which is the
// right way round -- the big slow thing on the big slow band.
constexpr EffectRoute kRoutes[] = {
    {"audio.bass", "nebulaMidBrightness", 0.15f, 120.0f, 800.0f},
    {"audio.mid", "nebulaMidShimmer", 0.10f, 90.0f, 600.0f},
    {"audio.treble", "starTwinkle", 0.12f, 30.0f, 280.0f},
    {"audio.rms", "dustDensity", 0.06f, 150.0f, 900.0f},
    {"beat.pulse", "galaxiesBrightness", 0.08f, 15.0f, 420.0f},
};

AtmosphericEffect make(std::string name) {
    AtmosphericEffect e;
    e.name = std::move(name);
    e.kind = AtmosphereKind::CosmicOcean;
    // `defaultCosmicOcean()` and not the struct's member defaults: the members have to be NEUTRAL
    // so a field round-trips to itself, and the default LOOK has to be worth rendering, and those
    // are not the same numbers. `cosmic_ocean.hpp` says the same thing at the declaration.
    e.cosmicOcean = defaultCosmicOcean();
    e.style = std::string(kStyleNames[0]);
    applyCosmicOceanStyle(e.cosmicOcean, kStyleNames[0]);
    // Scenery, not an event, and for the vortex's reasons: it is the background, it is there for
    // the whole shot, and a fade on it is a fade on the sky.
    e.activation = Activation::Always;
    e.timing.fadeIn = 0.0;
    e.timing.fadeOut = 0.0;
    // No ground glow. A cosmic background does not light the island (ADR-390 §10), and a
    // `GroundIllumination` that did nothing would be a control that lies.
    e.ground.mode = GroundGlow::Off;
    return e;
}

// Nothing per-frame to resolve, exactly like the vortex: every celestial body in it is a hash the
// fragment shader evaluates, so there is no trajectory on the CPU. What resolution decides is "is
// it live, and how far through its fade", and the envelope in `base` already carries that.
bool fill(const AtmosphericEffect&, std::size_t, const AtmosphericContext&,
          const ResolvedAtmospheric& base, ResolvedAtmospheric& r) {
    r = base;
    return true;
}

Result<void> validate(const AtmosphericEffect& e) { return e.cosmicOcean.validate(); }

EffectSchema buildSchema() {
    EffectSchema s;
    s.kind = AtmosphereKind::CosmicOcean;
    s.key = "cosmicOcean";
    s.enumName = "CosmicOcean";
    s.displayName = "Cosmic Ocean";
    s.addLabel = "Add cosmic ocean";
    s.addTip = "A procedural deep-space background: nebulae, star strata, planets, dust and\n"
               "galaxies, each with its own depth and parallax. It is drawn behind everything\n"
               "by a pass of its own, so it replaces the sky rather than sitting in front of it.\n"
               "One per scene; a second is counted and reported.";
    s.fields = kFields;
    s.styles = kStyles;
    s.routes = kRoutes;
    s.beatLeaf = "galaxiesBrightness";
    s.groundGlow = false;
    s.factory = make;
    s.resolve.bucket = EffectBucket::CosmicOcean;
    s.resolve.fill = fill;
    s.validate = validate;
    return s;
}

} // namespace

const EffectSchema& cosmicOceanSchema() {
    static const EffectSchema kSchema = buildSchema();
    return kSchema;
}

} // namespace avgen::world
