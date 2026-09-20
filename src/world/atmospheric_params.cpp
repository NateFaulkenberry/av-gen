#include "world/atmospheric_params.hpp"

#include "params/parameter_set.hpp"

#include <algorithm>

namespace avgen::world {
namespace {

using Effect = AtmosphericEffect;

// ---- the tables ----------------------------------------------------------------------------------
//
// One row per parameter, carrying its range and an accessor pair. Register, apply and capture are
// each one loop over these, so a parameter that exists works in all three directions or in none --
// which is the failure `effect_params.cpp`'s three parallel lists can have and this cannot.
//
// The soft range is what the UI offers; the hard range is what a modulation route is clamped to. The
// two differ wherever an authored value may legitimately go past what a slider should reach for --
// every HDR radiance here has a hard ceiling well above its soft one, because a route driving a
// comet's core on a drop is exactly the thing this system is for.

struct FloatField {
    const char* leaf;
    float lo, hi, slo, shi;
    float (*get)(const Effect&);
    void (*set)(Effect&, float);
};

struct ColorField {
    const char* leaf;
    glm::vec3 (*get)(const Effect&);
    void (*set)(Effect&, glm::vec3);
};

struct BoolField {
    const char* leaf;
    bool (*get)(const Effect&);
    void (*set)(Effect&, bool);
};

#define F_GET(expr) +[](const Effect& e) { return (expr); }
#define F_SET(lhs) +[](Effect& e, float v) { (lhs) = v; }
#define C_SET(lhs) +[](Effect& e, glm::vec3 v) { (lhs) = v; }
#define B_SET(lhs) +[](Effect& e, bool v) { (lhs) = v; }

// Lifecycle and ground illumination: the same questions whatever the effect is, so the same rows.
constexpr FloatField kSharedFloats[] = {
    {"groundIntensity", 0.0f, 12.0f, 0.0f, 3.0f, F_GET(e.ground.intensity), F_SET(e.ground.intensity)},
    {"groundRadius", 1.0f, 4000.0f, 20.0f, 800.0f, F_GET(e.ground.radius), F_SET(e.ground.radius)},
    {"groundFalloff", 0.05f, 8.0f, 0.5f, 4.0f, F_GET(e.ground.falloff), F_SET(e.ground.falloff)},
    // Timing is float here and double on the effect: ADR-011 says a parameter is float components.
    {"delay", 0.0f, 300.0f, 0.0f, 20.0f, F_GET(static_cast<float>(e.timing.delay)),
     +[](Effect& e, float v) { e.timing.delay = v; }},
    {"lifetime", 0.0f, 900.0f, 0.0f, 60.0f, F_GET(static_cast<float>(e.timing.lifetime)),
     +[](Effect& e, float v) { e.timing.lifetime = v; }},
    {"fadeIn", 0.0f, 60.0f, 0.0f, 8.0f, F_GET(static_cast<float>(e.timing.fadeIn)),
     +[](Effect& e, float v) { e.timing.fadeIn = v; }},
    {"fadeOut", 0.0f, 60.0f, 0.0f, 8.0f, F_GET(static_cast<float>(e.timing.fadeOut)),
     +[](Effect& e, float v) { e.timing.fadeOut = v; }},
    {"windowStart", 0.0f, 3600.0f, 0.0f, 240.0f, F_GET(static_cast<float>(e.timing.windowStart)),
     +[](Effect& e, float v) { e.timing.windowStart = v; }},
    {"windowSeconds", 0.0f, 3600.0f, 0.0f, 120.0f, F_GET(static_cast<float>(e.timing.windowSeconds)),
     +[](Effect& e, float v) { e.timing.windowSeconds = v; }},
    {"repeat", 0.0f, 600.0f, 0.0f, 30.0f, F_GET(static_cast<float>(e.timing.repeatSeconds)),
     +[](Effect& e, float v) { e.timing.repeatSeconds = v; }},
};

constexpr ColorField kSharedColors[] = {
    {"groundColor", F_GET(e.ground.color), C_SET(e.ground.color)},
};

constexpr FloatField kCometFloats[] = {
    // Appearance. Every radiance's hard ceiling is far above its soft one: a route driving the core
    // on a drop is the reason this system exists, and a hard clamp at the slider's end would make
    // the route do nothing at exactly the moment it matters.
    {"coreIntensity", 0.0f, 200.0f, 0.0f, 40.0f, F_GET(e.comet.appearance.coreIntensity),
     F_SET(e.comet.appearance.coreIntensity)},
    {"headSize", 0.5f, 400.0f, 2.0f, 80.0f, F_GET(e.comet.appearance.headSize), F_SET(e.comet.appearance.headSize)},
    {"haloIntensity", 0.0f, 60.0f, 0.0f, 10.0f, F_GET(e.comet.appearance.haloIntensity),
     F_SET(e.comet.appearance.haloIntensity)},
    {"haloSize", 1.0f, 2000.0f, 10.0f, 400.0f, F_GET(e.comet.appearance.haloSize), F_SET(e.comet.appearance.haloSize)},
    {"tailIntensity", 0.0f, 80.0f, 0.0f, 15.0f, F_GET(e.comet.appearance.tailIntensity),
     F_SET(e.comet.appearance.tailIntensity)},
    {"tailLength", 10.0f, 6000.0f, 100.0f, 2000.0f, F_GET(e.comet.appearance.tailLength),
     F_SET(e.comet.appearance.tailLength)},
    {"tailWidth", 1.0f, 600.0f, 5.0f, 200.0f, F_GET(e.comet.appearance.tailWidth), F_SET(e.comet.appearance.tailWidth)},
    {"tailFalloff", 0.05f, 8.0f, 0.5f, 4.0f, F_GET(e.comet.appearance.tailFalloff),
     F_SET(e.comet.appearance.tailFalloff)},
    {"wispAmount", 0.0f, 400.0f, 0.0f, 150.0f, F_GET(e.comet.appearance.wispAmount),
     F_SET(e.comet.appearance.wispAmount)},
    {"wispScale", 0.0f, 0.05f, 0.0f, 0.01f, F_GET(e.comet.appearance.wispScale), F_SET(e.comet.appearance.wispScale)},
    {"flowSpeed", -4.0f, 4.0f, -1.0f, 1.0f, F_GET(e.comet.appearance.flowSpeed), F_SET(e.comet.appearance.flowSpeed)},
    // Sparkle. `density` is fragments per metre of trail, so its numbers are small: a 900 m tail at
    // 0.016 sheds fourteen.
    {"sparkleDensity", 0.0f, 0.2f, 0.0f, 0.05f, F_GET(e.comet.sparkle.density), F_SET(e.comet.sparkle.density)},
    {"sparkleSize", 0.0f, 1.0f, 0.02f, 0.8f, F_GET(e.comet.sparkle.size), F_SET(e.comet.sparkle.size)},
    {"sparkleIntensity", 0.0f, 80.0f, 0.0f, 20.0f, F_GET(e.comet.sparkle.intensity), F_SET(e.comet.sparkle.intensity)},
    {"sparkleSpeed", 0.0f, 12.0f, 0.0f, 4.0f, F_GET(e.comet.sparkle.speed), F_SET(e.comet.sparkle.speed)},
    // Rainbow.
    {"rainbowSpeed", -4.0f, 4.0f, -1.0f, 1.0f, F_GET(e.comet.rainbow.speed), F_SET(e.comet.rainbow.speed)},
    {"rainbowScale", 0.0f, 12.0f, 0.0f, 4.0f, F_GET(e.comet.rainbow.scale), F_SET(e.comet.rainbow.scale)},
    {"rainbowHue", -4.0f, 4.0f, 0.0f, 1.0f, F_GET(e.comet.rainbow.hueOffset), F_SET(e.comet.rainbow.hueOffset)},
    {"rainbowSaturation", 0.0f, 1.0f, 0.0f, 1.0f, F_GET(e.comet.rainbow.saturation),
     F_SET(e.comet.rainbow.saturation)},
    {"rainbowBrightness", 0.0f, 4.0f, 0.0f, 2.0f, F_GET(e.comet.rainbow.brightness),
     F_SET(e.comet.rainbow.brightness)},
    // Motion.
    {"startAzimuth", -720.0f, 720.0f, -180.0f, 180.0f, F_GET(e.comet.path.startAzimuth),
     F_SET(e.comet.path.startAzimuth)},
    {"startElevation", -20.0f, 89.0f, 0.0f, 80.0f, F_GET(e.comet.path.startElevation),
     F_SET(e.comet.path.startElevation)},
    {"endAzimuth", -720.0f, 720.0f, -180.0f, 180.0f, F_GET(e.comet.path.endAzimuth), F_SET(e.comet.path.endAzimuth)},
    {"endElevation", -20.0f, 89.0f, 0.0f, 80.0f, F_GET(e.comet.path.endElevation), F_SET(e.comet.path.endElevation)},
    {"distance", 50.0f, 40000.0f, 400.0f, 8000.0f, F_GET(e.comet.path.distance), F_SET(e.comet.path.distance)},
    {"travelSeconds", 0.05f, 600.0f, 1.0f, 30.0f, F_GET(e.comet.path.travelSeconds),
     F_SET(e.comet.path.travelSeconds)},
    {"speed", 0.01f, 20.0f, 0.1f, 4.0f, F_GET(e.comet.path.speedScale), F_SET(e.comet.path.speedScale)},
    // Floored just above -0.5, where the reparameterisation stops being monotone; the hard range is
    // what a route is clamped to, so this is the clamp that stops a modulated comet reversing.
    {"acceleration", -0.45f, 8.0f, -0.4f, 3.0f, F_GET(e.comet.path.acceleration), F_SET(e.comet.path.acceleration)},
    {"curvature", -4000.0f, 4000.0f, -800.0f, 800.0f, F_GET(e.comet.path.curvature), F_SET(e.comet.path.curvature)},
    {"arcLift", -4000.0f, 4000.0f, -600.0f, 600.0f, F_GET(e.comet.path.arcLift), F_SET(e.comet.path.arcLift)},
};

constexpr ColorField kCometColors[] = {
    {"coreColor", F_GET(e.comet.appearance.coreColor), C_SET(e.comet.appearance.coreColor)},
    {"haloColor", F_GET(e.comet.appearance.haloColor), C_SET(e.comet.appearance.haloColor)},
    {"tailColor", F_GET(e.comet.appearance.tailColor), C_SET(e.comet.appearance.tailColor)},
};

constexpr BoolField kCometBools[] = {
    {"sparkle", F_GET(e.comet.sparkle.enabled), B_SET(e.comet.sparkle.enabled)},
    {"rainbow", F_GET(e.comet.rainbow.enabled), B_SET(e.comet.rainbow.enabled)},
};

constexpr FloatField kAuroraFloats[] = {
    // Appearance.
    {"intensity", 0.0f, 60.0f, 0.0f, 8.0f, F_GET(e.aurora.appearance.intensity), F_SET(e.aurora.appearance.intensity)},
    {"emission", 0.0f, 2.0f, 0.0f, 1.0f, F_GET(e.aurora.appearance.emission), F_SET(e.aurora.appearance.emission)},
    {"opacity", 0.0f, 1.0f, 0.0f, 1.0f, F_GET(e.aurora.appearance.opacity), F_SET(e.aurora.appearance.opacity)},
    {"edgeBrightness", 0.0f, 30.0f, 0.0f, 6.0f, F_GET(e.aurora.appearance.edgeBrightness),
     F_SET(e.aurora.appearance.edgeBrightness)},
    {"filaments", 0.0f, 10.0f, 0.0f, 3.0f, F_GET(e.aurora.appearance.filaments), F_SET(e.aurora.appearance.filaments)},
    {"sparkle", 0.0f, 10.0f, 0.0f, 3.0f, F_GET(e.aurora.appearance.sparkle), F_SET(e.aurora.appearance.sparkle)},
    {"horizonGlow", 0.0f, 6.0f, 0.0f, 2.0f, F_GET(e.aurora.appearance.horizonGlow),
     F_SET(e.aurora.appearance.horizonGlow)},
    // Shape. `curtains` is hard-clamped to the shader's loop bound, because a route that pushed it
    // to seven would silently do nothing past five and that is a control that lies.
    {"curtains", 1.0f, 5.0f, 1.0f, 5.0f, F_GET(e.aurora.shape.curtainCount), F_SET(e.aurora.shape.curtainCount)},
    {"radius", 200.0f, 40000.0f, 1000.0f, 12000.0f, F_GET(e.aurora.shape.radius), F_SET(e.aurora.shape.radius)},
    {"layerSpacing", 0.0f, 3.0f, 0.0f, 1.0f, F_GET(e.aurora.shape.layerSpacing), F_SET(e.aurora.shape.layerSpacing)},
    {"baseHeight", -4000.0f, 4000.0f, -400.0f, 400.0f, F_GET(e.aurora.shape.baseHeight),
     F_SET(e.aurora.shape.baseHeight)},
    {"curtainHeight", 50.0f, 30000.0f, 400.0f, 8000.0f, F_GET(e.aurora.shape.curtainHeight),
     F_SET(e.aurora.shape.curtainHeight)},
    {"waveAmplitude", 0.0f, 3.0f, 0.0f, 1.0f, F_GET(e.aurora.shape.waveAmplitude),
     F_SET(e.aurora.shape.waveAmplitude)},
    {"waveScale", 0.0f, 30.0f, 0.2f, 8.0f, F_GET(e.aurora.shape.waveScale), F_SET(e.aurora.shape.waveScale)},
    {"turbulence", 0.0f, 3.0f, 0.0f, 1.5f, F_GET(e.aurora.shape.turbulence), F_SET(e.aurora.shape.turbulence)},
    {"complexity", 0.0f, 160.0f, 4.0f, 60.0f, F_GET(e.aurora.shape.complexity), F_SET(e.aurora.shape.complexity)},
    {"flowSpeed", -2.0f, 2.0f, -0.4f, 0.4f, F_GET(e.aurora.shape.flowSpeed), F_SET(e.aurora.shape.flowSpeed)},
    {"driftSpeed", -4.0f, 4.0f, -1.0f, 1.0f, F_GET(e.aurora.shape.driftSpeed), F_SET(e.aurora.shape.driftSpeed)},
    {"verticalSpeed", -4.0f, 4.0f, -1.0f, 1.0f, F_GET(e.aurora.shape.verticalSpeed),
     F_SET(e.aurora.shape.verticalSpeed)},
    // Audio response. These are depths on signals that already exist, not an analyzer.
    {"audioBass", 0.0f, 6.0f, 0.0f, 2.0f, F_GET(e.aurora.audio.bass), F_SET(e.aurora.audio.bass)},
    {"audioLowMid", 0.0f, 6.0f, 0.0f, 2.0f, F_GET(e.aurora.audio.lowMid), F_SET(e.aurora.audio.lowMid)},
    {"audioMid", 0.0f, 6.0f, 0.0f, 2.0f, F_GET(e.aurora.audio.mid), F_SET(e.aurora.audio.mid)},
    {"audioHigh", 0.0f, 6.0f, 0.0f, 2.0f, F_GET(e.aurora.audio.high), F_SET(e.aurora.audio.high)},
    {"audioBeat", 0.0f, 6.0f, 0.0f, 2.0f, F_GET(e.aurora.audio.beat), F_SET(e.aurora.audio.beat)},
    {"audioSensitivity", 0.0f, 6.0f, 0.0f, 3.0f, F_GET(e.aurora.audio.sensitivity),
     F_SET(e.aurora.audio.sensitivity)},
    {"spectrumShape", 0.0f, 1.0f, 0.0f, 1.0f, F_GET(e.aurora.audio.spectrumShape),
     F_SET(e.aurora.audio.spectrumShape)},
    // Rainbow.
    {"rainbowSpeed", -4.0f, 4.0f, -0.5f, 0.5f, F_GET(e.aurora.rainbow.speed), F_SET(e.aurora.rainbow.speed)},
    {"rainbowScale", 0.0f, 12.0f, 0.0f, 3.0f, F_GET(e.aurora.rainbow.scale), F_SET(e.aurora.rainbow.scale)},
    {"rainbowHue", -4.0f, 4.0f, 0.0f, 1.0f, F_GET(e.aurora.rainbow.hueOffset), F_SET(e.aurora.rainbow.hueOffset)},
    {"rainbowSaturation", 0.0f, 1.0f, 0.0f, 1.0f, F_GET(e.aurora.rainbow.saturation),
     F_SET(e.aurora.rainbow.saturation)},
    {"rainbowBrightness", 0.0f, 4.0f, 0.0f, 2.0f, F_GET(e.aurora.rainbow.brightness),
     F_SET(e.aurora.rainbow.brightness)},
};

constexpr ColorField kAuroraColors[] = {
    {"lowColor", F_GET(e.aurora.appearance.lowColor), C_SET(e.aurora.appearance.lowColor)},
    {"midColor", F_GET(e.aurora.appearance.midColor), C_SET(e.aurora.appearance.midColor)},
    {"topColor", F_GET(e.aurora.appearance.topColor), C_SET(e.aurora.appearance.topColor)},
};

constexpr BoolField kAuroraBools[] = {
    {"rainbow", F_GET(e.aurora.rainbow.enabled), B_SET(e.aurora.rainbow.enabled)},
};

// ADR-383: the vortex's parameters, table-driven like the other two kinds so it gains
// registration, apply, capture and default routes without a line of bespoke code.
constexpr FloatField kVortexFloats[] = {
    {"radius", 0.0f, 20000.0f, 0.0f, 1500.0f, F_GET(e.vortex.radius), F_SET(e.vortex.radius)},
    {"thickness", 0.1f, 5000.0f, 5.0f, 600.0f, F_GET(e.vortex.thickness), F_SET(e.vortex.thickness)},
    {"funnelDepth", 0.0f, 20000.0f, 0.0f, 3000.0f, F_GET(e.vortex.funnelDepth), F_SET(e.vortex.funnelDepth)},
    {"throat", 0.02f, 1.0f, 0.05f, 1.0f, F_GET(e.vortex.throat), F_SET(e.vortex.throat)},
    {"throatDensity", 0.0f, 1.0f, 0.0f, 1.0f, F_GET(e.vortex.throatDensity), F_SET(e.vortex.throatDensity)},
    {"swirl", -32.0f, 32.0f, -8.0f, 8.0f, F_GET(e.vortex.swirl), F_SET(e.vortex.swirl)},
    {"rotationSpeed", -4.0f, 4.0f, -0.4f, 0.4f, F_GET(e.vortex.rotationSpeed), F_SET(e.vortex.rotationSpeed)},
    {"turbulence", 0.0f, 1.0f, 0.0f, 1.0f, F_GET(e.vortex.turbulence), F_SET(e.vortex.turbulence)},
    {"turbulenceScale", 0.001f, 40.0f, 0.1f, 8.0f, F_GET(e.vortex.turbulenceScale), F_SET(e.vortex.turbulenceScale)},
    {"density", 0.0f, 8.0f, 0.0f, 0.01f, F_GET(e.vortex.density), F_SET(e.vortex.density)},
    {"emission", 0.0f, 20.0f, 0.0f, 0.2f, F_GET(e.vortex.emission), F_SET(e.vortex.emission)},
    {"contrast", 0.05f, 12.0f, 0.5f, 5.0f, F_GET(e.vortex.contrast), F_SET(e.vortex.contrast)},
    {"innerVoid", 0.0f, 0.95f, 0.0f, 0.6f, F_GET(e.vortex.innerVoid), F_SET(e.vortex.innerVoid)},
    {"filaments", 0.0f, 4.0f, 0.0f, 2.0f, F_GET(e.vortex.filaments), F_SET(e.vortex.filaments)},
    {"breathAmount", 0.0f, 1.0f, 0.0f, 0.3f, F_GET(e.vortex.breathAmount), F_SET(e.vortex.breathAmount)},
    {"breathSpeed", 0.0f, 4.0f, 0.0f, 1.0f, F_GET(e.vortex.breathSpeed), F_SET(e.vortex.breathSpeed)},
    {"spill", 0.0f, 20.0f, 0.0f, 6.0f, F_GET(e.vortex.spill), F_SET(e.vortex.spill)},
    {"cometResponse", 0.0f, 8.0f, 0.0f, 2.0f, F_GET(e.vortex.cometResponse), F_SET(e.vortex.cometResponse)},
    {"cometReach", 1.0f, 40.0f, 1.0f, 12.0f, F_GET(e.vortex.cometReach), F_SET(e.vortex.cometReach)},
    {"centerX", -1e5f, 1e5f, -500.0f, 500.0f, F_GET(e.vortex.center.x), F_SET(e.vortex.center.x)},
    {"centerY", -1e5f, 1e5f, -2000.0f, 500.0f, F_GET(e.vortex.center.y), F_SET(e.vortex.center.y)},
    {"centerZ", -1e5f, 1e5f, -500.0f, 500.0f, F_GET(e.vortex.center.z), F_SET(e.vortex.center.z)},
};
constexpr ColorField kVortexColors[] = {
    {"colorDeep", F_GET(e.vortex.colorDeep), C_SET(e.vortex.colorDeep)},
    {"colorMid", F_GET(e.vortex.colorMid), C_SET(e.vortex.colorMid)},
    {"colorAccent", F_GET(e.vortex.colorAccent), C_SET(e.vortex.colorAccent)},
};

#undef F_GET
#undef F_SET
#undef C_SET
#undef B_SET

std::span<const FloatField> floatFields(AtmosphereKind kind) {
    switch (kind) {
    case AtmosphereKind::Comet: return std::span<const FloatField>(kCometFloats);
    case AtmosphereKind::Vortex: return std::span<const FloatField>(kVortexFloats);
    case AtmosphereKind::Aurora: break;
    }
    return std::span<const FloatField>(kAuroraFloats);
}
std::span<const ColorField> colorFields(AtmosphereKind kind) {
    switch (kind) {
    case AtmosphereKind::Comet: return std::span<const ColorField>(kCometColors);
    case AtmosphereKind::Vortex: return std::span<const ColorField>(kVortexColors);
    case AtmosphereKind::Aurora: break;
    }
    return std::span<const ColorField>(kAuroraColors);
}
std::span<const BoolField> boolFields(AtmosphereKind kind) {
    switch (kind) {
    case AtmosphereKind::Comet: return std::span<const BoolField>(kCometBools);
    // A vortex has no booleans of its own: `enabled` is the effect's, and `radius` 0 is the gate.
    case AtmosphereKind::Vortex: return {};
    case AtmosphereKind::Aurora: break;
    }
    return std::span<const BoolField>(kAuroraBools);
}

// Desc builders, the same shape `effect_params.cpp` uses.
params::ParamDesc<float> f(std::string path, float def, float lo, float hi, float slo, float shi) {
    params::ParamDesc<float> d;
    d.path = std::move(path);
    d.defaultValue = std::clamp(def, lo, hi);
    d.hardMin = lo;
    d.hardMax = hi;
    d.softMin = slo;
    d.softMax = shi;
    return d;
}

params::ParamDesc<bool> b(std::string path, bool def) {
    params::ParamDesc<bool> d;
    d.path = std::move(path);
    d.defaultValue = def;
    d.hardMin = false;
    d.hardMax = true;
    return d;
}

params::ParamDesc<glm::vec3> col(std::string path, glm::vec3 def) {
    params::ParamDesc<glm::vec3> d;
    d.path = std::move(path);
    d.defaultValue = def;
    // HDR headroom on the hard range, the picker's range on the soft one: a comet's core colour is
    // a radiance and a route may legitimately push it past white.
    d.hardMin = glm::vec3(0.0f);
    d.hardMax = glm::vec3(8.0f);
    d.softMin = glm::vec3(0.0f);
    d.softMax = glm::vec3(1.0f);
    d.isColor = true;
    return d;
}

// Belt and braces before the values reach the resolver: a route can drive a final anywhere inside
// the *hard* range, and a few of these are divisors. The hard ranges above already exclude zero
// where it matters, so this is the second line rather than the first.
void sanitise(Effect& e) {
    e.comet.path.travelSeconds = std::max(e.comet.path.travelSeconds, 0.05f);
    e.comet.path.speedScale = std::max(e.comet.path.speedScale, 0.01f);
    e.comet.path.distance = std::max(e.comet.path.distance, 1.0f);
    e.comet.appearance.tailLength = std::max(e.comet.appearance.tailLength, 1.0f);
    e.comet.appearance.headSize = std::max(e.comet.appearance.headSize, 0.1f);
    e.comet.appearance.haloSize = std::max(e.comet.appearance.haloSize, 1.0f);
    e.comet.appearance.tailWidth = std::max(e.comet.appearance.tailWidth, 0.5f);
    e.comet.appearance.tailFalloff = std::max(e.comet.appearance.tailFalloff, 0.05f);
    e.aurora.shape.curtainCount = std::clamp(e.aurora.shape.curtainCount, 1.0f, 5.0f);
    e.aurora.shape.radius = std::max(e.aurora.shape.radius, 1.0f);
    e.aurora.shape.curtainHeight = std::max(e.aurora.shape.curtainHeight, 1.0f);
    e.ground.radius = std::max(e.ground.radius, 1.0f);
    e.ground.falloff = std::max(e.ground.falloff, 0.05f);
}

} // namespace

std::string atmosphericParameterPrefix(std::string_view effectName) {
    std::string prefix = "atmos/";
    prefix.append(effectName);
    prefix.push_back('/');
    return prefix;
}

const AtmosphericParams* AtmosphericParameters::find(std::string_view name) const {
    for (const AtmosphericParams& p : effects) {
        if (p.name == name) {
            return &p;
        }
    }
    return nullptr;
}

AtmosphericParameters registerAtmosphericParameters(params::ParameterSet& params,
                                                    std::span<const AtmosphericEffect> effects) {
    AtmosphericParameters out;
    out.effects.reserve(effects.size());
    for (const AtmosphericEffect& e : effects) {
        const std::string base = atmosphericParameterPrefix(e.name);
        const auto path = [&](const char* leaf) {
            std::string full = base + leaf;
            out.registered.push_back(full); // exact unregister, not a suffix table
            return full;
        };

        AtmosphericParams p;
        p.name = e.name;
        p.kind = e.kind;
        p.enabled = &params.add(b(path("enabled"), e.enabled));

        // Only the rows for the kind this effect actually is. An effect keeps the settings of the
        // kind it is not -- that is why both payloads exist -- but registering both would put ninety
        // parameters in the table for every one an artist can reach, and half of them would do
        // nothing, which is worse than their being absent.
        const auto floats = floatFields(e.kind);
        p.floats.reserve(floats.size() + std::size(kSharedFloats));
        for (const FloatField& field : floats) {
            p.floats.push_back(&params.add(f(path(field.leaf), field.get(e), field.lo, field.hi, field.slo, field.shi)));
        }
        for (const FloatField& field : kSharedFloats) {
            p.floats.push_back(&params.add(f(path(field.leaf), field.get(e), field.lo, field.hi, field.slo, field.shi)));
        }
        const auto colors = colorFields(e.kind);
        p.colors.reserve(colors.size() + std::size(kSharedColors));
        for (const ColorField& field : colors) {
            p.colors.push_back(&params.add(col(path(field.leaf), field.get(e))));
        }
        for (const ColorField& field : kSharedColors) {
            p.colors.push_back(&params.add(col(path(field.leaf), field.get(e))));
        }
        const auto bools = boolFields(e.kind);
        p.flags.reserve(bools.size());
        for (const BoolField& field : bools) {
            p.flags.push_back(&params.add(b(path(field.leaf), field.get(e))));
        }
        out.effects.push_back(std::move(p));
    }
    return out;
}

void unregisterAtmosphericParameters(params::ParameterSet& params, AtmosphericParameters& registered) {
    for (const std::string& path : registered.registered) {
        params.remove(path);
    }
    registered.registered.clear();
    registered.effects.clear();
}

namespace {

// One walk serving both directions, so the two cannot drift. `fromBase` picks what a save wants
// (what somebody authored) over what the renderer wants (this frame's modulated finals).
void copyParameters(const AtmosphericParameters& registered, std::vector<AtmosphericEffect>& effects,
                    bool fromBase) {
    const auto value = [fromBase](const params::IParameter* p, std::size_t component) {
        return fromBase ? p->baseComponent(component) : p->finalComponent(component);
    };
    for (AtmosphericEffect& e : effects) {
        const AtmosphericParams* p = registered.find(e.name);
        // Effects the registrar never saw are skipped rather than zeroed: a scene that added an
        // effect this frame has one the parameter table does not know about yet, and stamping
        // defaults onto it would erase what the file said.
        if (p == nullptr || p->kind != e.kind) {
            continue;
        }
        if (p->enabled != nullptr) {
            e.enabled = value(p->enabled, 0) >= 0.5f;
        }
        const auto floats = floatFields(e.kind);
        std::size_t i = 0;
        for (const FloatField& field : floats) {
            if (i < p->floats.size() && p->floats[i] != nullptr) {
                field.set(e, value(p->floats[i], 0));
            }
            ++i;
        }
        for (const FloatField& field : kSharedFloats) {
            if (i < p->floats.size() && p->floats[i] != nullptr) {
                field.set(e, value(p->floats[i], 0));
            }
            ++i;
        }
        const auto colors = colorFields(e.kind);
        std::size_t c = 0;
        for (const ColorField& field : colors) {
            if (c < p->colors.size() && p->colors[c] != nullptr) {
                const params::IParameter* q = p->colors[c];
                field.set(e, glm::vec3(value(q, 0), value(q, 1), value(q, 2)));
            }
            ++c;
        }
        for (const ColorField& field : kSharedColors) {
            if (c < p->colors.size() && p->colors[c] != nullptr) {
                const params::IParameter* q = p->colors[c];
                field.set(e, glm::vec3(value(q, 0), value(q, 1), value(q, 2)));
            }
            ++c;
        }
        const auto bools = boolFields(e.kind);
        std::size_t k = 0;
        for (const BoolField& field : bools) {
            if (k < p->flags.size() && p->flags[k] != nullptr) {
                field.set(e, value(p->flags[k], 0) >= 0.5f);
            }
            ++k;
        }
        if (!fromBase) {
            sanitise(e);
        }
    }
}

} // namespace

void applyAtmosphericParameters(const AtmosphericParameters& registered, std::vector<AtmosphericEffect>& live) {
    copyParameters(registered, live, false);
}

void captureAtmosphericParameters(const AtmosphericParameters& registered,
                                  std::vector<AtmosphericEffect>& authored) {
    copyParameters(registered, authored, true);
}

std::vector<params::ModRoute> defaultAtmosphericRoutes(std::string_view effectName, AtmosphereKind kind) {
    const std::string base = atmosphericParameterPrefix(effectName);
    std::vector<params::ModRoute> routes;
    const auto add = [&](const char* source, const char* leaf, float amount, float attackMs, float decayMs) {
        params::ModRoute r;
        r.source = source;
        r.target = base + leaf;
        r.amount = amount;
        // `Add` so silence leaves the authored pose exactly as it was written. A `Multiply` route
        // would make an unplayed project look wrong, which is the discipline scene/tree_audio.hpp
        // states and the reason every depth here is small next to the value it moves.
        r.op = params::ModOp::Add;
        r.chain.attackMs = attackMs;
        r.chain.decayMs = decayMs;
        routes.push_back(std::move(r));
    };
    if (kind == AtmosphereKind::Aurora) {
        // §4.2's proposed mapping, as the default rather than as the only answer. The depths are
        // fractions of each parameter's soft range, and the smoothing is what stops the curtain
        // jittering: a 40 ms attack and a ~400 ms release is a curtain that answers the music
        // rather than one that strobes with it.
        add("audio.bass", "curtainHeight", 900.0f, 60.0f, 420.0f);
        add("audio.rms", "intensity", 0.8f, 80.0f, 500.0f);
        add("audio.lowMid", "waveAmplitude", 0.12f, 50.0f, 380.0f);
        add("audio.mid", "turbulence", 0.20f, 40.0f, 320.0f);
        add("audio.treble", "filaments", 0.55f, 25.0f, 240.0f);
        add("beat.pulse", "edgeBrightness", 1.1f, 10.0f, 260.0f);
    } else {
        // A comet is an event, and most of its shape is authored rather than played. What answers
        // the music is its brightness and its sparkle -- the two that read at a glance.
        add("audio.rms", "coreIntensity", 6.0f, 60.0f, 400.0f);
        add("beat.pulse", "tailIntensity", 1.4f, 10.0f, 280.0f);
        add("audio.treble", "sparkleIntensity", 4.0f, 20.0f, 220.0f);
    }
    return routes;
}

} // namespace avgen::world
