#include "world/wave_effect.hpp"

#include "world/effects/effect_instance.hpp"
#include "world/effects/effect_registry.hpp"

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
    case SourceKind::Owner: return "owner";
    }
    return "world";
}
std::optional<SourceKind> sourceKindFromName(std::string_view name) {
    if (name == "world" || name == "position") { return SourceKind::World; }
    if (name == "node" || name == "entity") { return SourceKind::Node; }
    if (name == "hero") { return SourceKind::Hero; }
    if (name == "camera" || name == "active_camera" || name == "activeCamera") { return SourceKind::Camera; }
    if (name == "focusHero" || name == "focus_hero" || name == "spotlight") { return SourceKind::FocusHero; }
    if (name == "owner") { return SourceKind::Owner; }
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

Result<void> MaterialResponse::validate() const {
    for (const float v : {ground, foliage, surface, emissive}) {
        if (!finite(v) || v < 0.0f) {
            return fail("a material response weight must be finite and not negative");
        }
    }
    return {};
}

Result<void> WaveEffect::validate() const {
    if (auto ok = source.validate(); !ok) { return fail("source: {}", ok.error().message); }
    if (hasTarget) {
        if (auto ok = target.validate(); !ok) { return fail("target: {}", ok.error().message); }
    }
    if (auto ok = propagation.validate(); !ok) { return ok; }
    if (auto ok = appearance.validate(); !ok) { return ok; }
    if (auto ok = sparkle.validate(); !ok) { return ok; }
    if (auto ok = response.validate(); !ok) { return ok; }
    const bool needsTarget = propagation.direction == DirectionMode::SourceToTarget ||
                             propagation.direction == DirectionMode::CameraToTarget;
    if (needsTarget && !hasTarget) {
        return fail("direction '{}' needs a target", directionModeName(propagation.direction));
    }
    return {};
}

// ---- JSON --------------------------------------------------------------------------------------
//
// ADR-702: only the endpoints. Every number on a wave is a registry row now and is written by the
// registry's walk; what is left is the part that is a kind and a NAME rather than a number.

json waveEndpointToJson(const EffectEndpoint& e) {
    json j;
    j["kind"] = sourceKindName(e.kind);
    if (!e.name.empty()) { j["name"] = e.name; }
    if (e.kind == SourceKind::World || e.position != glm::vec3(0.0f)) { j["position"] = vec3ToJson(e.position); }
    if (e.groundOffset != 0.0f) { j["groundOffset"] = e.groundOffset; }
    return j;
}

Result<EffectEndpoint> waveEndpointFromJson(const json& j) {
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


// ---- presets -----------------------------------------------------------------------------------

namespace {

constexpr std::array<std::string_view, 5> kBeamStyles{"Bioluminescent", "Rainbow", "Energy", "Magical", "Subtle"};
constexpr std::array<std::string_view, 4> kPulseStyles{"Water", "Bioluminescent", "Shockwave", "Magical"};

} // namespace

std::span<const std::string_view> beamStyleNames() { return kBeamStyles; }
std::span<const std::string_view> pulseStyleNames() { return kPulseStyles; }

bool applyBeamStyle(WaveEffect& e, std::string_view style) {
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
    return true;
}

bool applyPulseStyle(WaveEffect& e, std::string_view style) {
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
    return true;
}

// ---- resolution --------------------------------------------------------------------------------

namespace {

using Window = ActivationWindow;

// The name an endpoint rides when it is an entity by name: a `Hero` or `Node` endpoint's own
// name, or -- for `Owner` -- the entity the instance is attached to.
std::string_view endpointSubject(const EffectInstance& e, const EffectEndpoint& p) {
    if (p.kind == SourceKind::Owner) {
        return e.owner.kind == EffectTarget::Entity ? std::string_view(e.owner.name) : std::string_view();
    }
    return p.name;
}

std::optional<Window> activeWindow(const EffectInstance& e, const EffectContext& ctx) {
    // A `FocusHero` source follows whatever the cut is on. Anything else fires for its OWN subject
    // only -- which for an entity-owned pulse is the entity it is attached to, and is what makes
    // sixteen per-hero pulses behave, together, exactly like ADR-207's one pulse that followed focus.
    return resolveActivationWindow(e.activation, e.timing, ctx, e.wave.source.kind == SourceKind::FocusHero,
                                   endpointSubject(e, e.wave.source),
                                   e.owner.kind == EffectTarget::Entity ? std::string_view(e.owner.name)
                                                                        : std::string_view());
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
glm::vec3 heroStandsAt(const HeroPoint& hero, const EffectContext& ctx) {
    glm::vec3 at{0.0f};
    if (ctx.scene != nullptr && ctx.scene->nodePosition(hero.name, at)) {
        return at;
    }
    return hero.position;
}

// Where an endpoint is. `accent` receives the hero's own colour when the endpoint resolved to one,
// so an effect can be the colour of the thing it is about without anybody typing it twice.
bool resolveEndpoint(const EffectInstance& owner, const EffectEndpoint& e, const EffectContext& ctx,
                     const ShotSpan* span, glm::vec3& out, const glm::vec3** accent) {
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
    case SourceKind::Owner: {
        // ADR-702: wherever the instance is attached. An entity owner resolves as a hero of that
        // name when there is one (and lends its accent, as a named hero source always has), else
        // as a node; a camera owner is the camera. The World and a light have no position here, so
        // the effect is inactive rather than at the origin.
        switch (owner.owner.kind) {
        case EffectTarget::Entity: {
            if (const HeroPoint* hero = findHero(ctx.heroes, owner.owner.name)) {
                out = heroStandsAt(*hero, ctx);
                if (accent != nullptr) { *accent = &hero->colorAccent; }
            } else if (ctx.scene == nullptr || !ctx.scene->nodePosition(owner.owner.name, out)) {
                return false;
            }
            break;
        }
        case EffectTarget::Camera: out = ctx.cameraPosition; break;
        case EffectTarget::World:
        case EffectTarget::Light: return false;
        }
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
bool resolveTravelTarget(const EffectInstance& instance, const EffectContext& ctx, const ShotSpan* span,
                         glm::vec3& out) {
    const WaveEffect& e = instance.wave;
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
    return resolveEndpoint(instance, e.target, ctx, span, out, nullptr);
}

glm::vec3 resolveAxis(const EffectInstance& instance, const EffectContext& ctx, const ShotSpan* span,
                      const glm::vec3& origin) {
    const WaveEffect& e = instance.wave;
    const glm::vec3 fallback = safeNormalize(e.propagation.explicitDirection, glm::vec3(0.0f, 0.0f, -1.0f));
    const auto toTarget = [&](const glm::vec3& from) -> std::optional<glm::vec3> {
        glm::vec3 target{0.0f};
        if (!resolveTravelTarget(instance, ctx, span, target)) {
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

bool isWave(const EffectInstance& e) {
    const EffectSchema* schema = effectSchema(e.kind);
    return schema != nullptr && schema->resolve.bucket == EffectBucket::Surface;
}

} // namespace

std::optional<ResolvedWave> resolveWave(const EffectInstance& instance, const EffectContext& ctx) {
    if (!instance.enabled || !isWave(instance)) {
        return std::nullopt;
    }
    const WaveEffect& e = instance.wave;
    const auto window = activeWindow(instance, ctx);
    if (!window) {
        return std::nullopt;
    }
    // Time since the activation opened, minus the delay. Everything below is a function of this
    // and of the authored numbers: no state, no history, no frame counter.
    const Timing& timing = instance.timing;
    const double local = ctx.seconds - window->start - timing.delay;
    if (local < 0.0) {
        return std::nullopt;
    }
    const double windowLength = window->end - window->start - timing.delay;
    const double lifetime = timing.lifetime > 0.0 ? timing.lifetime : windowLength;
    if (std::isfinite(lifetime) && local >= lifetime) {
        return std::nullopt;
    }
    float envelope = 1.0f;
    if (timing.fadeIn > 0.0) {
        envelope *= smoothRamp(static_cast<float>(local), static_cast<float>(timing.fadeIn));
    }
    if (timing.fadeOut > 0.0 && std::isfinite(lifetime)) {
        const double remaining = lifetime - local;
        envelope *= smoothRamp(static_cast<float>(remaining), static_cast<float>(timing.fadeOut));
    }
    if (envelope <= 1e-4f) {
        return std::nullopt;
    }

    // Where the front is. A repeating effect restarts its front every `repeatSeconds`, which is
    // what makes a pulse a pulse; `fmod` rather than a counter, so scrubbing lands the same ring.
    const double pass = timing.repeatSeconds > 0.0 ? std::fmod(local, timing.repeatSeconds) : local;
    const float front = e.propagation.startOffset + e.propagation.speed * static_cast<float>(pass);
    if (front > e.propagation.range + e.propagation.trailLength * e.appearance.width) {
        return std::nullopt; // this pass is over the horizon and the next one has not started
    }

    ResolvedWave r;
    r.effect = &instance;
    r.envelope = envelope;
    r.frontDistance = front;
    r.elapsed = local;
    const glm::vec3* accent = nullptr;
    if (!resolveEndpoint(instance, e.source, ctx, window->span, r.origin, &accent)) {
        return std::nullopt; // a FocusHero source with nothing spotlit is not an error, it is inactive
    }
    r.axis = e.propagation.kind == PropagationKind::DirectionalWave
                 ? resolveAxis(instance, ctx, window->span, r.origin)
                 : glm::vec3(0.0f, 1.0f, 0.0f);
    // A hero lends its accent to an effect that did not state a colour of its own. "Did not
    // state" is the appearance still being the default white, which is what the Rainbow style
    // leaves it as -- so rainbow wins, as it should, because it overrides the hue outright.
    r.color = e.appearance.color;
    if (accent != nullptr && !e.appearance.rainbow && e.appearance.color == glm::vec3(1.0f)) {
        r.color = *accent;
    }
    return r;
}

std::size_t resolveWaves(std::span<const EffectInstance> effects, const EffectContext& ctx,
                         std::span<ResolvedWave> out, std::span<const std::uint32_t> order,
                         std::span<EffectStatus> status) {
    std::size_t written = 0;
    const std::size_t n = order.empty() ? effects.size() : order.size();
    for (std::size_t walk = 0; walk < n; ++walk) {
        const std::size_t at = order.empty() ? walk : order[walk];
        if (at >= effects.size() || !isWave(effects[at])) {
            continue;
        }
        const EffectInstance& e = effects[at];
        EffectStatus said = e.enabled ? EffectStatus::Dormant : EffectStatus::Disabled;
        if (const auto r = resolveWave(e, ctx)) {
            // ADR-702: the ninth live wave used to be dropped by a bare `break`, and the header
            // above said it was "dropped with a warning". It is now dropped AND reported, per
            // instance, where the Effects panel draws it.
            if (written < out.size()) {
                out[written++] = *r;
                said = EffectStatus::Drawn;
            } else {
                said = EffectStatus::Dropped;
            }
        }
        if (at < status.size()) {
            status[at] = said;
        }
    }
    return written;
}

WaveGpu packWave(const ResolvedWave& r) {
    WaveGpu g{};
    if (r.effect == nullptr) {
        return g;
    }
    const WaveEffect& e = r.effect->wave;
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

void buildWaveFrame(std::span<const EffectInstance> effects, const EffectContext& ctx, WaveFrame& out,
                    std::span<const std::uint32_t> order, std::span<EffectStatus> status) {
    std::array<ResolvedWave, kMaxGpuWaves> resolved{};
    const std::size_t count = resolveWaves(effects, ctx, resolved, order, status);
    out.count = static_cast<std::uint32_t>(count);
    for (std::size_t i = 0; i < count; ++i) {
        out.effects[i] = packWave(resolved[i]);
    }
    for (std::size_t i = count; i < kMaxGpuWaves; ++i) {
        out.effects[i] = WaveGpu{};
    }
}

} // namespace avgen::world
