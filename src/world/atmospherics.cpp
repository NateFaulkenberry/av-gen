#include "world/atmospherics.hpp"

#include <glm/gtc/constants.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <unordered_set>

namespace avgen::world {
namespace {

using json = nlohmann::json;

constexpr float kEps = 1e-5f;

bool finite(float v) { return std::isfinite(v); }

glm::vec3 vec3FromJson(const json& j, glm::vec3 fallback) {
    if (!j.is_array() || j.size() != 3) {
        return fallback;
    }
    return glm::vec3(j[0].get<float>(), j[1].get<float>(), j[2].get<float>());
}

json vec3ToJson(const glm::vec3& v) { return json::array({v.x, v.y, v.z}); }

float readFloat(const json& j, const char* key, float fallback) {
    return j.contains(key) && j.at(key).is_number() ? j.at(key).get<float>() : fallback;
}
double readDouble(const json& j, const char* key, double fallback) {
    return j.contains(key) && j.at(key).is_number() ? j.at(key).get<double>() : fallback;
}
bool readBool(const json& j, const char* key, bool fallback) {
    return j.contains(key) && j.at(key).is_boolean() ? j.at(key).get<bool>() : fallback;
}
std::string readString(const json& j, const char* key, std::string fallback = {}) {
    return j.contains(key) && j.at(key).is_string() ? j.at(key).get<std::string>() : std::move(fallback);
}
glm::vec3 readVec3(const json& j, const char* key, glm::vec3 fallback) {
    return j.contains(key) ? vec3FromJson(j.at(key), fallback) : fallback;
}

glm::vec3 safeNormalize(const glm::vec3& v, const glm::vec3& fallback) {
    const float len = glm::length(v);
    return len > kEps ? v / len : fallback;
}

// Azimuth is degrees clockwise from +Z, elevation degrees above the horizon. Chosen over "degrees
// from +X counter-clockwise" because a scene's camera is usually written looking down -Z and an
// artist typing 0 should get "straight ahead", not "off to the right".
glm::vec3 directionFromSky(float azimuthDegrees, float elevationDegrees) {
    const float az = glm::radians(azimuthDegrees);
    const float el = glm::radians(std::clamp(elevationDegrees, -89.0f, 89.0f));
    const float horizontal = std::cos(el);
    return glm::vec3(horizontal * std::sin(az), std::sin(el), horizontal * std::cos(az));
}

// The bow's shape along the crossing: zero at both ends, one in the middle, and zero outside the
// authored span so a comet that outlives its crossing flies straight on rather than bending back.
float bowShape(float t) {
    if (t <= 0.0f || t >= 1.0f) {
        return 0.0f;
    }
    return std::sin(glm::pi<float>() * t);
}

} // namespace

// ---- enum names --------------------------------------------------------------------------------

const char* atmosphereKindName(AtmosphereKind k) {
    switch (k) {
    case AtmosphereKind::Comet: return "comet";
    case AtmosphereKind::Aurora: return "aurora";
    case AtmosphereKind::Vortex: return "vortex";
    }
    return "comet";
}
std::optional<AtmosphereKind> atmosphereKindFromName(std::string_view name) {
    if (name == "comet") { return AtmosphereKind::Comet; }
    if (name == "aurora") { return AtmosphereKind::Aurora; }
    if (name == "vortex") { return AtmosphereKind::Vortex; }
    return std::nullopt;
}

const char* skyAnchorName(SkyAnchor a) {
    switch (a) {
    case SkyAnchor::World: return "world";
    case SkyAnchor::Camera: return "camera";
    }
    return "world";
}
std::optional<SkyAnchor> skyAnchorFromName(std::string_view name) {
    if (name == "world") { return SkyAnchor::World; }
    if (name == "camera") { return SkyAnchor::Camera; }
    return std::nullopt;
}

const char* groundGlowName(GroundGlow g) {
    switch (g) {
    case GroundGlow::Off: return "off";
    case GroundGlow::Subtle: return "subtle";
    case GroundGlow::Strong: return "strong";
    }
    return "off";
}
std::optional<GroundGlow> groundGlowFromName(std::string_view name) {
    if (name == "off") { return GroundGlow::Off; }
    if (name == "subtle") { return GroundGlow::Subtle; }
    if (name == "strong") { return GroundGlow::Strong; }
    return std::nullopt;
}

// The multiplier each word means. Subtle is deliberately well under half of Strong: §6 warns that
// the effect must not flatten the scene, and the failure mode of a "subtle" setting that is really
// two thirds is that nobody ever uses Off.
float groundGlowScale(GroundGlow g) {
    switch (g) {
    case GroundGlow::Off: return 0.0f;
    case GroundGlow::Subtle: return 0.35f;
    case GroundGlow::Strong: return 1.0f;
    }
    return 0.0f;
}

// ---- validation --------------------------------------------------------------------------------

Result<void> SkyRainbow::validate() const {
    if (!finite(speed) || !finite(scale) || !finite(hueOffset)) {
        return fail("the rainbow controls are not finite");
    }
    if (saturation < 0.0f || saturation > 1.0f) { return fail("rainbow saturation is 0..1"); }
    if (brightness < 0.0f || !finite(brightness)) { return fail("rainbow brightness must not be negative"); }
    return {};
}

Result<void> GroundIllumination::validate() const {
    if (!finite(intensity) || intensity < 0.0f) { return fail("ground illumination intensity must not be negative"); }
    if (!(radius > 0.0f) || !finite(radius)) { return fail("ground illumination radius must be positive"); }
    if (!(falloff > 0.0f) || !finite(falloff)) { return fail("ground illumination falloff must be positive"); }
    if (!finite(color.x) || !finite(color.y) || !finite(color.z)) {
        return fail("the ground illumination colour is not finite");
    }
    return {};
}

Result<void> CometPath::validate() const {
    if (!(distance > 0.0f) || !finite(distance)) { return fail("comet distance must be positive"); }
    if (!(travelSeconds > 0.0)) { return fail("comet travel duration must be positive"); }
    if (!(speedScale > 0.0f) || !finite(speedScale)) { return fail("comet speed scale must be positive"); }
    // Below -0.5 the reparameterisation stops being monotone and the comet would fly backwards
    // through the second half of its own crossing.
    if (!(acceleration > -0.5f) || !finite(acceleration)) {
        return fail("comet acceleration must be greater than -0.5 (below that the path reverses)");
    }
    for (const float v : {startAzimuth, startElevation, endAzimuth, endElevation, curvature, arcLift}) {
        if (!finite(v)) { return fail("a comet path angle or offset is not finite"); }
    }
    if (startAzimuth == endAzimuth && startElevation == endElevation) {
        return fail("a comet's start and end directions are the same; it would not move");
    }
    return {};
}

Result<void> CometAppearance::validate() const {
    for (const float v : {coreIntensity, haloIntensity, tailIntensity}) {
        if (!finite(v) || v < 0.0f) { return fail("a comet radiance must be finite and not negative"); }
    }
    if (!(headSize > 0.0f) || !finite(headSize)) { return fail("comet head size must be positive"); }
    if (!(haloSize > 0.0f) || !finite(haloSize)) { return fail("comet halo size must be positive"); }
    if (!(tailLength > 0.0f) || !finite(tailLength)) { return fail("comet tail length must be positive"); }
    if (!(tailWidth > 0.0f) || !finite(tailWidth)) { return fail("comet tail width must be positive"); }
    if (!(tailFalloff > 0.0f) || !finite(tailFalloff)) { return fail("comet tail falloff must be positive"); }
    if (wispAmount < 0.0f || !finite(wispAmount)) { return fail("comet wisp amount must not be negative"); }
    if (wispScale < 0.0f || !finite(wispScale)) { return fail("comet wisp scale must not be negative"); }
    if (!finite(flowSpeed)) { return fail("comet flow speed is not finite"); }
    return {};
}

Result<void> Comet::validate() const {
    if (auto ok = path.validate(); !ok) { return ok; }
    if (auto ok = appearance.validate(); !ok) { return ok; }
    if (auto ok = sparkle.validate(); !ok) { return ok; }
    if (auto ok = rainbow.validate(); !ok) { return ok; }
    return {};
}

Result<void> AuroraShape::validate() const {
    if (curtainCount < 1.0f || curtainCount > 5.0f) {
        return fail("an aurora has 1..5 curtain layers; each one is a ray solve per sky pixel");
    }
    if (!(radius > 0.0f) || !finite(radius)) { return fail("aurora radius must be positive"); }
    if (layerSpacing < 0.0f || !finite(layerSpacing)) { return fail("aurora layer spacing must not be negative"); }
    if (!finite(baseHeight)) { return fail("aurora base height is not finite"); }
    if (!(curtainHeight > 0.0f) || !finite(curtainHeight)) { return fail("aurora curtain height must be positive"); }
    if (waveAmplitude < 0.0f || !finite(waveAmplitude)) { return fail("aurora wave amplitude must not be negative"); }
    if (waveScale < 0.0f || !finite(waveScale)) { return fail("aurora wave scale must not be negative"); }
    if (turbulence < 0.0f || !finite(turbulence)) { return fail("aurora turbulence must not be negative"); }
    if (complexity < 0.0f || !finite(complexity)) { return fail("aurora complexity must not be negative"); }
    for (const float v : {flowSpeed, driftSpeed, verticalSpeed}) {
        if (!finite(v)) { return fail("an aurora motion speed is not finite"); }
    }
    return {};
}

Result<void> AuroraAppearance::validate() const {
    for (const float v : {intensity, emission, opacity, edgeBrightness, filaments, sparkle, horizonGlow}) {
        if (!finite(v) || v < 0.0f) { return fail("an aurora appearance weight must be finite and not negative"); }
    }
    return {};
}

Result<void> AuroraAudio::validate() const {
    for (const float v : {bass, lowMid, mid, high, beat, sensitivity, spectrumShape}) {
        if (!finite(v) || v < 0.0f) { return fail("an aurora audio response must be finite and not negative"); }
    }
    return {};
}

Result<void> Aurora::validate() const {
    if (auto ok = shape.validate(); !ok) { return ok; }
    if (auto ok = appearance.validate(); !ok) { return ok; }
    if (auto ok = audio.validate(); !ok) { return ok; }
    if (auto ok = rainbow.validate(); !ok) { return ok; }
    return {};
}

// ADR-387. The gate is `radius`: every shader function returns before doing any work at zero, so a
// zero radius must stay legal (it is the default, and it is what every scene but one has).
Result<void> Vortex::validate() const {
    if (!finite(center.x) || !finite(center.y) || !finite(center.z)) {
        return fail("the vortex centre is not finite");
    }
    for (const float f : {radius, thickness, swirl, rotationSpeed, density, innerVoid, contrast,
                          turbulence, turbulenceScale, breathAmount, breathSpeed, emission,
                          filaments, spill, scattering, cometResponse, cometReach, funnelDepth,
                          throat, throatDensity, smokeWarp, smokeBillow, detail}) {
        if (!finite(f)) { return fail("a vortex control is not finite"); }
    }
    if (radius < 0.0f) { return fail("the vortex radius may not be negative (0 is off)"); }
    if (thickness < 0.0f) { return fail("the vortex thickness may not be negative"); }
    if (funnelDepth < 0.0f) { return fail("the vortex funnel depth may not be negative"); }
    if (innerVoid < 0.0f || innerVoid > 1.0f) { return fail("the vortex inner void is 0..1"); }
    if (throat < 0.0f || throat > 1.0f) { return fail("the vortex throat is 0..1 of the mouth"); }
    return {};
}

Result<void> AtmosphericEffect::validate() const {
    if (name.empty()) { return fail("an atmospheric effect needs a name"); }
    if (name.find('/') != std::string::npos) {
        // The name is half of a parameter path (`atmos/<name>/intensity`); a slash in it would
        // silently invent a group nobody can address.
        return fail("atmospheric effect '{}': a name may not contain '/'", name);
    }
    // Both payloads are validated whatever the kind. An effect keeps the settings of the kind it is
    // not currently using -- that is why they are two members rather than a variant -- and a file
    // that round-trips a broken one and only complains after somebody switches kind is worse than
    // one that complains now.
    if (auto ok = comet.validate(); !ok) { return fail("atmospheric effect '{}': {}", name, ok.error().message); }
    if (auto ok = aurora.validate(); !ok) { return fail("atmospheric effect '{}': {}", name, ok.error().message); }
    if (auto ok = vortex.validate(); !ok) { return fail("atmospheric effect '{}': {}", name, ok.error().message); }
    if (auto ok = ground.validate(); !ok) { return fail("atmospheric effect '{}': {}", name, ok.error().message); }
    if (auto ok = timing.validate(); !ok) { return fail("atmospheric effect '{}': {}", name, ok.error().message); }
    return {};
}

Result<void> validateAtmosphericEffects(std::span<const AtmosphericEffect> effects) {
    std::unordered_set<std::string> seen;
    for (const AtmosphericEffect& e : effects) {
        if (auto ok = e.validate(); !ok) {
            return ok;
        }
        if (!seen.insert(e.name).second) {
            return fail("two atmospheric effects are named '{}'; a name is half of a parameter path", e.name);
        }
    }
    return {};
}

// ---- JSON --------------------------------------------------------------------------------------

namespace {

json rainbowToJson(const SkyRainbow& r) {
    return json{{"enabled", r.enabled}, {"speed", r.speed},           {"scale", r.scale},
                {"hueOffset", r.hueOffset}, {"saturation", r.saturation}, {"brightness", r.brightness}};
}
SkyRainbow rainbowFromJson(const json& j) {
    SkyRainbow r;
    if (!j.is_object()) { return r; }
    r.enabled = readBool(j, "enabled", r.enabled);
    r.speed = readFloat(j, "speed", r.speed);
    r.scale = readFloat(j, "scale", r.scale);
    r.hueOffset = readFloat(j, "hueOffset", r.hueOffset);
    r.saturation = readFloat(j, "saturation", r.saturation);
    r.brightness = readFloat(j, "brightness", r.brightness);
    return r;
}

json sparkleToJson(const Sparkle& s) {
    return json{{"enabled", s.enabled},   {"density", s.density}, {"size", s.size},
                {"intensity", s.intensity}, {"speed", s.speed},   {"fadeDistance", s.fadeDistance},
                {"seed", s.seed}};
}
Sparkle sparkleFromJson(const json& j) {
    Sparkle s;
    if (!j.is_object()) { return s; }
    s.enabled = readBool(j, "enabled", s.enabled);
    s.density = readFloat(j, "density", s.density);
    s.size = readFloat(j, "size", s.size);
    s.intensity = readFloat(j, "intensity", s.intensity);
    s.speed = readFloat(j, "speed", s.speed);
    s.fadeDistance = readFloat(j, "fadeDistance", s.fadeDistance);
    if (j.contains("seed") && j.at("seed").is_number_unsigned()) {
        s.seed = j.at("seed").get<std::uint32_t>();
    }
    return s;
}

json timingToJson(const Timing& t) {
    return json{{"delay", t.delay},           {"lifetime", t.lifetime},
                {"fadeIn", t.fadeIn},         {"fadeOut", t.fadeOut},
                {"windowStart", t.windowStart}, {"windowSeconds", t.windowSeconds},
                {"repeatSeconds", t.repeatSeconds}};
}
Timing timingFromJson(const json& j) {
    Timing t;
    if (!j.is_object()) { return t; }
    t.delay = readDouble(j, "delay", t.delay);
    t.lifetime = readDouble(j, "lifetime", t.lifetime);
    t.fadeIn = readDouble(j, "fadeIn", t.fadeIn);
    t.fadeOut = readDouble(j, "fadeOut", t.fadeOut);
    t.windowStart = readDouble(j, "windowStart", t.windowStart);
    t.windowSeconds = readDouble(j, "windowSeconds", t.windowSeconds);
    t.repeatSeconds = readDouble(j, "repeatSeconds", t.repeatSeconds);
    return t;
}

} // namespace

json AtmosphericEffect::toJson() const {
    const Comet& c = comet;
    const Aurora& a = aurora;
    json j;
    j["name"] = name;
    j["enabled"] = enabled;
    if (!style.empty()) { j["style"] = style; }
    j["kind"] = atmosphereKindName(kind);
    j["activation"] = activationName(activation);
    j["timing"] = timingToJson(timing);

    j["ground"] = json{{"mode", groundGlowName(ground.mode)},
                       {"color", vec3ToJson(ground.color)},
                       {"intensity", ground.intensity},
                       {"radius", ground.radius},
                       {"falloff", ground.falloff}};

    j["comet"] = json{
        {"path", json{{"anchor", skyAnchorName(c.path.anchor)},
                      {"anchorPosition", vec3ToJson(c.path.anchorPosition)},
                      {"startAzimuth", c.path.startAzimuth},
                      {"startElevation", c.path.startElevation},
                      {"endAzimuth", c.path.endAzimuth},
                      {"endElevation", c.path.endElevation},
                      {"distance", c.path.distance},
                      {"travelSeconds", c.path.travelSeconds},
                      {"speedScale", c.path.speedScale},
                      {"acceleration", c.path.acceleration},
                      {"curvature", c.path.curvature},
                      {"arcLift", c.path.arcLift}}},
        {"appearance", json{{"coreColor", vec3ToJson(c.appearance.coreColor)},
                            {"coreIntensity", c.appearance.coreIntensity},
                            {"headSize", c.appearance.headSize},
                            {"haloColor", vec3ToJson(c.appearance.haloColor)},
                            {"haloIntensity", c.appearance.haloIntensity},
                            {"haloSize", c.appearance.haloSize},
                            {"tailColor", vec3ToJson(c.appearance.tailColor)},
                            {"tailIntensity", c.appearance.tailIntensity},
                            {"tailLength", c.appearance.tailLength},
                            {"tailWidth", c.appearance.tailWidth},
                            {"tailFalloff", c.appearance.tailFalloff},
                            {"wispAmount", c.appearance.wispAmount},
                            {"wispScale", c.appearance.wispScale},
                            {"flowSpeed", c.appearance.flowSpeed}}},
        {"sparkle", sparkleToJson(c.sparkle)},
        {"rainbow", rainbowToJson(c.rainbow)}};

    j["aurora"] = json{
        {"shape", json{{"anchor", skyAnchorName(a.shape.anchor)},
                       {"anchorPosition", vec3ToJson(a.shape.anchorPosition)},
                       {"curtainCount", a.shape.curtainCount},
                       {"radius", a.shape.radius},
                       {"layerSpacing", a.shape.layerSpacing},
                       {"baseHeight", a.shape.baseHeight},
                       {"curtainHeight", a.shape.curtainHeight},
                       {"waveAmplitude", a.shape.waveAmplitude},
                       {"waveScale", a.shape.waveScale},
                       {"turbulence", a.shape.turbulence},
                       {"complexity", a.shape.complexity},
                       {"flowSpeed", a.shape.flowSpeed},
                       {"driftSpeed", a.shape.driftSpeed},
                       {"verticalSpeed", a.shape.verticalSpeed}}},
        {"appearance", json{{"lowColor", vec3ToJson(a.appearance.lowColor)},
                            {"midColor", vec3ToJson(a.appearance.midColor)},
                            {"topColor", vec3ToJson(a.appearance.topColor)},
                            {"intensity", a.appearance.intensity},
                            {"emission", a.appearance.emission},
                            {"opacity", a.appearance.opacity},
                            {"edgeBrightness", a.appearance.edgeBrightness},
                            {"filaments", a.appearance.filaments},
                            {"sparkle", a.appearance.sparkle},
                            {"horizonGlow", a.appearance.horizonGlow}}},
        {"audio", json{{"bass", a.audio.bass},
                       {"lowMid", a.audio.lowMid},
                       {"mid", a.audio.mid},
                       {"high", a.audio.high},
                       {"beat", a.audio.beat},
                       {"sensitivity", a.audio.sensitivity},
                       {"spectrumShape", a.audio.spectrumShape}}},
        {"rainbow", rainbowToJson(a.rainbow)}};

    // ADR-387: flat, because every one of these is a single authored number with no sub-structure
    // and the names are the ones the legacy `environment.vortex` block used -- a scene migrated
    // from that form reads back identically field for field.
    const Vortex& vx = vortex;
    j["vortex"] = json{{"center", vec3ToJson(vx.center)},
                       {"radius", vx.radius},
                       {"thickness", vx.thickness},
                       {"swirl", vx.swirl},
                       {"rotationSpeed", vx.rotationSpeed},
                       {"density", vx.density},
                       {"innerVoid", vx.innerVoid},
                       {"contrast", vx.contrast},
                       {"turbulence", vx.turbulence},
                       {"turbulenceScale", vx.turbulenceScale},
                       {"breathAmount", vx.breathAmount},
                       {"breathSpeed", vx.breathSpeed},
                       {"smokeWarp", vx.smokeWarp},
                       {"smokeBillow", vx.smokeBillow},
                       {"detail", vx.detail},
                       {"emission", vx.emission},
                       {"filaments", vx.filaments},
                       {"spill", vx.spill},
                       {"scattering", vx.scattering},
                       {"cometResponse", vx.cometResponse},
                       {"cometReach", vx.cometReach},
                       {"funnelDepth", vx.funnelDepth},
                       {"throat", vx.throat},
                       {"throatDensity", vx.throatDensity},
                       {"colorDeep", vec3ToJson(vx.colorDeep)},
                       {"colorMid", vec3ToJson(vx.colorMid)},
                       {"colorAccent", vec3ToJson(vx.colorAccent)}};
    return j;
}

Result<AtmosphericEffect> AtmosphericEffect::fromJson(const json& j) {
    if (!j.is_object()) {
        return fail("an atmospheric effect must be an object");
    }
    AtmosphericEffect e;
    e.name = readString(j, "name");
    e.enabled = readBool(j, "enabled", true);
    e.style = readString(j, "style");

    if (j.contains("kind")) {
        const auto kind = atmosphereKindFromName(readString(j, "kind"));
        if (!kind) { return fail("atmospheric effect '{}': unknown kind '{}'", e.name, readString(j, "kind")); }
        e.kind = *kind;
    }
    if (j.contains("activation")) {
        const auto act = activationFromName(readString(j, "activation"));
        if (!act) { return fail("atmospheric effect '{}': unknown activation '{}'", e.name, readString(j, "activation")); }
        e.activation = *act;
    }
    if (j.contains("timing")) { e.timing = timingFromJson(j.at("timing")); }

    if (j.contains("ground") && j.at("ground").is_object()) {
        const json& g = j.at("ground");
        if (g.contains("mode")) {
            const auto mode = groundGlowFromName(readString(g, "mode"));
            if (!mode) { return fail("atmospheric effect '{}': unknown ground glow '{}'", e.name, readString(g, "mode")); }
            e.ground.mode = *mode;
        }
        e.ground.color = readVec3(g, "color", e.ground.color);
        e.ground.intensity = readFloat(g, "intensity", e.ground.intensity);
        e.ground.radius = readFloat(g, "radius", e.ground.radius);
        e.ground.falloff = readFloat(g, "falloff", e.ground.falloff);
    }

    if (j.contains("comet") && j.at("comet").is_object()) {
        const json& c = j.at("comet");
        if (c.contains("path") && c.at("path").is_object()) {
            const json& p = c.at("path");
            if (p.contains("anchor")) {
                const auto anchor = skyAnchorFromName(readString(p, "anchor"));
                if (!anchor) { return fail("atmospheric effect '{}': unknown anchor '{}'", e.name, readString(p, "anchor")); }
                e.comet.path.anchor = *anchor;
            }
            CometPath& path = e.comet.path;
            path.anchorPosition = readVec3(p, "anchorPosition", path.anchorPosition);
            path.startAzimuth = readFloat(p, "startAzimuth", path.startAzimuth);
            path.startElevation = readFloat(p, "startElevation", path.startElevation);
            path.endAzimuth = readFloat(p, "endAzimuth", path.endAzimuth);
            path.endElevation = readFloat(p, "endElevation", path.endElevation);
            path.distance = readFloat(p, "distance", path.distance);
            path.travelSeconds = readFloat(p, "travelSeconds", path.travelSeconds);
            path.speedScale = readFloat(p, "speedScale", path.speedScale);
            path.acceleration = readFloat(p, "acceleration", path.acceleration);
            path.curvature = readFloat(p, "curvature", path.curvature);
            path.arcLift = readFloat(p, "arcLift", path.arcLift);
        }
        if (c.contains("appearance") && c.at("appearance").is_object()) {
            const json& a = c.at("appearance");
            CometAppearance& ap = e.comet.appearance;
            ap.coreColor = readVec3(a, "coreColor", ap.coreColor);
            ap.coreIntensity = readFloat(a, "coreIntensity", ap.coreIntensity);
            ap.headSize = readFloat(a, "headSize", ap.headSize);
            ap.haloColor = readVec3(a, "haloColor", ap.haloColor);
            ap.haloIntensity = readFloat(a, "haloIntensity", ap.haloIntensity);
            ap.haloSize = readFloat(a, "haloSize", ap.haloSize);
            ap.tailColor = readVec3(a, "tailColor", ap.tailColor);
            ap.tailIntensity = readFloat(a, "tailIntensity", ap.tailIntensity);
            ap.tailLength = readFloat(a, "tailLength", ap.tailLength);
            ap.tailWidth = readFloat(a, "tailWidth", ap.tailWidth);
            ap.tailFalloff = readFloat(a, "tailFalloff", ap.tailFalloff);
            ap.wispAmount = readFloat(a, "wispAmount", ap.wispAmount);
            ap.wispScale = readFloat(a, "wispScale", ap.wispScale);
            ap.flowSpeed = readFloat(a, "flowSpeed", ap.flowSpeed);
        }
        if (c.contains("sparkle")) { e.comet.sparkle = sparkleFromJson(c.at("sparkle")); }
        if (c.contains("rainbow")) { e.comet.rainbow = rainbowFromJson(c.at("rainbow")); }
    }

    if (j.contains("aurora") && j.at("aurora").is_object()) {
        const json& au = j.at("aurora");
        if (au.contains("shape") && au.at("shape").is_object()) {
            const json& s = au.at("shape");
            if (s.contains("anchor")) {
                const auto anchor = skyAnchorFromName(readString(s, "anchor"));
                if (!anchor) { return fail("atmospheric effect '{}': unknown anchor '{}'", e.name, readString(s, "anchor")); }
                e.aurora.shape.anchor = *anchor;
            }
            AuroraShape& sh = e.aurora.shape;
            sh.anchorPosition = readVec3(s, "anchorPosition", sh.anchorPosition);
            sh.curtainCount = readFloat(s, "curtainCount", sh.curtainCount);
            sh.radius = readFloat(s, "radius", sh.radius);
            sh.layerSpacing = readFloat(s, "layerSpacing", sh.layerSpacing);
            sh.baseHeight = readFloat(s, "baseHeight", sh.baseHeight);
            sh.curtainHeight = readFloat(s, "curtainHeight", sh.curtainHeight);
            sh.waveAmplitude = readFloat(s, "waveAmplitude", sh.waveAmplitude);
            sh.waveScale = readFloat(s, "waveScale", sh.waveScale);
            sh.turbulence = readFloat(s, "turbulence", sh.turbulence);
            sh.complexity = readFloat(s, "complexity", sh.complexity);
            sh.flowSpeed = readFloat(s, "flowSpeed", sh.flowSpeed);
            sh.driftSpeed = readFloat(s, "driftSpeed", sh.driftSpeed);
            sh.verticalSpeed = readFloat(s, "verticalSpeed", sh.verticalSpeed);
        }
        if (au.contains("appearance") && au.at("appearance").is_object()) {
            const json& a = au.at("appearance");
            AuroraAppearance& ap = e.aurora.appearance;
            ap.lowColor = readVec3(a, "lowColor", ap.lowColor);
            ap.midColor = readVec3(a, "midColor", ap.midColor);
            ap.topColor = readVec3(a, "topColor", ap.topColor);
            ap.intensity = readFloat(a, "intensity", ap.intensity);
            ap.emission = readFloat(a, "emission", ap.emission);
            ap.opacity = readFloat(a, "opacity", ap.opacity);
            ap.edgeBrightness = readFloat(a, "edgeBrightness", ap.edgeBrightness);
            ap.filaments = readFloat(a, "filaments", ap.filaments);
            ap.sparkle = readFloat(a, "sparkle", ap.sparkle);
            ap.horizonGlow = readFloat(a, "horizonGlow", ap.horizonGlow);
        }
        if (au.contains("audio") && au.at("audio").is_object()) {
            const json& a = au.at("audio");
            AuroraAudio& ad = e.aurora.audio;
            ad.bass = readFloat(a, "bass", ad.bass);
            ad.lowMid = readFloat(a, "lowMid", ad.lowMid);
            ad.mid = readFloat(a, "mid", ad.mid);
            ad.high = readFloat(a, "high", ad.high);
            ad.beat = readFloat(a, "beat", ad.beat);
            ad.sensitivity = readFloat(a, "sensitivity", ad.sensitivity);
            ad.spectrumShape = readFloat(a, "spectrumShape", ad.spectrumShape);
        }
        if (au.contains("rainbow")) { e.aurora.rainbow = rainbowFromJson(au.at("rainbow")); }
    }

    if (j.contains("vortex") && j.at("vortex").is_object()) {
        const json& vj = j.at("vortex");
        Vortex& vx = e.vortex;
        vx.center = readVec3(vj, "center", vx.center);
        vx.radius = readFloat(vj, "radius", vx.radius);
        vx.thickness = readFloat(vj, "thickness", vx.thickness);
        vx.swirl = readFloat(vj, "swirl", vx.swirl);
        vx.rotationSpeed = readFloat(vj, "rotationSpeed", vx.rotationSpeed);
        vx.density = readFloat(vj, "density", vx.density);
        vx.innerVoid = readFloat(vj, "innerVoid", vx.innerVoid);
        vx.contrast = readFloat(vj, "contrast", vx.contrast);
        vx.turbulence = readFloat(vj, "turbulence", vx.turbulence);
        vx.turbulenceScale = readFloat(vj, "turbulenceScale", vx.turbulenceScale);
        vx.breathAmount = readFloat(vj, "breathAmount", vx.breathAmount);
        vx.breathSpeed = readFloat(vj, "breathSpeed", vx.breathSpeed);
        vx.smokeWarp = readFloat(vj, "smokeWarp", vx.smokeWarp);
        vx.smokeBillow = readFloat(vj, "smokeBillow", vx.smokeBillow);
        vx.detail = readFloat(vj, "detail", vx.detail);
        vx.emission = readFloat(vj, "emission", vx.emission);
        vx.filaments = readFloat(vj, "filaments", vx.filaments);
        vx.spill = readFloat(vj, "spill", vx.spill);
        vx.scattering = readFloat(vj, "scattering", vx.scattering);
        vx.cometResponse = readFloat(vj, "cometResponse", vx.cometResponse);
        vx.cometReach = readFloat(vj, "cometReach", vx.cometReach);
        vx.funnelDepth = readFloat(vj, "funnelDepth", vx.funnelDepth);
        vx.throat = readFloat(vj, "throat", vx.throat);
        vx.throatDensity = readFloat(vj, "throatDensity", vx.throatDensity);
        vx.colorDeep = readVec3(vj, "colorDeep", vx.colorDeep);
        vx.colorMid = readVec3(vj, "colorMid", vx.colorMid);
        vx.colorAccent = readVec3(vj, "colorAccent", vx.colorAccent);
    }

    if (auto ok = e.validate(); !ok) {
        return std::unexpected(ok.error());
    }
    return e;
}

// ---- presets -----------------------------------------------------------------------------------

namespace {

constexpr std::array<std::string_view, 5> kCometStyles{"Bioluminescent Cyan", "Rainbow Cosmic",
                                                       "Emerald Teal", "Magenta Blue",
                                                       "Subtle Shooting Star"};
// ADR-387. "Cosmic Funnel" is the Tree of Life's shipped funnel, value for value, for the reason
// the factory comment above gives: a preset that only approximates the scene it was taken from is a
// preset that drifts from it. The other two are the two directions that turned out to read -- a
// shallow disc seen from above, and a deeper, denser throat.
constexpr std::array<std::string_view, 3> kVortexStyles{"Cosmic Funnel", "Shallow Disc",
                                                        "Deep Maelstrom"};
constexpr std::array<std::string_view, 5> kAuroraStyles{"Glowmere Bioluminescence", "Vibrant Emerald",
                                                        "Violet Cosmic", "Rainbow Aurora",
                                                        "Subtle Night"};

} // namespace

std::span<const std::string_view> cometStyleNames() { return kCometStyles; }
std::span<const std::string_view> auroraStyleNames() { return kAuroraStyles; }
std::span<const std::string_view> vortexStyleNames() { return kVortexStyles; }

bool applyCometStyle(AtmosphericEffect& e, std::string_view style) {
    CometAppearance& a = e.comet.appearance;
    Sparkle& s = e.comet.sparkle;
    SkyRainbow& r = e.comet.rainbow;
    // Every style writes every field it cares about, including turning things off. A style that
    // only sets what it wants leaves the previous style's rainbow on, and the user reads that as
    // the preset being broken rather than as two presets overlapping.
    r = SkyRainbow{};
    s.enabled = true;
    s.seed = 1;
    if (style == kCometStyles[0]) { // Bioluminescent Cyan
        a.coreColor = {0.55f, 1.0f, 1.0f};
        a.coreIntensity = 20.0f;
        a.headSize = 19.0f;
        a.haloColor = {0.16f, 0.78f, 1.0f};
        a.haloIntensity = 2.1f;
        a.haloSize = 95.0f;
        a.tailColor = {0.42f, 0.34f, 1.0f};
        a.tailIntensity = 5.0f;
        a.tailLength = 2000.0f;
        a.tailWidth = 38.0f;
        a.tailFalloff = 1.85f;
        a.wispAmount = 62.0f;
        a.wispScale = 0.0022f;
        a.flowSpeed = 0.35f;
        s.density = 0.011f;
        s.size = 0.13f;
        s.intensity = 7.0f;
        s.speed = 1.3f;
    } else if (style == kCometStyles[1]) { // Rainbow Cosmic
        // The colours are left near-white on purpose: rainbow replaces the *hue* and keeps the
        // magnitude, exactly as ADR-207's Rainbow style does, so a tinted base would fight it.
        a.coreColor = {1.0f, 1.0f, 1.0f};
        // Lower than the explicit-colour presets on purpose. Rainbow multiplies a hue into a white
        // base, so at 22 the core clipped to white and the one part of the comet an eye goes
        // straight to was the one part with no colour left in it.
        a.coreIntensity = 13.0f;
        a.headSize = 21.0f;
        a.haloColor = {0.9f, 0.9f, 1.0f};
        a.haloIntensity = 2.2f;
        a.haloSize = 105.0f;
        a.tailColor = {1.0f, 1.0f, 1.0f};
        a.tailIntensity = 5.5f;
        a.tailLength = 2400.0f;
        a.tailWidth = 44.0f;
        a.tailFalloff = 1.65f;
        a.wispAmount = 78.0f;
        a.wispScale = 0.0019f;
        a.flowSpeed = 0.42f;
        s.density = 0.013f;
        s.size = 0.15f;
        s.intensity = 8.5f;
        s.speed = 1.6f;
        r.enabled = true;
        // Just over one turn across the tail: less and it is a single hue, much more and it stripes
        // into the "broken RGB shader" §3.3 warns about.
        r.scale = 1.25f;
        r.speed = 0.28f;
        r.saturation = 0.92f;
        r.brightness = 1.05f;
    } else if (style == kCometStyles[2]) { // Emerald Teal
        a.coreColor = {0.55f, 1.0f, 0.78f};
        a.coreIntensity = 17.0f;
        a.headSize = 17.0f;
        a.haloColor = {0.10f, 0.85f, 0.62f};
        a.haloIntensity = 1.9f;
        a.haloSize = 88.0f;
        a.tailColor = {0.13f, 0.92f, 0.72f};
        a.tailIntensity = 4.6f;
        a.tailLength = 1750.0f;
        a.tailWidth = 34.0f;
        a.tailFalloff = 2.0f;
        a.wispAmount = 54.0f;
        a.wispScale = 0.0024f;
        a.flowSpeed = 0.31f;
        s.density = 0.010f;
        s.size = 0.12f;
        s.intensity = 6.5f;
        s.speed = 1.2f;
    } else if (style == kCometStyles[3]) { // Magenta Blue
        a.coreColor = {1.0f, 0.68f, 1.0f};
        a.coreIntensity = 19.0f;
        a.headSize = 19.0f;
        a.haloColor = {0.85f, 0.22f, 0.95f};
        a.haloIntensity = 2.0f;
        a.haloSize = 98.0f;
        a.tailColor = {0.30f, 0.32f, 1.0f};
        a.tailIntensity = 5.2f;
        a.tailLength = 1900.0f;
        a.tailWidth = 40.0f;
        a.tailFalloff = 1.75f;
        a.wispAmount = 66.0f;
        a.wispScale = 0.0021f;
        a.flowSpeed = 0.38f;
        s.density = 0.012f;
        s.size = 0.14f;
        s.intensity = 7.5f;
        s.speed = 1.4f;
    } else if (style == kCometStyles[4]) { // Subtle Shooting Star
        a.coreColor = {0.92f, 0.97f, 1.0f};
        a.coreIntensity = 7.0f;
        a.headSize = 6.0f;
        a.haloColor = {0.55f, 0.72f, 1.0f};
        a.haloIntensity = 0.7f;
        a.haloSize = 34.0f;
        a.tailColor = {0.60f, 0.75f, 1.0f};
        a.tailIntensity = 1.5f;
        a.tailLength = 700.0f;
        a.tailWidth = 11.0f;
        a.tailFalloff = 2.6f;
        a.wispAmount = 6.0f;
        a.wispScale = 0.0040f;
        a.flowSpeed = 0.20f;
        s.enabled = false;
        s.density = 0.010f;
        s.size = 0.18f;
        s.intensity = 3.0f;
        s.speed = 1.0f;
    } else {
        return false;
    }
    e.kind = AtmosphereKind::Comet;
    e.style = std::string(style);
    return true;
}

bool applyAuroraStyle(AtmosphericEffect& e, std::string_view style) {
    AuroraAppearance& a = e.aurora.appearance;
    AuroraShape& s = e.aurora.shape;
    SkyRainbow& r = e.aurora.rainbow;
    r = SkyRainbow{};
    if (style == kAuroraStyles[0]) { // Glowmere Bioluminescence
        a.lowColor = {0.14f, 1.0f, 0.62f};
        a.midColor = {0.16f, 0.82f, 1.0f};
        a.topColor = {0.58f, 0.32f, 1.0f};
        a.intensity = 2.8f;
        a.emission = 1.0f;
        a.opacity = 0.85f;
        a.edgeBrightness = 2.6f;
        a.filaments = 0.95f;
        a.horizonGlow = 0.60f;
        s.curtainCount = 3.0f;
        s.waveAmplitude = 0.30f;
        s.waveScale = 2.4f;
        s.turbulence = 0.45f;
        s.complexity = 26.0f;
    } else if (style == kAuroraStyles[1]) { // Vibrant Emerald
        a.lowColor = {0.10f, 1.0f, 0.38f};
        a.midColor = {0.22f, 1.0f, 0.72f};
        a.topColor = {0.20f, 0.90f, 1.0f};
        a.intensity = 3.4f;
        a.emission = 1.0f;
        a.opacity = 0.92f;
        a.edgeBrightness = 3.2f;
        a.filaments = 1.10f;
        a.horizonGlow = 0.72f;
        s.curtainCount = 4.0f;
        s.waveAmplitude = 0.36f;
        s.waveScale = 2.0f;
        s.turbulence = 0.40f;
        s.complexity = 30.0f;
    } else if (style == kAuroraStyles[2]) { // Violet Cosmic
        a.lowColor = {0.32f, 0.28f, 1.0f};
        a.midColor = {0.62f, 0.26f, 1.0f};
        a.topColor = {1.0f, 0.34f, 0.86f};
        a.intensity = 2.9f;
        a.emission = 1.0f;
        a.opacity = 0.80f;
        a.edgeBrightness = 2.8f;
        a.filaments = 0.85f;
        a.horizonGlow = 0.50f;
        s.curtainCount = 3.0f;
        s.waveAmplitude = 0.42f;
        s.waveScale = 1.7f;
        s.turbulence = 0.55f;
        s.complexity = 22.0f;
    } else if (style == kAuroraStyles[3]) { // Rainbow Aurora
        a.lowColor = {1.0f, 1.0f, 1.0f};
        a.midColor = {1.0f, 1.0f, 1.0f};
        a.topColor = {1.0f, 1.0f, 1.0f};
        a.intensity = 2.6f;
        a.emission = 1.0f;
        a.opacity = 0.84f;
        a.edgeBrightness = 2.4f;
        a.filaments = 1.0f;
        a.horizonGlow = 0.55f;
        s.curtainCount = 4.0f;
        s.waveAmplitude = 0.34f;
        s.waveScale = 2.2f;
        s.turbulence = 0.48f;
        s.complexity = 28.0f;
        r.enabled = true;
        // Under one turn of azimuth, so the sky reads as a slow sweep through the spectrum rather
        // than as bands: §4.4's rainbow mode is a colour *cycle*, not a striping.
        r.scale = 0.75f;
        r.speed = 0.06f;
        r.saturation = 0.88f;
        r.brightness = 1.0f;
    } else if (style == kAuroraStyles[4]) { // Subtle Night
        a.lowColor = {0.18f, 0.72f, 0.52f};
        a.midColor = {0.18f, 0.55f, 0.72f};
        a.topColor = {0.28f, 0.30f, 0.62f};
        a.intensity = 0.85f;
        a.emission = 0.5f;
        a.opacity = 0.55f;
        a.edgeBrightness = 0.9f;
        a.filaments = 0.35f;
        a.horizonGlow = 0.30f;
        s.curtainCount = 2.0f;
        s.waveAmplitude = 0.22f;
        s.waveScale = 1.9f;
        s.turbulence = 0.32f;
        s.complexity = 18.0f;
    } else {
        return false;
    }
    e.kind = AtmosphereKind::Aurora;
    e.style = std::string(style);
    return true;
}

AtmosphericEffect bioluminescentComet(std::string name) {
    AtmosphericEffect e;
    e.name = std::move(name);
    e.kind = AtmosphereKind::Comet;
    applyCometStyle(e, kCometStyles[0]);
    // A comet is an *event*: §3.5 wants one launch on a musical transition, not a permanent object.
    // A window is the activation a sequencer can move; `lifetime` 0 means "as long as the window",
    // and the window is a little longer than the crossing so the tail has somewhere to drain.
    e.activation = Activation::Window;
    e.timing.windowStart = 4.0;
    e.timing.windowSeconds = 10.0;
    e.timing.fadeIn = 0.5;
    e.timing.fadeOut = 1.8;
    e.comet.path.travelSeconds = 7.0f;
    e.ground.mode = GroundGlow::Subtle;
    e.ground.color = {0.30f, 0.85f, 1.0f};
    return e;
}

AtmosphericEffect glowmereAurora(std::string name) {
    AtmosphericEffect e;
    e.name = std::move(name);
    e.kind = AtmosphereKind::Aurora;
    applyAuroraStyle(e, kAuroraStyles[0]);
    // An aurora is scenery that breathes rather than an event: it is on, and the music moves it.
    e.activation = Activation::Always;
    e.timing.fadeIn = 2.0;
    e.timing.fadeOut = 0.0;
    e.ground.mode = GroundGlow::Subtle;
    e.ground.color = {0.16f, 0.72f, 0.58f};
    e.ground.intensity = 0.5f;
    return e;
}

bool applyVortexStyle(AtmosphericEffect& e, std::string_view style) {
    Vortex& v = e.vortex;
    // Every style writes every field it touches, for the reason the comet's does: a style that
    // leaves the previous one's throat behind reads as the preset being broken.
    // `center` is deliberately not written -- see the header.
    const glm::vec3 keep = v.center;
    v = Vortex{};
    v.center = keep;
    if (style == kVortexStyles[0]) { // Cosmic Funnel -- the Tree of Life's
        v.radius = 200.0f;
        v.thickness = 70.0f;
        v.funnelDepth = 1500.0f;
        v.throat = 0.1f;
        v.throatDensity = 0.55f;
        v.swirl = 6.5f;
        v.rotationSpeed = 0.028f;
        v.density = 0.0013f;
        v.innerVoid = 0.24f;
        v.contrast = 3.6f;
        v.turbulence = 0.65f;
        v.turbulenceScale = 2.4f;
        v.breathAmount = 0.05f;
        v.breathSpeed = 0.18f;
        v.emission = 0.04f;
        v.filaments = 2.2f;
        v.spill = 2.5f;
        v.cometResponse = 0.6f;
        v.cometReach = 6.0f;
        v.colorDeep = {0.016f, 0.012f, 0.062f};
        v.colorMid = {0.050f, 0.085f, 0.240f};
        v.colorAccent = {0.100f, 0.340f, 0.460f};
        return true;
    }
    if (style == kVortexStyles[1]) { // Shallow Disc
        v.radius = 260.0f;
        v.thickness = 45.0f;
        v.funnelDepth = 0.0f;
        v.throat = 0.25f;
        v.throatDensity = 0.6f;
        v.swirl = 4.2f;
        v.rotationSpeed = 0.045f;
        v.density = 0.0016f;
        v.innerVoid = 0.30f;
        v.contrast = 2.6f;
        v.turbulence = 0.45f;
        v.turbulenceScale = 2.0f;
        v.breathAmount = 0.06f;
        v.breathSpeed = 0.22f;
        v.emission = 0.05f;
        v.filaments = 1.6f;
        v.spill = 2.0f;
        v.cometResponse = 0.4f;
        v.cometReach = 6.0f;
        v.colorDeep = {0.014f, 0.018f, 0.055f};
        v.colorMid = {0.045f, 0.100f, 0.210f};
        v.colorAccent = {0.120f, 0.380f, 0.420f};
        return true;
    }
    if (style == kVortexStyles[2]) { // Deep Maelstrom
        v.radius = 170.0f;
        v.thickness = 95.0f;
        v.funnelDepth = 2600.0f;
        v.throat = 0.06f;
        v.throatDensity = 0.75f;
        v.swirl = 9.0f;
        v.rotationSpeed = 0.020f;
        v.density = 0.0018f;
        v.innerVoid = 0.18f;
        v.contrast = 4.4f;
        v.turbulence = 0.80f;
        v.turbulenceScale = 3.0f;
        v.breathAmount = 0.04f;
        v.breathSpeed = 0.14f;
        v.emission = 0.055f;
        v.filaments = 2.8f;
        v.spill = 3.2f;
        v.cometResponse = 0.8f;
        v.cometReach = 7.0f;
        v.colorDeep = {0.022f, 0.010f, 0.070f};
        v.colorMid = {0.060f, 0.070f, 0.260f};
        v.colorAccent = {0.140f, 0.300f, 0.520f};
        return true;
    }
    return false;
}

AtmosphericEffect cosmicVortex(std::string name) {
    AtmosphericEffect e;
    e.name = std::move(name);
    e.kind = AtmosphereKind::Vortex;
    e.style = std::string(kVortexStyles[0]);
    applyVortexStyle(e, kVortexStyles[0]);
    // A vortex is scenery, not an event, and it has no ground pool of its own: ADR-379's spill is
    // how it lights what floats over it, and that is a field on the vortex rather than ADR-230's
    // ground glow, because the glow lands on terrain and there is no terrain under this one.
    e.activation = Activation::Always;
    e.timing.fadeIn = 0.0;
    e.timing.fadeOut = 0.0;
    e.ground.mode = GroundGlow::Off;
    return e;
}

// ---- resolution --------------------------------------------------------------------------------

namespace {

// Progress through the crossing, with the acceleration reparameterisation. Monotone for
// `acceleration > -0.5`, which `CometPath::validate` enforces, and fixed at both ends so changing
// the acceleration does not move where the comet starts or finishes.
float reparameterise(float p, float acceleration) {
    const float a = std::clamp(acceleration, -0.49f, 8.0f);
    return (p + a * p * p) / (1.0f + a);
}

glm::vec3 anchorOf(SkyAnchor anchor, const glm::vec3& authored, const glm::vec3& camera) {
    return anchor == SkyAnchor::Camera ? camera : authored;
}

} // namespace

// The great-circle arc, evaluated at an arc length from the launch point. The same function the
// shader runs (`atmosCometCurve`), so the ground track and the visible head cannot disagree.
//
// Slerp rather than a chord: the distance stays constant, so the apparent azimuth and elevation
// interpolate between the two the artist typed. It extrapolates correctly past either end -- the
// sines simply carry on round the circle -- which is what a comet outliving its authored crossing
// should do, while the bow is clamped to the crossing so it does not bend the comet back through
// its own path afterwards.
glm::vec3 cometPositionAt(const ResolvedAtmospheric& r, float arcLength) {
    if (r.effect == nullptr) {
        return r.launch;
    }
    const float u = r.pathLength > kEps ? arcLength / r.pathLength : 0.0f;
    glm::vec3 d = r.dir0;
    const float sinOmega = std::sin(r.omega);
    if (r.omega > 1e-4f && std::abs(sinOmega) > 1e-6f) {
        d = (std::sin((1.0f - u) * r.omega) * r.dir0 + std::sin(u * r.omega) * r.dir1) / sinOmega;
    }
    const float bow = bowShape(u);
    if (bow > 0.0f) {
        // Perpendicular to the arc's plane, for the sideways bow. Degenerate when the two
        // directions are parallel, which `validate` already refuses.
        const glm::vec3 normal = safeNormalize(glm::cross(r.dir0, r.dir1), glm::vec3(0.0f, 1.0f, 0.0f));
        d += glm::vec3(0.0f, 1.0f, 0.0f) * (r.liftAmount * bow) + normal * (r.curveAmount * bow);
    }
    d = safeNormalize(d, r.dir0);
    return r.anchor + d * r.distance;
}

AtmosphericCounts resolveAtmosphericEffects(std::span<const AtmosphericEffect> effects,
                                            const AtmosphericContext& ctx,
                                            std::span<ResolvedAtmospheric> comets,
                                            std::span<ResolvedAtmospheric> auroras) {
    AtmosphericCounts counts;
    for (const AtmosphericEffect& e : effects) {
        if (!e.enabled) {
            continue;
        }
        // ADR-207's gating, unchanged. An atmospheric effect has no source endpoint, so it never
        // follows a spotlit hero by name -- a `HeroFocus` aurora fires for whichever hero is up.
        const auto window = resolveActivationWindow(e.activation, e.timing, ctx.seconds, ctx.shots, true);
        if (!window) {
            continue;
        }
        const double local = ctx.seconds - window->start - e.timing.delay;
        if (local < 0.0) {
            continue;
        }
        const double windowLength = window->end - window->start - e.timing.delay;
        // A repeating effect restarts every `repeatSeconds`, which is what turns one comet into a
        // burst of them (§3.5). `fmod` rather than a counter, so scrubbing lands the same comet.
        const double pass = e.timing.repeatSeconds > 0.0 ? std::fmod(local, e.timing.repeatSeconds) : local;
        const double passLength = e.timing.repeatSeconds > 0.0
                                      ? std::min(e.timing.repeatSeconds, windowLength)
                                      : windowLength;
        const float envelope = timingEnvelope(e.timing, pass, passLength);
        if (envelope <= 1e-4f) {
            continue;
        }

        ResolvedAtmospheric r;
        r.effect = &e;
        r.envelope = envelope;
        r.elapsed = pass;

        // The dispatch is a `switch` with no `default` on purpose, and every arm sets `claimed`.
        //
        // It used to be `if comet / else if aurora / else vortex`. The vortex was therefore not a
        // *test* but the fall-through, which meant a kind no arm named did not resolve as nothing --
        // it resolved as a vortex, and was then dropped as "the second vortex in the scene". That is
        // a silent no-op wearing a limit's clothes, and it is the exact failure ADR-385 describes
        // from the other direction: a reading that stops anyone checking.
        //
        // Two guards, because neither alone holds. The missing `default` makes a new enumerator a
        // `-Wswitch` diagnostic -- but `AVGEN_WARNINGS_AS_ERRORS` is OFF by default
        // (`CMakeLists.txt`), so that is a line in a build log and not a guard. `claimed` is the
        // half that holds at runtime: an unclaimed kind is counted in `dropped` and drawn by
        // nobody, so it is reportable rather than silently attributed to a neighbour.
        bool claimed = false;
        switch (e.kind) {
        case AtmosphereKind::Comet: {
            if (counts.comets >= comets.size()) {
                ++counts.dropped;
                continue;
            }
            const CometPath& p = e.comet.path;
            r.anchor = anchorOf(p.anchor, p.anchorPosition, ctx.cameraPosition);
            r.dir0 = directionFromSky(p.startAzimuth, p.startElevation);
            r.dir1 = directionFromSky(p.endAzimuth, p.endElevation);
            r.distance = std::max(p.distance, 1.0f);
            // The angle between the two bearings, which with the distance is the arc's length. A
            // comet whose ends have collapsed onto each other has nowhere to fly; `validate` refuses
            // that, but a modulated azimuth can still reach it, so it is checked here too.
            const float cosOmega = std::clamp(glm::dot(r.dir0, r.dir1), -1.0f, 1.0f);
            r.omega = std::acos(cosOmega);
            if (r.omega <= 1e-4f) {
                continue;
            }
            r.pathLength = r.omega * r.distance;
            r.liftAmount = p.arcLift / r.distance;
            r.curveAmount = p.curvature / r.distance;

            const float progress =
                static_cast<float>(pass) * std::max(p.speedScale, kEps) / std::max(p.travelSeconds, kEps);
            r.travelled = reparameterise(progress, p.acceleration) * r.pathLength;
            r.launch = r.anchor + r.dir0 * r.distance;
            r.destination = r.anchor + r.dir1 * r.distance;
            comets[counts.comets++] = r;
            claimed = true;
            break;
        }
        case AtmosphereKind::Aurora: {
            if (counts.auroras >= auroras.size()) {
                ++counts.dropped;
                continue;
            }
            const AuroraShape& s = e.aurora.shape;
            r.anchor = anchorOf(s.anchor, s.anchorPosition, ctx.cameraPosition);
            auroras[counts.auroras++] = r;
            claimed = true;
            break;
        }
        case AtmosphereKind::Vortex: {
            // ADR-387: a vortex has no per-frame trajectory to resolve -- it is a static field the
            // volume march samples -- so resolution is "is it live", and the payload travels
            // unchanged. Counted as dropped past the first, so the second one in a scene is a
            // reported limit rather than a silent no-op.
            if (counts.vortices >= 1) {
                ++counts.dropped;
                continue;
            }
            ++counts.vortices;
            claimed = true;
            break;
        }
        }
        if (!claimed) {
            // A kind no arm above claims. It is reported rather than drawn as something else; see
            // the note at the top of the switch.
            ++counts.dropped;
        }
    }
    return counts;
}

CometGpu packComet(const ResolvedAtmospheric& r) {
    CometGpu g{};
    if (r.effect == nullptr || r.effect->kind != AtmosphereKind::Comet) {
        return g;
    }
    const Comet& c = r.effect->comet;
    const CometAppearance& a = c.appearance;

    g.anchorTravel = glm::vec4(r.anchor, r.travelled);
    g.dir0Tail = glm::vec4(r.dir0, a.tailLength);
    g.dir1Path = glm::vec4(r.dir1, r.pathLength);
    g.arc = glm::vec4(r.distance, r.omega, r.liftAmount, r.curveAmount);
    // The envelope is folded into every radiance rather than carried as a lane the shader would
    // multiply into three terms. A fade is a fade -- the same argument ADR-207 made.
    g.core = glm::vec4(a.coreColor * a.coreIntensity * r.envelope, a.headSize);
    g.halo = glm::vec4(a.haloColor * a.haloIntensity * r.envelope, a.haloSize);
    g.tail = glm::vec4(a.tailColor * a.tailIntensity * r.envelope, a.tailFalloff);
    g.shape = glm::vec4(a.tailWidth, a.wispAmount, a.wispScale,
                        static_cast<float>(r.elapsed) * a.flowSpeed);
    const float fragments = c.sparkle.enabled ? std::max(c.sparkle.density, 0.0f) : 0.0f;
    g.sparkle = glm::vec4(fragments,
                          // Sparkle size is authored as a fraction of a cell; a cell here is
                          // `1 / density` metres of trail, so this is the fragment's world radius.
                          std::clamp(c.sparkle.size, 0.0f, 1.0f) * (fragments > 0.0f ? 0.5f / fragments : 0.0f),
                          c.sparkle.intensity * r.envelope,
                          static_cast<float>(r.elapsed) * c.sparkle.speed +
                              static_cast<float>(c.sparkle.seed) * 0.6180339887f);
    g.rainbow = glm::vec4(c.rainbow.enabled ? c.rainbow.scale / std::max(a.tailLength, 1.0f) : 0.0f,
                          static_cast<float>(r.elapsed) * c.rainbow.speed + c.rainbow.hueOffset,
                          std::clamp(c.rainbow.saturation, 0.0f, 1.0f),
                          c.rainbow.enabled ? std::max(c.rainbow.brightness, 0.0f) : 0.0f);
    return g;
}

AuroraGpu packAurora(const ResolvedAtmospheric& r, std::span<const float> spectrum) {
    AuroraGpu g{};
    if (r.effect == nullptr || r.effect->kind != AtmosphereKind::Aurora) {
        return g;
    }
    const Aurora& au = r.effect->aurora;
    const AuroraShape& s = au.shape;
    const AuroraAppearance& a = au.appearance;
    const AuroraAudio& ad = au.audio;

    g.config = glm::vec4(std::clamp(s.curtainCount, 1.0f, 5.0f), s.radius, s.baseHeight, s.curtainHeight);
    g.shape = glm::vec4(s.waveAmplitude, s.waveScale, s.turbulence, s.complexity);
    g.flow = glm::vec4(static_cast<float>(r.elapsed) * s.flowSpeed,
                       static_cast<float>(r.elapsed) * s.driftSpeed,
                       static_cast<float>(r.elapsed) * s.verticalSpeed, s.layerSpacing);
    const float scale = a.intensity * r.envelope;
    g.low = glm::vec4(a.lowColor * scale, std::clamp(a.opacity, 0.0f, 1.0f));
    g.mid = glm::vec4(a.midColor * scale, a.emission);
    g.top = glm::vec4(a.topColor * scale, a.edgeBrightness);
    g.detail = glm::vec4(a.filaments, a.sparkle, std::clamp(ad.spectrumShape, 0.0f, 1.0f), a.horizonGlow);
    const float sens = std::max(ad.sensitivity, 0.0f);
    g.audio = glm::vec4(ad.bass * sens, ad.lowMid * sens, ad.mid * sens, ad.high * sens);
    g.audio2 = glm::vec4(ad.beat * sens,
                         au.rainbow.enabled ? std::max(au.rainbow.brightness, 0.0f) : 0.0f,
                         au.rainbow.scale,
                         static_cast<float>(r.elapsed) * au.rainbow.speed + au.rainbow.hueOffset);
    g.anchor = glm::vec4(r.anchor, std::clamp(au.rainbow.saturation, 0.0f, 1.0f));

    // The spectrum, or a flat 0.5 when there is no music. 0.5 is the value the shader's height
    // mapping treats as neutral, so silence gives an ordinary curtain rather than a collapsed one.
    std::array<float, kAuroraBands> bands{};
    for (std::size_t i = 0; i < kAuroraBands; ++i) {
        bands[i] = i < spectrum.size() ? std::clamp(spectrum[i], 0.0f, 1.0f) : 0.5f;
    }
    g.band0 = glm::vec4(bands[0], bands[1], bands[2], bands[3]);
    g.band1 = glm::vec4(bands[4], bands[5], bands[6], bands[7]);
    g.band2 = glm::vec4(bands[8], bands[9], bands[10], bands[11]);
    g.band3 = glm::vec4(bands[12], bands[13], bands[14], bands[15]);
    return g;
}

void buildAtmosphericFrame(std::span<const AtmosphericEffect> effects, const AtmosphericContext& ctx,
                           AtmosphericFrame& out) {
    std::array<ResolvedAtmospheric, kMaxGpuComets> comets{};
    std::array<ResolvedAtmospheric, kMaxGpuAuroras> auroras{};
    const AtmosphericCounts counts = resolveAtmosphericEffects(effects, ctx, comets, auroras);

    out.cometCount = static_cast<std::uint32_t>(counts.comets);
    out.auroraCount = static_cast<std::uint32_t>(counts.auroras);
    // ADR-387: the first live vortex, copied through. Its `enabled`, activation and lifetime
    // envelope were already applied by the resolve above, so a vortex inside a closed window
    // arrives here switched off exactly as a comet does.
    out.hasVortex = false;
    out.vortex = Vortex{};
    for (const AtmosphericEffect& e : effects) {
        if (e.kind != AtmosphereKind::Vortex || !e.enabled || !e.vortex.active()) {
            continue;
        }
        out.hasVortex = true;
        out.vortex = e.vortex;
        break;
    }
    for (std::size_t i = 0; i < counts.comets; ++i) {
        out.comets[i] = packComet(comets[i]);
    }
    for (std::size_t i = counts.comets; i < kMaxGpuComets; ++i) {
        out.comets[i] = CometGpu{};
    }
    for (std::size_t i = 0; i < counts.auroras; ++i) {
        out.auroras[i] = packAurora(auroras[i], ctx.spectrum);
    }
    for (std::size_t i = counts.auroras; i < kMaxGpuAuroras; ++i) {
        out.auroras[i] = AuroraGpu{};
    }

    // ---- ground illumination (§6) ----
    //
    // A sum of coloured washes plus the single brightest comet's pool. Summing the auroras is right
    // -- two curtains overhead are twice the light -- while picking one comet is a compromise the
    // ADR records: six pools would be a loop in the surface shader for a term whose whole purpose
    // is a soft wash, and the brightest is the one anybody would notice.
    out.ground = SkyGroundGpu{};
    glm::vec3 ambient(0.0f);
    for (std::size_t i = 0; i < counts.auroras; ++i) {
        const AtmosphericEffect& e = *auroras[i].effect;
        const float scale = groundGlowScale(e.ground.mode) * e.ground.intensity * auroras[i].envelope;
        if (scale > 0.0f) {
            ambient += e.ground.color * scale;
        }
    }
    float brightest = 0.0f;
    for (std::size_t i = 0; i < counts.comets; ++i) {
        const AtmosphericEffect& e = *comets[i].effect;
        const float scale = groundGlowScale(e.ground.mode) * e.ground.intensity * comets[i].envelope;
        if (scale <= 0.0f) {
            continue;
        }
        // A high comet's pool of light lands kilometres away and is usually off screen, which is
        // why the wash exists as well and takes the larger share: it is the part that reads.
        ambient += e.ground.color * scale * 0.45f;
        if (scale > brightest) {
            brightest = scale;
            const glm::vec3 head = cometPositionAt(comets[i], comets[i].travelled);
            out.ground.point = glm::vec4(head.x, 0.0f, head.z, std::max(e.ground.radius, 1.0f));
            out.ground.pointColor = glm::vec4(e.ground.color * scale, std::max(e.ground.falloff, 0.05f));
        }
    }
    out.ground.ambient = glm::vec4(ambient, 0.0f);
}

} // namespace avgen::world
