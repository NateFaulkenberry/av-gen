#include "world/atmospherics.hpp"

#include "world/effects/effect_registry.hpp"
#include "world/effects/effect_stack.hpp"

#include <glm/gtc/constants.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include "core/log.hpp"

#include <array>
#include <set>
#include <cmath>
#include <limits>
#include <string_view>
#include <unordered_set>
#include <utility>

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

glm::vec3 safeNormalize(const glm::vec3& v, const glm::vec3& fallback) {
    const float len = glm::length(v);
    return len > kEps ? v / len : fallback;
}

} // namespace

// ADR-500: exported, so the comet and the meteor shower share one arc rather than transliterating
// it twice. Declared in the header; the reasoning is there.
glm::vec3 directionFromSky(float azimuthDegrees, float elevationDegrees) {
    const float az = glm::radians(azimuthDegrees);
    const float el = glm::radians(std::clamp(elevationDegrees, -89.0f, 89.0f));
    const float horizontal = std::cos(el);
    return glm::vec3(horizontal * std::sin(az), std::sin(el), horizontal * std::cos(az));
}

namespace {

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

// ADR-500: both directions read the registry, so they cannot disagree with each other and cannot
// fall behind a kind. ADR-392 had to check that the round trip held, because the writer was a
// switch and the reader an if-chain that a new kind could be left out of; there is now one list.
const char* effectKindName(EffectKind k) {
    const EffectSchema* schema = effectSchema(k);
    // A kind with no schema. It has no name to write, and the fallback is deliberately NOT a
    // neighbour's: writing "comet" for it would save a file that silently loads as a comet, which
    // is the family's worst failure mode wearing a default's clothes. `checkRegistry` names it in
    // the CPU suite, and a file that says "unknown" refuses to load rather than lying.
    return schema != nullptr ? schema->key : "unknown";
}
std::optional<EffectKind> effectKindFromName(std::string_view name) {
    const EffectSchema* schema = effectSchema(name);
    if (schema == nullptr) {
        return std::nullopt;
    }
    return schema->kind;
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
    // ADR-562: the geometry half is validated through `field`, which is the same twenty-four
    // members it always was -- the struct composes `vortex::VortexField` rather than copying it,
    // so this list no longer has to be kept in step with a second one.
    if (!finite(field.center.x) || !finite(field.center.y) || !finite(field.center.z)) {
        return fail("the vortex centre is not finite");
    }
    for (const float f : {field.radius, field.thickness, field.swirl, field.rotationSpeed,
                          field.innerVoid, field.contrast, field.turbulence, field.turbulenceScale,
                          field.breathAmount, field.breathSpeed, field.funnelDepth, field.throat,
                          field.throatDensity, field.smokeWarp, field.smokeBillow, field.detail,
                          field.eyeWallWidth, field.eyeWallGain, field.bandArms,
                          field.bandPitchDegrees, field.bandDepth, field.bandHarmonic,
                          field.cloudNoise,
                          density, emission, filaments, spill, scattering, cometResponse,
                          cometReach}) {
        if (!finite(f)) { return fail("a vortex control is not finite"); }
    }
    if (field.radius < 0.0f) { return fail("the vortex radius may not be negative (0 is off)"); }
    if (field.thickness < 0.0f) { return fail("the vortex thickness may not be negative"); }
    if (field.funnelDepth < 0.0f) { return fail("the vortex funnel depth may not be negative"); }
    if (field.innerVoid < 0.0f || field.innerVoid > 1.0f) {
        return fail("the vortex inner void is 0..1");
    }
    if (field.throat < 0.0f || field.throat > 1.0f) {
        return fail("the vortex throat is 0..1 of the mouth");
    }
    return {};
}

Result<void> Tornado::validate() const {
    const tornado::TornadoField& f = field;
    if (!finite(f.base.x) || !finite(f.base.y) || !finite(f.base.z)) {
        return fail("the tornado base is not finite");
    }
    for (const float v : {f.height, f.radiusBottom, f.radiusMid, f.radiusTop, f.taper,
                          f.shellWidth, f.shellGain, f.coreRadius, f.coreDensity, f.edgeSoft,
                          f.wallCloudGain, f.cloudWidth, f.cloudHeight, f.cloudDensity, f.suctionCount, f.suctionStrength,
                          f.suctionRadius, f.suctionWidth, f.suctionSpeed,
                          f.cloudAmount, f.macroAmp, f.mesoAmp, f.microAmp, f.detailContrast,
                          f.detailScale, f.climbRate, f.erosion, f.touchdown, f.footSoft, f.skirtWidth, f.skirtHeight,
                          f.skirtDensity, f.skirtFlare, f.stripeCount, f.stripePitch,
                          f.stripeDepth, f.stripeHarmonic, f.circulation, f.coreRadiusMetres,
                          f.inflow, f.lift, f.rotationBottom, f.rotationTop, f.rotationCurve,
                          f.lean.x, f.lean.y, f.wobbleAmount, f.wobbleSpeed,
                          density, emission, scattering}) {
        if (!finite(v)) { return fail("a tornado control is not finite"); }
    }
    if (f.height < 0.0f) { return fail("the tornado height may not be negative (0 is off)"); }
    if (f.radiusBottom < 0.0f || f.radiusMid < 0.0f || f.radiusTop < 0.0f) {
        return fail("a tornado radius may not be negative");
    }
    if (f.touchdown < 0.0f || f.touchdown > 1.0f) { return fail("the tornado touchdown is 0..1"); }
    if (f.coreRadius < 0.0f || f.coreRadius > 1.0f) { return fail("the tornado core radius is 0..1"); }
    if (density < 0.0f) { return fail("the tornado density may not be negative"); }
    return {};
}


// ADR-702: `EffectInstance::validate`, its JSON and the whole-list check moved to
// `world/effects/effect_instance.cpp` and `effect_stack.cpp`: they are the instance's, not the sky's.

// ---- presets and factories (ADR-500) -------------------------------------------------------------
//
// The five hand-written per-kind lists ADR-392 counted are gone from this file. A style's body and
// a factory's body now live beside the rows they set, in `world/effects/kinds/<kind>_effect.cpp`,
// and what is left here is the lookup -- which is kind-agnostic, so the next kind gets presets and
// an "Add" button without a line being written here.
//
// The three name accessors and the three `apply...Style` functions are kept because about fifty
// call sites in `src/` and `tests/` name them. They are three-line adapters over one generic pair.

namespace {

std::span<const std::string_view> styleNamesOf(EffectKind kind) {
    // A `static` cache per kind rather than a rebuilt vector, because the callers are combos that
    // ask every frame and the answer is a compile-time constant. Built once, on first use.
    static std::vector<std::vector<std::string_view>> cache = [] {
        std::vector<std::vector<std::string_view>> out(kEffectKinds.size());
        for (const EffectSchema* schema : effectSchemas()) {
            const std::size_t index = effectKindIndex(schema->kind);
            if (index >= out.size()) {
                continue;
            }
            for (const EffectStyle& style : schema->styles) {
                out[index].emplace_back(style.name);
            }
        }
        return out;
    }();
    const std::size_t index = effectKindIndex(kind);
    if (index >= cache.size()) {
        return {};
    }
    return cache[index];
}

} // namespace

std::span<const std::string_view> effectStyleNames(EffectKind kind) { return styleNamesOf(kind); }

bool applyEffectStyle(EffectInstance& e, EffectKind kind, std::string_view style) {
    const EffectSchema* schema = effectSchema(kind);
    if (schema == nullptr) {
        return false;
    }
    for (const EffectStyle& s : schema->styles) {
        if (style == s.name && s.apply != nullptr) {
            s.apply(e);
            return true;
        }
    }
    // A style of another kind, or a name that no longer exists. False rather than a silent no-op on
    // the effect, which is what the three `apply...Style` functions promised and is what the UI
    // relies on to leave the combo where it was.
    return false;
}

EffectInstance makeEffect(EffectKind kind, std::string name) {
    const EffectSchema* schema = effectSchema(kind);
    EffectInstance e;
    if (schema == nullptr || schema->factory == nullptr) {
        e.name = std::move(name);
        e.kind = kind;
    } else {
        e = schema->factory(std::move(name));
    }
    // ADR-702: a ready-made instance is a VALID one, so it carries an id -- derived from its name,
    // which is unique on its own. A caller putting several into one list makes them unique with
    // `insertEffect` / `uniqueEffectId`, which is what the Add Effect menu does.
    if (e.id.empty()) {
        e.id = uniqueEffectId({}, e.name);
    }
    adaptEffectToOwner(e);
    return e;
}

std::span<const std::string_view> cometStyleNames() { return styleNamesOf(EffectKind::Comet); }
std::span<const std::string_view> auroraStyleNames() { return styleNamesOf(EffectKind::Aurora); }
std::span<const std::string_view> vortexStyleNames() { return styleNamesOf(EffectKind::Vortex); }

bool applyCometStyle(EffectInstance& e, std::string_view style) {
    return applyEffectStyle(e, EffectKind::Comet, style);
}
bool applyAuroraStyle(EffectInstance& e, std::string_view style) {
    return applyEffectStyle(e, EffectKind::Aurora, style);
}
bool applyVortexStyle(EffectInstance& e, std::string_view style) {
    return applyEffectStyle(e, EffectKind::Vortex, style);
}

EffectInstance bioluminescentComet(std::string name) {
    return makeEffect(EffectKind::Comet, std::move(name));
}
EffectInstance glowmereAurora(std::string name) {
    return makeEffect(EffectKind::Aurora, std::move(name));
}
EffectInstance cosmicVortex(std::string name) {
    return makeEffect(EffectKind::Vortex, std::move(name));
}

// ---- resolution --------------------------------------------------------------------------------

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

EffectFlow resolveEffectFlow(const EffectInstance& effect, const glm::vec3& anchor,
                             const EffectContext& ctx) {
    EffectFlow out;
    if (ctx.fieldBus == nullptr || !effect.flow.active()) {
        return out;
    }
    const fields::FieldHandle handle = ctx.fieldBus->resolve(effect.flow.field);
    if (handle == fields::kNoField) {
        // A name the scene does not publish. It answers as "no field" HERE, and is reported by name
        // elsewhere (`FieldBus::unresolved`, the engine's log, `effect_conformance`). Both halves
        // are needed and neither substitutes for the other: silently zero would be ADR-392's dead
        // route again, and refusing to render would make a disabled vortex break a comet.
        return out;
    }
    out.sample = ctx.fieldBus->sample(handle, anchor, static_cast<float>(ctx.seconds));
    out.influence = effect.flow.influence;
    return out;
}

float flowAmplitude(const fields::FlowSample& sample, float influence) {
    // `strength + gust`, not `strength * (1 + gust)`: a gust must be able to move something that
    // sits in a calm region, because a front arriving somewhere still is exactly what a gust is.
    // Floored at 0 so a subscriber can never be driven backwards through zero into an inversion;
    // `sanitise` already floors the influence, and this is the second line.
    const float excess = std::max(influence, 0.0f) * std::max(sample.strength + sample.gust, 0.0f);
    return 1.0f + excess;
}

float flowOffset(const fields::FlowSample& sample, float influence) {
    // The wind's spatial phase is `wavenumber * distance`, so at a flutter scale of 2.2 m and a
    // comet 4 km away it is in the thousands of radians. Wrapping here keeps the number a shader
    // adds to its own phase well-conditioned in float, and costs nothing: every consumer of it
    // takes a sine.
    const float raw = influence * sample.phase;
    const float wrapped = std::fmod(raw, fields::kFlowTau);
    return wrapped < 0.0f ? wrapped + fields::kFlowTau : wrapped;
}

glm::vec3 flowLean(const fields::FlowSample& sample, float influence) {
    if (influence == 0.0f) {
        return glm::vec3(0.0f);
    }
    // Horizontal only. A placed medium stands on the ground or hangs at an authored height; a
    // vertical component of the flow would move it off that height, which is a different question
    // from "which way is the air pushing it" and is not one anything asks.
    const glm::vec3 flat(sample.flow.x, 0.0f, sample.flow.z);
    const float len = glm::length(flat);
    if (len <= 1e-6f) {
        return glm::vec3(0.0f);
    }
    return (flat / len) * (std::min(len, 1.0f) * influence);
}

AtmosphericCounts resolveAtmosphericEffects(std::span<const EffectInstance> effects,
                                            const EffectContext& ctx,
                                            std::span<ResolvedAtmospheric> comets,
                                            std::span<ResolvedAtmospheric> auroras,
                                            std::span<ResolvedAtmospheric> vortices,
                                            std::span<const std::uint32_t> order,
                                            std::span<EffectStatus> status) {
    AtmosphericCounts counts;
    const std::size_t n = order.empty() ? effects.size() : order.size();
    for (std::size_t walk = 0; walk < n; ++walk) {
        const std::size_t at = order.empty() ? walk : order[walk];
        if (at >= effects.size()) {
            continue;
        }
        const EffectInstance& e = effects[at];
        const EffectSchema* schema = effectSchema(e.kind);
        // ADR-702: one list holds every type. The surface waves are `resolveWaves`' to evaluate.
        if (schema != nullptr && !isAtmosphericBucket(schema->resolve.bucket)) {
            continue; // ADR-702/703: owned by another stage's builder
        }
        // Written for every instance this function owns, so the panel can say what happened to
        // each one rather than a count of what happened to some. Refined below as it resolves.
        EffectStatus* said = at < status.size() ? &status[at] : nullptr;
        if (said != nullptr) {
            *said = e.enabled ? EffectStatus::Dormant : EffectStatus::Disabled;
        }
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

        ResolvedAtmospheric base;
        base.effect = &e;
        base.envelope = envelope;
        base.elapsed = pass;

        // ADR-500. The dispatch was `if comet / else if aurora / else vortex`, where the vortex was
        // the fall-through: a kind no arm named did not resolve as nothing, it resolved as a VORTEX
        // and was then dropped as "the second vortex in the scene". ADR-392 replaced that with an
        // exhaustive `switch` and a `claimed` flag, because `-Wswitch` alone is a line in a
        // five-thousand-line log while `AVGEN_WARNINGS_AS_ERRORS` is off (`CMakeLists.txt:33`).
        //
        // Both guards survive here and both got stronger.
        //
        //   * The *compile-time* guard is now on `EffectBucket` -- an exhaustive `switch` with no
        //     `default`, one arm per GPU payload the engine has. Adding a payload is a diagnostic;
        //     adding a KIND is not a change to this function at all, which is the whole point.
        //   * The *runtime* guard is `schema == nullptr || fill == nullptr`: a kind with no
        //     declaration is counted in `dropped` and drawn by nobody, so it is reportable rather
        //     than silently attributed to a neighbour. `checkRegistry` has already named it in the
        //     CPU suite, which is where the news belongs.
        if (schema == nullptr || schema->resolve.fill == nullptr) {
            ++counts.dropped;
            if (said != nullptr) {
                *said = EffectStatus::Dropped;
            }
            continue;
        }
        bool drew = false;
        bool lost = false;
        // How many records this one effect contributes. One for everything ADR-230 shipped; a
        // meteor shower is the first kind for which it is more than one.
        const std::size_t records = schema->resolve.count != nullptr ? schema->resolve.count(e) : 1;
        for (std::size_t index = 0; index < records; ++index) {
            ResolvedAtmospheric r;
            switch (schema->resolve.bucket) {
            case EffectBucket::Comet: {
                if (counts.comets >= comets.size()) {
                    ++counts.dropped;
                    lost = true;
                    continue;
                }
                if (!schema->resolve.fill(e, index, ctx, base, r)) {
                    continue; // a degenerate arc, or a meteor that has not launched yet
                }
                comets[counts.comets++] = r;
                drew = true;
                break;
            }
            case EffectBucket::Aurora: {
                if (counts.auroras >= auroras.size()) {
                    ++counts.dropped;
                    lost = true;
                    continue;
                }
                if (!schema->resolve.fill(e, index, ctx, base, r)) {
                    continue;
                }
                auroras[counts.auroras++] = r;
                drew = true;
                break;
            }
            case EffectBucket::Medium: {
                // ADR-562: `kMaxMedia` slots, not one. The literal `1` that used to be here is the
                // whole of ADR-560's headline defect -- a fog bank and a cosmic vortex authored
                // together rendered byte-identical to whichever came FIRST in the array, with the
                // loser contributing not one pixel and nothing saying so. `agent/tornado`'s
                // seven-variant showcase rendered a flat grey frame for the same reason.
                //
                // The number is still a budget and dropping still happens past it -- ADR-374's
                // +5.5 ms is real and the per-slot ray interval is what pays for more. What changed
                // is that the budget is four rather than one, and that going over it is now SAID
                // (`mediaDropped` on the frame) rather than counted into a field nobody read.
                // The cap is `kMaxMedia`, NOT `vortices.size()`, and the distinction is a defect
                // the suite caught: `vortices` is a defaulted `{}` for callers that want the counts
                // and not the records, so testing the SPAN made an empty span mean "no slots" and
                // dropped every medium in the scene. Counting and storing are independent here and
                // always were -- the original guarded the store separately for the same reason.
                if (counts.vortices >= kMaxMedia) {
                    ++counts.dropped;
                    lost = true;
                    continue;
                }
                if (!schema->resolve.fill(e, index, ctx, base, r)) {
                    continue;
                }
                if (counts.vortices < vortices.size()) {
                    vortices[counts.vortices] = r;
                }
                ++counts.vortices;
                drew = true;
                break;
            }
            case EffectBucket::Surface:
            case EffectBucket::EntityLanes:
            case EffectBucket::Ribbon:
            case EffectBucket::Distortion:
            case EffectBucket::Emitter:
                break; // unreachable: skipped at the top; each has a builder of its own
            }
        }
        if (said != nullptr) {
            // Any record lost is reported as a drop: a shower that drew four of its six trails is
            // not what was authored, and "drawn" would hide exactly the case this report exists for.
            *said = lost ? EffectStatus::Dropped : (drew ? EffectStatus::Drawn : EffectStatus::Dormant);
        }
    }
    return counts;
}

CometGpu packComet(const ResolvedAtmospheric& r) {
    CometGpu g{};
    // ADR-500: the test is the BUCKET, not the kind. A meteor shower's streaks are comets in every
    // way the shader cares about -- a head on an arc with a trail integrated along the view ray --
    // and a kind test here would have made "reuse the integrator you already have" impossible,
    // which is most of what makes an effect one file.
    const EffectSchema* schema = r.effect != nullptr ? effectSchema(r.effect->kind) : nullptr;
    if (schema == nullptr || schema->resolve.bucket != EffectBucket::Comet) {
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
    // §68. The subscribed field reaches the comet through the two things its trail already has: how
    // far the wisps are thrown sideways, and the phase they are thrown at.
    //
    // It is deliberately NOT the trajectory. A comet is a body on a ballistic arc four kilometres
    // up; air that bends its path is a comet nobody would recognise, and ADR-230 made the arc's
    // endpoints exactly what an artist typed for the reason that a control which is a suggestion is
    // not a control. What the medium moves is the TRAIL -- which is what a comet's tail is made of,
    // and which is the part that should know there is weather.
    const float amp = flowAmplitude(r.flow, r.flowInfluence);
    g.shape = glm::vec4(a.tailWidth, a.wispAmount * amp, a.wispScale,
                        static_cast<float>(r.elapsed) * a.flowSpeed + flowOffset(r.flow, r.flowInfluence));
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
    const EffectSchema* schema = r.effect != nullptr ? effectSchema(r.effect->kind) : nullptr;
    if (schema == nullptr || schema->resolve.bucket != EffectBucket::Aurora) {
        return g;
    }
    const Aurora& au = r.effect->aurora;
    const AuroraShape& s = au.shape;
    const AuroraAppearance& a = au.appearance;
    const AuroraAudio& ad = au.audio;

    g.config = glm::vec4(std::clamp(s.curtainCount, 1.0f, 5.0f), s.radius, s.baseHeight, s.curtainHeight);
    // §68, the same two levers the comet's trail gets and for the same reason: the curtain's
    // undulation is how hard the air is pushing it, and the fold phase is where it is standing.
    // The height and the colours are left alone -- those answer to the music (ADR-230 §4.2), and an
    // aurora that dimmed in a gust would be answering two masters with one number.
    const float amp = flowAmplitude(r.flow, r.flowInfluence);
    const float offset = flowOffset(r.flow, r.flowInfluence);
    g.shape = glm::vec4(s.waveAmplitude * amp, s.waveScale, s.turbulence, s.complexity);
    g.flow = glm::vec4(static_cast<float>(r.elapsed) * s.flowSpeed,
                       static_cast<float>(r.elapsed) * s.driftSpeed + offset,
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
// ADR-566: pack + tag, in ONE place, because they are one operation and two callers need it.
//
// `buildAtmosphericFrame` is the shipping caller; a test that wants the exact bytes the march
// reads is the other, and before this function existed the only way to get them was to call
// `pack` and then write the tag by hand -- a second copy of the ordering rule that ADR-565's
// sentinel exists to enforce. ADR-554's rule: a seam published from two places must be written by
// both, so this one is published from one.
void packMediumSlot(const EffectInstance& e, float envelope, MediumSlot& slot,
                    const MediumFlowInput& flow) {
    const EffectSchema* schema = effectSchema(e.kind);
    if (schema == nullptr || schema->resolve.pack == nullptr) {
        return;
    }
    // ADR-565: the tag's lane is RESERVED, and this makes that a constraint a packer cannot
    // violate silently rather than a sentence in a comment.
    //
    // `agent/tornado` found the hazard by walking into it: its block is sixteen lanes, it had
    // sixty floats and wanted sixty-one, and the overflow landed exactly on lane 15. Because
    // the tag is written AFTER `pack` below, the tag itself always wins -- so the dispatch
    // keeps working and the PACKER's value disappears. Silent, and it presents as a wrong
    // appearance rather than as a wrong shape, which is the harder thing to trace.
    //
    // A sentinel written before the call and checked after costs two stores per medium per
    // frame and turns a silent loss into a named one.
    constexpr float kReserved = -987654.0f;
    slot.lane[kMediumLanes - 1] = glm::vec4(kReserved);
    schema->resolve.pack(e, envelope, flow, slot);
    if (slot.lane[kMediumLanes - 1] != glm::vec4(kReserved)) {
        static std::set<EffectKind> warned;
        if (warned.insert(e.kind).second) {
            log::warn("medium kind '{}' writes lane {} in its packer, which is reserved for the "
                      "kind tag: that value is discarded and the effect will look wrong",
                      effectKindName(e.kind), kMediumLanes - 1);
        }
    }
    slot.kind = static_cast<std::uint32_t>(e.kind);
    // ADR-562: and the kind reaches the SHADER, in the last lane.
    //
    // It did not, for a day. `MediumSlot::kind` was set here, compared by `frameDiffers` and
    // then dropped on the floor by the renderer, which uploaded only the lanes -- so the march
    // had no way to tell a fog bank from a tornado and every kind got `vortexShapeAt`. The slot
    // contract said "kind tag" and the tag was unreachable: this branch's own defect family,
    // one day old, in the foundation written to fix it.
    //
    // Set HERE rather than in each kind's `packMedium`, so a new kind cannot forget to. The
    // lane is the last one precisely because it is the one no kind's parameters will reach
    // first -- the vortex fills 0-12 and the tornado 0-13.
    slot.lane[kMediumLanes - 1].x = static_cast<float>(slot.kind);
}


void buildAtmosphericFrame(std::span<const EffectInstance> effects, const EffectContext& ctx,
                           AtmosphericFrame& out, std::span<const std::uint32_t> order,
                           std::span<EffectStatus> status) {
    std::array<ResolvedAtmospheric, kMaxGpuComets> comets{};
    std::array<ResolvedAtmospheric, kMaxGpuAuroras> auroras{};
    std::array<ResolvedAtmospheric, kMaxMedia> vortices{};
    const AtmosphericCounts counts = resolveAtmosphericEffects(effects, ctx, comets, auroras, vortices, order, status);

    out.cometCount = static_cast<std::uint32_t>(counts.comets);
    out.auroraCount = static_cast<std::uint32_t>(counts.auroras);

    // ADR-562: every live placed medium, packed into lanes. It is taken from the RESOLVE, and that
    // was a fix rather than a tidy-up when ADR-387 made it so: this used to walk `effects` a second
    // time testing `e.enabled && e.vortex.active()`, under a comment claiming the activation window
    // and lifetime envelope "were already applied by the resolve above". They were not --
    // `resolveAtmosphericEffects` takes a `span<const>` and cannot write back, and the second walk
    // consulted `counts` not at all, so a medium with `activation: window` ignored its window.
    // ADR-385's stated reason that stopped anyone checking.
    //
    // What ADR-562 changes is the count: up to `kMaxMedia`, each through its own kind's `pack`.
    out.mediumCount = 0;
    // Reset here: this is the frame's count, and it is accumulated below. Without the reset a frame
    // block that is rebuilt in place (the engine's live scene is) carried every earlier frame's
    // drops forward, so one drop made every later frame report one.
    out.mediaDropped = 0;
    for (auto& slot : out.media) {
        slot = MediumSlot{};
    }
    for (std::size_t i = 0; i < counts.vortices && out.mediumCount < kMaxMedia; ++i) {
        const ResolvedAtmospheric& rv = vortices[i];
        if (rv.effect == nullptr) {
            continue;
        }
        const EffectSchema* schema = effectSchema(rv.effect->kind);
        if (schema == nullptr || schema->resolve.pack == nullptr) {
            // A medium kind with no packer reaches the march as nothing, so it is counted rather
            // than attributed to a neighbour -- the runtime half of ADR-500's guard, in the one
            // place a missing declaration would otherwise be invisible.
            ++out.mediaDropped;
            continue;
        }
        // §68 / ADR-572 (§17). A placed medium answers the wind it subscribes to -- and HOW it
        // answers is the KIND's business, not this loop's, so this loop does not answer it at all.
        //
        // This used to be two stages. It wrote `leaned.vortex.field.center` unconditionally, which
        // is right for a vortex and does nothing at all for any other kind -- ADR-580's Tornado
        // found that the moment it arrived: `effect_conformance`'s `flow-reaches` check reported
        // that subscribing one to a gale changed not a byte of the frame this builds. ADR-580 §68
        // fixed it with a per-kind `lean` hook here; ADR-572 §17 independently gave the PACKER the
        // resolved flow. The merge had both, which is two channels for one question, so the hook
        // was deleted and each kind's wind response moved into its own packer (see
        // `EffectResolve::pack`). A vortex and a fog bank still lean by moving; a tornado bends.
        //
        // What is left here is one call with the flow attached, which is the whole of it.
        packMediumSlot(*rv.effect, rv.envelope, out.media[out.mediumCount],
                       MediumFlowInput{rv.flow, rv.flowInfluence});
        ++out.mediumCount;
    }
    // Everything the resolve could not seat. ADR-560: this number had one reader in the whole tree
    // and it was a CPU conformance finding, so in a running editor it did not exist.
    out.mediaDropped += static_cast<std::uint32_t>(counts.dropped);

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
        const EffectInstance& e = *auroras[i].effect;
        const float scale = groundGlowScale(e.ground.mode) * e.ground.intensity * auroras[i].envelope;
        if (scale > 0.0f) {
            ambient += e.ground.color * scale;
        }
    }
    float brightest = 0.0f;
    for (std::size_t i = 0; i < counts.comets; ++i) {
        const EffectInstance& e = *comets[i].effect;
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
