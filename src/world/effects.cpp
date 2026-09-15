#include "world/effects.hpp"

#include "core/log.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <unordered_set>

namespace avgen::world {
namespace {

using nlohmann::json;

constexpr float kEps = 1e-5f;

glm::vec3 vec3FromJson(const json& j, glm::vec3 fallback) {
    if (!j.is_array() || j.size() < 3) {
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

bool finite(float v) { return std::isfinite(v); }

glm::vec3 safeNormalize(const glm::vec3& v, const glm::vec3& fallback) {
    const float len = glm::length(v);
    return len > kEps ? v / len : fallback;
}

// Smoothstep on a raw ratio, guarding the degenerate width that would otherwise divide by zero and
// give a hard edge exactly where §7 forbids one.
float smoothRamp(float x, float width) {
    if (width <= kEps) {
        return x >= 0.0f ? 1.0f : 0.0f;
    }
    const float t = std::clamp(x / width, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

} // namespace

// ---- names -------------------------------------------------------------------------------------

const char* propagationKindName(PropagationKind k) {
    switch (k) {
    case PropagationKind::DirectionalWave: return "directional";
    case PropagationKind::RadialWave: return "radial";
    }
    return "radial";
}
std::optional<PropagationKind> propagationKindFromName(std::string_view name) {
    if (name == "directional" || name == "directionalWave" || name == "beam") {
        return PropagationKind::DirectionalWave;
    }
    if (name == "radial" || name == "radialWave" || name == "ripple" || name == "pulse") {
        return PropagationKind::RadialWave;
    }
    return std::nullopt;
}

const char* sourceKindName(SourceKind k) {
    switch (k) {
    case SourceKind::World: return "world";
    case SourceKind::Node: return "node";
    case SourceKind::Hero: return "hero";
    case SourceKind::Camera: return "camera";
    case SourceKind::FocusHero: return "focusHero";
    }
    return "world";
}
std::optional<SourceKind> sourceKindFromName(std::string_view name) {
    if (name == "world" || name == "position") { return SourceKind::World; }
    if (name == "node" || name == "entity") { return SourceKind::Node; }
    if (name == "hero") { return SourceKind::Hero; }
    if (name == "camera" || name == "active_camera" || name == "activeCamera") { return SourceKind::Camera; }
    if (name == "focusHero" || name == "focus_hero" || name == "spotlight") { return SourceKind::FocusHero; }
    return std::nullopt;
}

const char* directionModeName(DirectionMode m) {
    switch (m) {
    case DirectionMode::Explicit: return "explicit";
    case DirectionMode::SourceForward: return "sourceForward";
    case DirectionMode::CameraForward: return "cameraForward";
    case DirectionMode::CameraVelocity: return "cameraVelocity";
    case DirectionMode::SourceToTarget: return "sourceToTarget";
    case DirectionMode::CameraToTarget: return "cameraToTarget";
    case DirectionMode::Blended: return "blended";
    }
    return "explicit";
}
std::optional<DirectionMode> directionModeFromName(std::string_view name) {
    if (name == "explicit" || name == "world") { return DirectionMode::Explicit; }
    if (name == "sourceForward") { return DirectionMode::SourceForward; }
    if (name == "cameraForward") { return DirectionMode::CameraForward; }
    if (name == "cameraVelocity" || name == "cameraMovement") { return DirectionMode::CameraVelocity; }
    if (name == "sourceToTarget") { return DirectionMode::SourceToTarget; }
    if (name == "cameraToTarget") { return DirectionMode::CameraToTarget; }
    if (name == "blended" || name == "blend") { return DirectionMode::Blended; }
    return std::nullopt;
}

const char* activationName(Activation a) {
    switch (a) {
    case Activation::Always: return "always";
    case Activation::Window: return "window";
    case Activation::CameraTravel: return "cameraTravel";
    case Activation::HeroFocus: return "heroFocus";
    }
    return "always";
}
std::optional<Activation> activationFromName(std::string_view name) {
    if (name == "always") { return Activation::Always; }
    if (name == "window" || name == "time") { return Activation::Window; }
    if (name == "cameraTravel" || name == "travel" || name == "transition") { return Activation::CameraTravel; }
    if (name == "heroFocus" || name == "focus" || name == "spotlight") { return Activation::HeroFocus; }
    return std::nullopt;
}

// ---- validation --------------------------------------------------------------------------------

Result<void> EffectEndpoint::validate() const {
    if ((kind == SourceKind::Node || kind == SourceKind::Hero) && name.empty()) {
        return fail("a {} source needs a name", sourceKindName(kind));
    }
    if (!finite(position.x) || !finite(position.y) || !finite(position.z) || !finite(groundOffset)) {
        return fail("an effect endpoint has a non-finite position");
    }
    return {};
}

Result<void> Propagation::validate() const {
    if (!(speed > 0.0f) || !finite(speed)) { return fail("propagation speed must be positive"); }
    if (!(range > 0.0f) || !finite(range)) { return fail("propagation range must be positive"); }
    if (!(frontWidth > 0.0f) || !finite(frontWidth)) { return fail("front width must be positive"); }
    if (trailLength < 0.0f || !finite(trailLength)) { return fail("trail length must not be negative"); }
    if (!(falloff > 0.0f) || !finite(falloff)) { return fail("falloff must be positive"); }
    if (verticalExtent < 0.0f || !finite(verticalExtent)) { return fail("vertical extent must not be negative"); }
    if (verticalGrowth < 0.0f || !finite(verticalGrowth)) { return fail("vertical growth must not be negative"); }
    if (ringCount < 0.0f || !finite(ringCount)) { return fail("ring count must not be negative"); }
    if (beamRadius < 0.0f || !finite(beamRadius)) { return fail("beam radius must not be negative"); }
    if (glm::length(explicitDirection) <= kEps) {
        return fail("the explicit direction is zero-length");
    }
    return {};
}

Result<void> Appearance::validate() const {
    if (!finite(intensity) || intensity < 0.0f) { return fail("appearance intensity must not be negative"); }
    if (!finite(edgeIntensity) || edgeIntensity < 0.0f) { return fail("edge intensity must not be negative"); }
    if (!finite(width) || width <= 0.0f) { return fail("appearance width must be positive"); }
    if (!finite(rainbowScale) || !finite(rainbowSpeed)) { return fail("the rainbow controls are not finite"); }
    return {};
}

Result<void> Sparkle::validate() const {
    if (density < 0.0f || !finite(density)) { return fail("sparkle density must not be negative"); }
    if (size < 0.0f || size > 1.0f) { return fail("sparkle size is a fraction of a cell, 0..1"); }
    if (intensity < 0.0f || !finite(intensity)) { return fail("sparkle intensity must not be negative"); }
    if (fadeDistance <= 0.0f || !finite(fadeDistance)) { return fail("sparkle fade distance must be positive"); }
    return {};
}

Result<void> MaterialResponse::validate() const {
    for (const float v : {ground, foliage, surface, emissive}) {
        if (!finite(v) || v < 0.0f) {
            return fail("a material response weight must be finite and not negative");
        }
    }
    return {};
}

Result<void> Timing::validate() const {
    if (delay < 0.0) { return fail("delay must not be negative"); }
    if (lifetime < 0.0) { return fail("lifetime must not be negative (0 = as long as the activation)"); }
    if (fadeIn < 0.0 || fadeOut < 0.0) { return fail("fades must not be negative"); }
    if (windowSeconds < 0.0) { return fail("the window length must not be negative"); }
    if (repeatSeconds < 0.0) { return fail("the repeat interval must not be negative"); }
    return {};
}

Result<void> WorldEffect::validate() const {
    if (name.empty()) { return fail("a world effect needs a name"); }
    if (name.find('/') != std::string::npos) {
        // The name is half of a parameter path (`worldfx/<name>/intensity`); a slash in it would
        // silently invent a group nobody can address.
        return fail("world effect '{}': a name may not contain '/'", name);
    }
    if (auto ok = source.validate(); !ok) { return fail("world effect '{}': source: {}", name, ok.error().message); }
    if (hasTarget) {
        if (auto ok = target.validate(); !ok) { return fail("world effect '{}': target: {}", name, ok.error().message); }
    }
    if (auto ok = propagation.validate(); !ok) { return fail("world effect '{}': {}", name, ok.error().message); }
    if (auto ok = appearance.validate(); !ok) { return fail("world effect '{}': {}", name, ok.error().message); }
    if (auto ok = sparkle.validate(); !ok) { return fail("world effect '{}': {}", name, ok.error().message); }
    if (auto ok = response.validate(); !ok) { return fail("world effect '{}': {}", name, ok.error().message); }
    if (auto ok = timing.validate(); !ok) { return fail("world effect '{}': {}", name, ok.error().message); }
    const bool needsTarget = propagation.direction == DirectionMode::SourceToTarget ||
                             propagation.direction == DirectionMode::CameraToTarget;
    if (needsTarget && !hasTarget) {
        return fail("world effect '{}': direction '{}' needs a target", name,
                    directionModeName(propagation.direction));
    }
    return {};
}

Result<void> validateWorldEffects(std::span<const WorldEffect> effects) {
    std::unordered_set<std::string> seen;
    for (const WorldEffect& e : effects) {
        if (auto ok = e.validate(); !ok) {
            return ok;
        }
        if (!seen.insert(e.name).second) {
            return fail("two world effects are named '{}'; a name is half of a parameter path", e.name);
        }
    }
    return {};
}

// ---- JSON --------------------------------------------------------------------------------------

namespace {

json endpointToJson(const EffectEndpoint& e) {
    json j;
    j["kind"] = sourceKindName(e.kind);
    if (!e.name.empty()) { j["name"] = e.name; }
    if (e.kind == SourceKind::World || e.position != glm::vec3(0.0f)) { j["position"] = vec3ToJson(e.position); }
    if (e.groundOffset != 0.0f) { j["groundOffset"] = e.groundOffset; }
    return j;
}

Result<EffectEndpoint> endpointFromJson(const json& j) {
    if (!j.is_object()) { return fail("an endpoint must be an object"); }
    EffectEndpoint e;
    const std::string kind = readString(j, "kind", "world");
    const auto parsed = sourceKindFromName(kind);
    if (!parsed) { return fail("unknown source kind '{}'", kind); }
    e.kind = *parsed;
    e.name = readString(j, "name");
    e.position = j.contains("position") ? vec3FromJson(j.at("position"), e.position) : e.position;
    e.groundOffset = readFloat(j, "groundOffset", e.groundOffset);
    if (auto ok = e.validate(); !ok) { return std::unexpected(ok.error()); }
    return e;
}

} // namespace

json WorldEffect::toJson() const {
    json j;
    j["name"] = name;
    j["enabled"] = enabled;
    if (!style.empty()) { j["style"] = style; }
    j["source"] = endpointToJson(source);
    if (hasTarget) { j["target"] = endpointToJson(target); }
    j["activation"] = activationName(activation);

    json p;
    p["kind"] = propagationKindName(propagation.kind);
    p["direction"] = directionModeName(propagation.direction);
    p["explicitDirection"] = vec3ToJson(propagation.explicitDirection);
    p["forwardWeight"] = propagation.forwardWeight;
    p["velocityWeight"] = propagation.velocityWeight;
    p["targetWeight"] = propagation.targetWeight;
    p["speed"] = propagation.speed;
    p["range"] = propagation.range;
    p["frontWidth"] = propagation.frontWidth;
    p["trailLength"] = propagation.trailLength;
    p["falloff"] = propagation.falloff;
    p["startOffset"] = propagation.startOffset;
    p["verticalExtent"] = propagation.verticalExtent;
    p["verticalGrowth"] = propagation.verticalGrowth;
    p["ringCount"] = propagation.ringCount;
    p["beamRadius"] = propagation.beamRadius;
    j["propagation"] = std::move(p);

    json a;
    a["color"] = vec3ToJson(appearance.color);
    a["intensity"] = appearance.intensity;
    a["edgeColor"] = vec3ToJson(appearance.edgeColor);
    a["edgeIntensity"] = appearance.edgeIntensity;
    a["width"] = appearance.width;
    a["rainbow"] = appearance.rainbow;
    a["rainbowSpeed"] = appearance.rainbowSpeed;
    a["rainbowScale"] = appearance.rainbowScale;
    a["rainbowSaturation"] = appearance.rainbowSaturation;
    a["rainbowBrightness"] = appearance.rainbowBrightness;
    j["appearance"] = std::move(a);

    json s;
    s["enabled"] = sparkle.enabled;
    s["density"] = sparkle.density;
    s["size"] = sparkle.size;
    s["intensity"] = sparkle.intensity;
    s["speed"] = sparkle.speed;
    s["fadeDistance"] = sparkle.fadeDistance;
    s["seed"] = sparkle.seed;
    j["sparkle"] = std::move(s);

    json r;
    r["ground"] = response.ground;
    r["foliage"] = response.foliage;
    r["surface"] = response.surface;
    r["emissive"] = response.emissive;
    j["response"] = std::move(r);

    json t;
    t["delay"] = timing.delay;
    t["lifetime"] = timing.lifetime;
    t["fadeIn"] = timing.fadeIn;
    t["fadeOut"] = timing.fadeOut;
    t["windowStart"] = timing.windowStart;
    t["windowSeconds"] = timing.windowSeconds;
    t["repeatSeconds"] = timing.repeatSeconds;
    j["timing"] = std::move(t);
    return j;
}

Result<WorldEffect> WorldEffect::fromJson(const json& j) {
    if (!j.is_object()) { return fail("a world effect must be an object"); }
    WorldEffect e;
    e.name = readString(j, "name");
    e.enabled = readBool(j, "enabled", true);
    e.style = readString(j, "style");

    if (j.contains("source")) {
        auto src = endpointFromJson(j.at("source"));
        if (!src) { return std::unexpected(src.error()); }
        e.source = std::move(*src);
    }
    if (j.contains("target")) {
        auto dst = endpointFromJson(j.at("target"));
        if (!dst) { return std::unexpected(dst.error()); }
        e.target = std::move(*dst);
        e.hasTarget = true;
    }
    if (j.contains("activation")) {
        const std::string name = readString(j, "activation", "always");
        const auto parsed = activationFromName(name);
        if (!parsed) { return fail("unknown activation '{}'", name); }
        e.activation = *parsed;
    }
    if (j.contains("propagation") && j.at("propagation").is_object()) {
        const json& p = j.at("propagation");
        if (p.contains("kind")) {
            const std::string name = readString(p, "kind", "radial");
            const auto parsed = propagationKindFromName(name);
            if (!parsed) { return fail("unknown propagation kind '{}'", name); }
            e.propagation.kind = *parsed;
        }
        if (p.contains("direction")) {
            const std::string name = readString(p, "direction", "explicit");
            const auto parsed = directionModeFromName(name);
            if (!parsed) { return fail("unknown direction mode '{}'", name); }
            e.propagation.direction = *parsed;
        }
        e.propagation.explicitDirection =
            p.contains("explicitDirection") ? vec3FromJson(p.at("explicitDirection"), e.propagation.explicitDirection)
                                            : e.propagation.explicitDirection;
        e.propagation.forwardWeight = readFloat(p, "forwardWeight", e.propagation.forwardWeight);
        e.propagation.velocityWeight = readFloat(p, "velocityWeight", e.propagation.velocityWeight);
        e.propagation.targetWeight = readFloat(p, "targetWeight", e.propagation.targetWeight);
        e.propagation.speed = readFloat(p, "speed", e.propagation.speed);
        e.propagation.range = readFloat(p, "range", e.propagation.range);
        e.propagation.frontWidth = readFloat(p, "frontWidth", e.propagation.frontWidth);
        e.propagation.trailLength = readFloat(p, "trailLength", e.propagation.trailLength);
        e.propagation.falloff = readFloat(p, "falloff", e.propagation.falloff);
        e.propagation.startOffset = readFloat(p, "startOffset", e.propagation.startOffset);
        e.propagation.verticalExtent = readFloat(p, "verticalExtent", e.propagation.verticalExtent);
        e.propagation.verticalGrowth = readFloat(p, "verticalGrowth", e.propagation.verticalGrowth);
        e.propagation.ringCount = readFloat(p, "ringCount", e.propagation.ringCount);
        e.propagation.beamRadius = readFloat(p, "beamRadius", e.propagation.beamRadius);
    }
    if (j.contains("appearance") && j.at("appearance").is_object()) {
        const json& a = j.at("appearance");
        e.appearance.color = a.contains("color") ? vec3FromJson(a.at("color"), e.appearance.color) : e.appearance.color;
        e.appearance.intensity = readFloat(a, "intensity", e.appearance.intensity);
        e.appearance.edgeColor =
            a.contains("edgeColor") ? vec3FromJson(a.at("edgeColor"), e.appearance.edgeColor) : e.appearance.edgeColor;
        e.appearance.edgeIntensity = readFloat(a, "edgeIntensity", e.appearance.edgeIntensity);
        e.appearance.width = readFloat(a, "width", e.appearance.width);
        e.appearance.rainbow = readBool(a, "rainbow", e.appearance.rainbow);
        e.appearance.rainbowSpeed = readFloat(a, "rainbowSpeed", e.appearance.rainbowSpeed);
        e.appearance.rainbowScale = readFloat(a, "rainbowScale", e.appearance.rainbowScale);
        e.appearance.rainbowSaturation = readFloat(a, "rainbowSaturation", e.appearance.rainbowSaturation);
        e.appearance.rainbowBrightness = readFloat(a, "rainbowBrightness", e.appearance.rainbowBrightness);
    }
    if (j.contains("sparkle") && j.at("sparkle").is_object()) {
        const json& s = j.at("sparkle");
        e.sparkle.enabled = readBool(s, "enabled", e.sparkle.enabled);
        e.sparkle.density = readFloat(s, "density", e.sparkle.density);
        e.sparkle.size = readFloat(s, "size", e.sparkle.size);
        e.sparkle.intensity = readFloat(s, "intensity", e.sparkle.intensity);
        e.sparkle.speed = readFloat(s, "speed", e.sparkle.speed);
        e.sparkle.fadeDistance = readFloat(s, "fadeDistance", e.sparkle.fadeDistance);
        if (s.contains("seed") && s.at("seed").is_number_unsigned()) {
            e.sparkle.seed = s.at("seed").get<std::uint32_t>();
        }
    }
    if (j.contains("response") && j.at("response").is_object()) {
        const json& r = j.at("response");
        e.response.ground = readFloat(r, "ground", e.response.ground);
        e.response.foliage = readFloat(r, "foliage", e.response.foliage);
        e.response.surface = readFloat(r, "surface", e.response.surface);
        e.response.emissive = readFloat(r, "emissive", e.response.emissive);
    }
    if (j.contains("timing") && j.at("timing").is_object()) {
        const json& t = j.at("timing");
        e.timing.delay = readDouble(t, "delay", e.timing.delay);
        e.timing.lifetime = readDouble(t, "lifetime", e.timing.lifetime);
        e.timing.fadeIn = readDouble(t, "fadeIn", e.timing.fadeIn);
        e.timing.fadeOut = readDouble(t, "fadeOut", e.timing.fadeOut);
        e.timing.windowStart = readDouble(t, "windowStart", e.timing.windowStart);
        e.timing.windowSeconds = readDouble(t, "windowSeconds", e.timing.windowSeconds);
        e.timing.repeatSeconds = readDouble(t, "repeatSeconds", e.timing.repeatSeconds);
    }
    if (auto ok = e.validate(); !ok) { return std::unexpected(ok.error()); }
    return e;
}

// ---- presets -----------------------------------------------------------------------------------

namespace {

constexpr std::array<std::string_view, 5> kBeamStyles{"Bioluminescent", "Rainbow", "Energy", "Magical", "Subtle"};
constexpr std::array<std::string_view, 4> kPulseStyles{"Water", "Bioluminescent", "Shockwave", "Magical"};

} // namespace

std::span<const std::string_view> beamStyleNames() { return kBeamStyles; }
std::span<const std::string_view> pulseStyleNames() { return kPulseStyles; }

bool applyBeamStyle(WorldEffect& e, std::string_view style) {
    Appearance& a = e.appearance;
    Sparkle& s = e.sparkle;
    Propagation& p = e.propagation;
    if (style == "Bioluminescent") {
        a.color = glm::vec3(0.07f, 0.78f, 1.0f);
        a.intensity = 2.3f;
        a.edgeColor = glm::vec3(0.72f, 1.0f, 0.92f);
        a.edgeIntensity = 3.4f;
        a.rainbow = false;
        s.enabled = true;
        // Cells per metre. Coarse cells do not read as sparkle -- at 0.5 (two-metre cells) the
        // leading edge grew soft discs the size of a bush. Finer cells alias sooner, which is what
        // `fadeDistance` is for.
        s.density = 1.1f;
        s.size = 0.22f;
        s.intensity = 1.9f;
        p.frontWidth = 16.0f;
        p.trailLength = 52.0f;
        p.falloff = 1.6f;
    } else if (style == "Rainbow") {
        a.color = glm::vec3(1.0f);
        a.intensity = 2.0f;
        a.edgeColor = glm::vec3(1.0f);
        a.edgeIntensity = 2.6f;
        a.rainbow = true;
        a.rainbowSpeed = 0.3f;
        // Cycles per metre. 0.010 was one full sweep per 100 m, which over a band 80 m long is less
        // than one turn -- rendered, it read as a single tinted stripe rather than as a rainbow.
        // 0.025 puts two turns inside the band, which is where the eye starts calling it one.
        a.rainbowScale = 0.025f;
        a.rainbowSaturation = 0.9f;
        a.rainbowBrightness = 1.05f;
        s.enabled = true;
        s.density = 1.1f;
        s.size = 0.22f;
        s.intensity = 2.1f;
        p.frontWidth = 18.0f;
        p.trailLength = 64.0f;
        p.falloff = 1.3f;
    } else if (style == "Energy") {
        a.color = glm::vec3(0.35f, 0.62f, 1.0f);
        a.intensity = 3.1f;
        a.edgeColor = glm::vec3(1.0f, 0.95f, 0.80f);
        a.edgeIntensity = 5.2f;
        a.rainbow = false;
        s.enabled = true;
        s.density = 0.85f;
        s.size = 0.26f;
        s.intensity = 2.4f;
        p.frontWidth = 8.0f;
        p.trailLength = 30.0f;
        p.falloff = 2.3f;
    } else if (style == "Magical") {
        a.color = glm::vec3(0.62f, 0.34f, 1.0f);
        a.intensity = 2.2f;
        a.edgeColor = glm::vec3(1.0f, 0.82f, 0.96f);
        a.edgeIntensity = 3.0f;
        a.rainbow = false;
        s.enabled = true;
        s.density = 0.45f;
        s.size = 0.42f;
        s.intensity = 2.0f;
        s.speed = 0.4f;
        p.frontWidth = 22.0f;
        p.trailLength = 72.0f;
        p.falloff = 1.1f;
    } else if (style == "Subtle") {
        a.color = glm::vec3(0.30f, 0.72f, 0.86f);
        a.intensity = 0.85f;
        a.edgeColor = glm::vec3(0.62f, 0.90f, 0.95f);
        a.edgeIntensity = 1.1f;
        a.rainbow = false;
        s.enabled = false;
        p.frontWidth = 26.0f;
        p.trailLength = 60.0f;
        p.falloff = 1.5f;
    } else {
        return false;
    }
    e.style = std::string(style);
    return true;
}

bool applyPulseStyle(WorldEffect& e, std::string_view style) {
    Appearance& a = e.appearance;
    Sparkle& s = e.sparkle;
    Propagation& p = e.propagation;
    if (style == "Water") {
        a.color = glm::vec3(0.22f, 0.58f, 0.86f);
        a.intensity = 1.5f;
        a.edgeColor = glm::vec3(0.82f, 0.96f, 1.0f);
        a.edgeIntensity = 2.4f;
        a.rainbow = false;
        s.enabled = false;
        p.frontWidth = 2.4f;
        p.trailLength = 16.0f;
        p.ringCount = 3.0f;
        p.falloff = 1.7f;
    } else if (style == "Bioluminescent") {
        a.color = glm::vec3(0.16f, 0.92f, 0.62f);
        a.intensity = 2.0f;
        a.edgeColor = glm::vec3(0.78f, 1.0f, 0.88f);
        a.edgeIntensity = 3.4f;
        a.rainbow = false;
        s.enabled = true;
        s.density = 0.9f;
        s.size = 0.30f;
        s.intensity = 1.8f;
        p.frontWidth = 3.0f;
        p.trailLength = 18.0f;
        p.ringCount = 2.0f;
        p.falloff = 1.5f;
    } else if (style == "Shockwave") {
        a.color = glm::vec3(1.0f, 0.62f, 0.26f);
        a.intensity = 2.6f;
        a.edgeColor = glm::vec3(1.0f, 0.94f, 0.78f);
        a.edgeIntensity = 6.0f;
        a.rainbow = false;
        s.enabled = false;
        p.frontWidth = 1.4f;
        p.trailLength = 7.0f;
        p.ringCount = 0.0f;
        p.falloff = 2.8f;
    } else if (style == "Magical") {
        a.color = glm::vec3(0.58f, 0.36f, 1.0f);
        a.intensity = 1.9f;
        a.edgeColor = glm::vec3(0.96f, 0.84f, 1.0f);
        a.edgeIntensity = 3.0f;
        a.rainbow = false;
        s.enabled = true;
        s.density = 0.7f;
        s.size = 0.38f;
        s.intensity = 2.0f;
        p.frontWidth = 3.6f;
        p.trailLength = 24.0f;
        p.ringCount = 4.0f;
        p.falloff = 1.2f;
    } else {
        return false;
    }
    e.style = std::string(style);
    return true;
}

WorldEffect cameraTravelBeam(std::string name) {
    WorldEffect e;
    e.name = std::move(name);
    e.source.kind = SourceKind::Camera;
    // The beam is *going somewhere*: the subject the shot is travelling to. `FocusHero` resolves to
    // whichever hero the schedule names, which is what keeps this one effect rather than one per
    // hero -- and what keeps it free of Glowmere's coordinates.
    e.hasTarget = true;
    e.target.kind = SourceKind::FocusHero;
    e.activation = Activation::CameraTravel;
    e.propagation.kind = PropagationKind::DirectionalWave;
    // Blended, because none of the three alone is right: forward alone points at where the operator
    // is looking rather than where the camera is going, velocity alone flails on a bowed path, and
    // camera-to-target alone ignores the arc entirely (ADR-207).
    e.propagation.direction = DirectionMode::Blended;
    e.propagation.forwardWeight = 1.0f;
    e.propagation.velocityWeight = 0.6f;
    e.propagation.targetWeight = 1.2f;
    e.propagation.speed = 95.0f;
    e.propagation.range = 320.0f;
    e.propagation.startOffset = 12.0f;
    e.propagation.verticalExtent = 90.0f;
    e.propagation.verticalGrowth = 0.30f;
    e.propagation.ringCount = 0.0f;
    e.timing.delay = 0.15;
    e.timing.fadeIn = 0.5;
    e.timing.fadeOut = 1.1;
    e.timing.repeatSeconds = 3.2;
    applyBeamStyle(e, "Bioluminescent");
    return e;
}

WorldEffect heroGroundPulse(std::string name) {
    WorldEffect e;
    e.name = std::move(name);
    e.source.kind = SourceKind::FocusHero;
    // Down to the base. A hero's position is the centre of the object the camera is pointed at; a
    // ripple that starts in the air is a ring floating around a mushroom cap.
    e.source.groundOffset = 0.0f;
    e.activation = Activation::HeroFocus;
    e.propagation.kind = PropagationKind::RadialWave;
    e.propagation.direction = DirectionMode::Explicit;
    e.propagation.speed = 11.0f;
    e.propagation.range = 46.0f;
    e.propagation.verticalExtent = 4.5f;
    e.propagation.verticalGrowth = 0.30f;
    e.timing.delay = 0.35;
    e.timing.fadeIn = 0.6;
    e.timing.fadeOut = 1.2;
    // One ring per two seconds by default; a route from `beat.pulse` onto `worldfx/<n>/intensity`
    // is what makes it musical, and the UI's Beat Response slider is what writes that route.
    e.timing.repeatSeconds = 2.0;
    applyPulseStyle(e, "Bioluminescent");
    return e;
}

// ---- resolution --------------------------------------------------------------------------------

std::optional<ActivationWindow> resolveActivationWindow(Activation activation, const Timing& timing,
                                                        double seconds, std::span<const ShotSpan> shots,
                                                        bool followsFocus, std::string_view subject) {
    switch (activation) {
    case Activation::Always:
        return ActivationWindow{0.0, std::numeric_limits<double>::infinity(), nullptr};
    case Activation::Window: {
        const double end = timing.windowStart + timing.windowSeconds;
        if (seconds < timing.windowStart || seconds >= end) {
            return std::nullopt;
        }
        return ActivationWindow{timing.windowStart, end, nullptr};
    }
    case Activation::CameraTravel:
        for (const ShotSpan& s : shots) {
            if (s.travel && seconds >= s.start && seconds < s.end) {
                return ActivationWindow{s.start, s.end, &s};
            }
        }
        return std::nullopt;
    case Activation::HeroFocus:
        for (const ShotSpan& s : shots) {
            if (!s.spotlight || seconds < s.start || seconds >= s.end) {
                continue;
            }
            // A `FocusHero` source follows whatever is spotlit; a named source only fires for its
            // own subject, which is what lets a scene give one hero its own effect.
            if (followsFocus || subject.empty() || subject == s.subject) {
                return ActivationWindow{s.start, s.end, &s};
            }
        }
        return std::nullopt;
    }
    return std::nullopt;
}

float envelopeRamp(float x, float width) { return smoothRamp(x, width); }

float timingEnvelope(const Timing& timing, double local, double windowLength) {
    if (local < 0.0) {
        return 0.0f;
    }
    const double lifetime = timing.lifetime > 0.0 ? timing.lifetime : windowLength;
    if (std::isfinite(lifetime) && local >= lifetime) {
        return 0.0f;
    }
    float envelope = 1.0f;
    if (timing.fadeIn > 0.0) {
        envelope *= smoothRamp(static_cast<float>(local), static_cast<float>(timing.fadeIn));
    }
    if (timing.fadeOut > 0.0 && std::isfinite(lifetime)) {
        envelope *= smoothRamp(static_cast<float>(lifetime - local), static_cast<float>(timing.fadeOut));
    }
    return envelope;
}

namespace {

using Window = ActivationWindow;

std::optional<Window> activeWindow(const WorldEffect& e, const WorldEffectContext& ctx) {
    return resolveActivationWindow(e.activation, e.timing, ctx.seconds, ctx.shots,
                                   e.source.kind == SourceKind::FocusHero, e.source.name);
}

const HeroPoint* findHero(std::span<const HeroPoint> heroes, std::string_view name) {
    for (const HeroPoint& h : heroes) {
        if (h.name == name) {
            return &h;
        }
    }
    return nullptr;
}

// Where a hero *stands*, as distinct from where its bounds are centred.
//
// A hero is one object (ADR-107) and its name is that object's node, so when the scene has a node of
// that name its transform is where the thing stands -- which for Glowmere's elder is the ground at
// its stem, twelve metres below the `HeroPoint::position` that describes its cap. A ripple through
// the ground wants the former; ADR-199 is the record of what assuming the two are the same costs.
// Falls back to the hero's own position for a hero with no node of its name.
glm::vec3 heroStandsAt(const HeroPoint& hero, const WorldEffectContext& ctx) {
    glm::vec3 at{0.0f};
    if (ctx.scene != nullptr && ctx.scene->nodePosition(hero.name, at)) {
        return at;
    }
    return hero.position;
}

// Where an endpoint is. `accent` receives the hero's own colour when the endpoint resolved to one,
// so an effect can be the colour of the thing it is about without anybody typing it twice.
bool resolveEndpoint(const EffectEndpoint& e, const WorldEffectContext& ctx, const ShotSpan* span,
                     glm::vec3& out, const glm::vec3** accent) {
    switch (e.kind) {
    case SourceKind::World:
        out = e.position;
        break;
    case SourceKind::Camera:
        out = ctx.cameraPosition;
        break;
    case SourceKind::Node: {
        if (ctx.scene == nullptr || !ctx.scene->nodePosition(e.name, out)) {
            // A named node that is not in this scene falls back to the authored position rather than
            // refusing: a scene swap must not make an effect jump to the origin.
            out = e.position;
        }
        break;
    }
    case SourceKind::Hero: {
        const HeroPoint* hero = findHero(ctx.heroes, e.name);
        if (hero == nullptr) {
            out = e.position;
            break;
        }
        out = heroStandsAt(*hero, ctx);
        if (accent != nullptr) { *accent = &hero->colorAccent; }
        break;
    }
    case SourceKind::FocusHero: {
        if (span == nullptr || span->subject.empty()) {
            return false;
        }
        const HeroPoint* hero = findHero(ctx.heroes, span->subject);
        if (hero != nullptr) {
            out = heroStandsAt(*hero, ctx);
            if (accent != nullptr) { *accent = &hero->colorAccent; }
        } else {
            out = span->subjectPosition;
        }
        break;
    }
    }
    out.y -= e.groundOffset;
    return true;
}

// The travel target for a beam. A `FocusHero` target during a travel shot means the *handoff*, not
// the subject being left -- which is the whole point of pointing a beam at where the camera is going.
bool resolveTravelTarget(const WorldEffect& e, const WorldEffectContext& ctx, const ShotSpan* span,
                         glm::vec3& out) {
    if (!e.hasTarget) {
        return false;
    }
    if (e.target.kind == SourceKind::FocusHero && span != nullptr) {
        if (!span->handoff.empty()) {
            const HeroPoint* hero = findHero(ctx.heroes, span->handoff);
            out = hero != nullptr ? hero->position : span->handoffPosition;
            out.y -= e.target.groundOffset;
            return true;
        }
        if (!span->subject.empty()) {
            const HeroPoint* hero = findHero(ctx.heroes, span->subject);
            out = hero != nullptr ? hero->position : span->subjectPosition;
            out.y -= e.target.groundOffset;
            return true;
        }
        return false;
    }
    return resolveEndpoint(e.target, ctx, span, out, nullptr);
}

glm::vec3 resolveAxis(const WorldEffect& e, const WorldEffectContext& ctx, const ShotSpan* span,
                      const glm::vec3& origin) {
    const glm::vec3 fallback = safeNormalize(e.propagation.explicitDirection, glm::vec3(0.0f, 0.0f, -1.0f));
    const auto toTarget = [&](const glm::vec3& from) -> std::optional<glm::vec3> {
        glm::vec3 target{0.0f};
        if (!resolveTravelTarget(e, ctx, span, target)) {
            return std::nullopt;
        }
        const glm::vec3 d = target - from;
        return glm::length(d) > kEps ? std::optional<glm::vec3>(glm::normalize(d)) : std::nullopt;
    };
    switch (e.propagation.direction) {
    case DirectionMode::Explicit:
        return fallback;
    case DirectionMode::SourceForward: {
        glm::vec3 fwd{0.0f};
        if (e.source.kind == SourceKind::Camera) {
            return safeNormalize(ctx.cameraForward, fallback);
        }
        if (e.source.kind == SourceKind::Node && ctx.scene != nullptr && ctx.scene->nodeForward(e.source.name, fwd)) {
            return safeNormalize(fwd, fallback);
        }
        return fallback;
    }
    case DirectionMode::CameraForward:
        return safeNormalize(ctx.cameraForward, fallback);
    case DirectionMode::CameraVelocity:
        return safeNormalize(ctx.cameraVelocity, safeNormalize(ctx.cameraForward, fallback));
    case DirectionMode::SourceToTarget:
        return toTarget(origin).value_or(fallback);
    case DirectionMode::CameraToTarget:
        return toTarget(ctx.cameraPosition).value_or(safeNormalize(ctx.cameraForward, fallback));
    case DirectionMode::Blended: {
        glm::vec3 sum(0.0f);
        sum += safeNormalize(ctx.cameraForward, glm::vec3(0.0f)) * e.propagation.forwardWeight;
        sum += safeNormalize(ctx.cameraVelocity, glm::vec3(0.0f)) * e.propagation.velocityWeight;
        if (const auto t = toTarget(ctx.cameraPosition)) {
            sum += *t * e.propagation.targetWeight;
        }
        return safeNormalize(sum, safeNormalize(ctx.cameraForward, fallback));
    }
    }
    return fallback;
}

} // namespace

std::size_t resolveWorldEffects(std::span<const WorldEffect> effects, const WorldEffectContext& ctx,
                                std::span<ResolvedEffect> out) {
    std::size_t written = 0;
    for (const WorldEffect& e : effects) {
        if (written >= out.size()) {
            break;
        }
        if (!e.enabled) {
            continue;
        }
        const auto window = activeWindow(e, ctx);
        if (!window) {
            continue;
        }
        // Time since the activation opened, minus the delay. Everything below is a function of this
        // and of the authored numbers: no state, no history, no frame counter.
        const double local = ctx.seconds - window->start - e.timing.delay;
        if (local < 0.0) {
            continue;
        }
        const double windowLength = window->end - window->start - e.timing.delay;
        const double lifetime = e.timing.lifetime > 0.0 ? e.timing.lifetime : windowLength;
        if (std::isfinite(lifetime) && local >= lifetime) {
            continue;
        }
        float envelope = 1.0f;
        if (e.timing.fadeIn > 0.0) {
            envelope *= smoothRamp(static_cast<float>(local), static_cast<float>(e.timing.fadeIn));
        }
        if (e.timing.fadeOut > 0.0 && std::isfinite(lifetime)) {
            const double remaining = lifetime - local;
            envelope *= smoothRamp(static_cast<float>(remaining), static_cast<float>(e.timing.fadeOut));
        }
        if (envelope <= 1e-4f) {
            continue;
        }

        // Where the front is. A repeating effect restarts its front every `repeatSeconds`, which is
        // what makes a pulse a pulse; `fmod` rather than a counter, so scrubbing lands the same ring.
        const double pass = e.timing.repeatSeconds > 0.0 ? std::fmod(local, e.timing.repeatSeconds) : local;
        const float front = e.propagation.startOffset + e.propagation.speed * static_cast<float>(pass);
        if (front > e.propagation.range + e.propagation.trailLength * e.appearance.width) {
            continue; // this pass is over the horizon and the next one has not started
        }

        ResolvedEffect r;
        r.effect = &e;
        r.envelope = envelope;
        r.frontDistance = front;
        r.elapsed = local;
        const glm::vec3* accent = nullptr;
        if (!resolveEndpoint(e.source, ctx, window->span, r.origin, &accent)) {
            continue; // a FocusHero source with nothing spotlit is not an error, it is inactive
        }
        r.axis = e.propagation.kind == PropagationKind::DirectionalWave
                     ? resolveAxis(e, ctx, window->span, r.origin)
                     : glm::vec3(0.0f, 1.0f, 0.0f);
        // A hero lends its accent to an effect that did not state a colour of its own. "Did not
        // state" is the appearance still being the default white, which is what the Rainbow style
        // leaves it as -- so rainbow wins, as it should, because it overrides the hue outright.
        r.color = e.appearance.color;
        if (accent != nullptr && !e.appearance.rainbow && e.appearance.color == glm::vec3(1.0f)) {
            r.color = *accent;
        }
        out[written++] = r;
    }
    return written;
}

WorldEffectGpu packWorldEffect(const ResolvedEffect& r) {
    WorldEffectGpu g{};
    if (r.effect == nullptr) {
        return g;
    }
    const WorldEffect& e = *r.effect;
    const float w = std::max(e.appearance.width, kEps);
    g.originKind = glm::vec4(r.origin, static_cast<float>(e.propagation.kind));
    g.axisFront = glm::vec4(r.axis, r.frontDistance);
    g.shape = glm::vec4(e.propagation.frontWidth * w, e.propagation.trailLength * w, e.propagation.range,
                        e.propagation.falloff);
    g.vertical = glm::vec4(e.propagation.verticalExtent, e.propagation.verticalGrowth, e.propagation.ringCount,
                           e.propagation.beamRadius);
    // Envelope is folded into the radiance rather than carried as a separate multiply: the shader
    // would multiply it into three terms otherwise, and a fade is a fade. That frees `color.w`,
    // which carries the one remaining scalar with nowhere else to sit -- the distance at which
    // sparkle is faded out, which is the whole of its anti-aliasing.
    g.color = glm::vec4(r.color * e.appearance.intensity * r.envelope, std::max(e.sparkle.fadeDistance, 1.0f));
    g.edge = glm::vec4(e.appearance.edgeColor * e.appearance.edgeIntensity * r.envelope,
                       e.appearance.rainbow ? 1.0f : 0.0f);
    g.rainbow = glm::vec4(e.appearance.rainbowScale,
                          static_cast<float>(r.elapsed) * e.appearance.rainbowSpeed,
                          std::clamp(e.appearance.rainbowSaturation, 0.0f, 1.0f),
                          std::max(e.appearance.rainbowBrightness, 0.0f));
    const float density = e.sparkle.enabled ? std::max(e.sparkle.density, 0.0f) : 0.0f;
    g.sparkle = glm::vec4(density, std::clamp(e.sparkle.size, 0.0f, 1.0f), e.sparkle.intensity * r.envelope,
                          static_cast<float>(r.elapsed) * e.sparkle.speed + static_cast<float>(e.sparkle.seed) * 0.6180339887f);
    g.response = glm::vec4(e.response.ground, e.response.foliage, e.response.surface, e.response.emissive);
    return g;
}

void buildWorldEffectFrame(std::span<const WorldEffect> effects, const WorldEffectContext& ctx,
                           WorldEffectFrame& out) {
    std::array<ResolvedEffect, kMaxGpuWorldEffects> resolved{};
    const std::size_t count = resolveWorldEffects(effects, ctx, resolved);
    out.count = static_cast<std::uint32_t>(count);
    for (std::size_t i = 0; i < count; ++i) {
        out.effects[i] = packWorldEffect(resolved[i]);
    }
    for (std::size_t i = count; i < kMaxGpuWorldEffects; ++i) {
        out.effects[i] = WorldEffectGpu{};
    }
}

} // namespace avgen::world
