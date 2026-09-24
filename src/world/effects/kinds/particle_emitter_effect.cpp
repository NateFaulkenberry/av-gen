// Particle Emitter (ADR-703, EMIT): an effect-owned particle system riding its owner.
//
// One type, many looks. `look` picks a base configuration of the engine's existing particle system
// -- the part that makes fireflies blink in loose synchrony, embers rise and cool through a
// blackbody ramp, magic motes orbit their caster with short trails -- and the rows are the controls
// an artist reaches for on top of it. The looks are configurations, not code paths: everything a
// look sets is an ordinary `scene::ParticleSystem` field (docs/design/effect-library/
// catalog-particles.md, "nine of ten are presets"). The presets in the Add Effect menu are styles
// that set `look` and the rows together.

#include "world/effects/effect_registry.hpp"
#include "world/effects/particle_emitter.hpp"

#include <glm/gtc/matrix_access.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace avgen::world {
namespace {

using E = EffectInstance;

constexpr const char* kLooks[] = {"fireflies", "embers", "magic"};
enum class Look : int { Fireflies = 0, Embers = 1, Magic = 2 };

constexpr EffectField kFields[] = {
    storedChoice("look", "Look", 0, kLooks).main()
        .tooltip("The kind of particle: blinking fireflies, rising embers, or orbiting magic motes.\n"
                 "Each is a base configuration the rows below adjust."),
    storedFloat("rate", "Rate", 60.0f, 0.0f, 20000.0f, 0.0f, 2000.0f).main().fmt("%.0f /s")
        .tooltip("Particles emitted per second. Route audio onto it for a swarm that swells."),
    storedFloat("brightness", "Brightness", 8.0f, 0.0f, 40.0f, 0.0f, 16.0f).main()
        .tooltip("HDR emission. Above ~1 the particles feed the bloom."),
    storedFloat("size", "Size", 0.06f, 0.002f, 2.0f, 0.01f, 0.3f).main().fmt("%.3f m").log(),
    storedColor("color", "Colour", glm::vec3(1.0f)).main()
        .tooltip("Tints the look's own colours (fireflies' green-gold, embers' blackbody ramp)."),
    storedFloat("radius", "Spawn radius", 3.0f, 0.05f, 500.0f, 0.2f, 40.0f).main().fmt("%.1f m").log()
        .tooltip("The region around the owner particles are born in."),
    storedFloat("life", "Particle life", 5.0f, 0.1f, 30.0f, 0.5f, 10.0f).fmt("%.1f s").sec("Motion"),
    storedFloat("speed", "Speed", 0.3f, 0.0f, 40.0f, 0.0f, 6.0f).fmt("%.2f m/s"),
    storedFloat("rise", "Rise", 0.0f, -20.0f, 20.0f, -3.0f, 6.0f).fmt("%.2f m/s2")
        .tooltip("Buoyancy: positive rises (embers), negative falls."),
    storedFloat("turbulence", "Turbulence", 0.6f, 0.0f, 8.0f, 0.0f, 3.0f)
        .tooltip("Curl-noise wander. Coherent, not jitter: particles swirl, they do not shake."),
    storedFloat("wind", "Follows wind", 0.2f, 0.0f, 2.0f, 0.0f, 1.0f),
    storedFloat("orbit", "Orbit", 0.0f, -20.0f, 20.0f, -6.0f, 6.0f).sec("Swirl")
        .tooltip("Tangential pull around the owner. Magic motes circle their caster."),
    storedFloat("pulseRate", "Blink rate", 0.6f, 0.0f, 8.0f, 0.0f, 3.0f).fmt("%.2f Hz").sec("Blink"),
    storedFloat("pulseDepth", "Blink depth", 1.0f, 0.0f, 1.0f, 0.0f, 1.0f),
    storedFloat("pulseSync", "Blink sync", 0.2f, 0.0f, 1.0f, 0.0f, 1.0f)
        .tooltip("0 blinks independently; 1 blinks together. Route the beat onto it and the swarm\n"
                 "falls into time with the music."),
    storedBool("trail", "Trails", false),
    storedFloat("burst", "Burst", 0.0f, 0.0f, 10000.0f, 0.0f, 500.0f).fmt("%.0f").sec("Events")
        .tooltip("Extra particles emitted this frame. A route target, not a setting: route onsets\n"
                 "or the beat onto it for a pop on every hit."),
    storedFloat("centerX", "World X", 0.0f, -10000.0f, 10000.0f, -500.0f, 500.0f).sec("Placement (World owner)"),
    storedFloat("centerY", "World Y", 2.0f, -10000.0f, 10000.0f, -100.0f, 200.0f),
    storedFloat("centerZ", "World Z", 0.0f, -10000.0f, 10000.0f, -500.0f, 500.0f),
};

// ---- rows, read without allocating ---------------------------------------------------------------

struct Rows {
    // Resolved once: the field and its store key, so a frame never builds a key string.
    struct Row {
        const EffectField* field = nullptr;
        std::string key;
    };
    std::array<Row, std::size(kFields)> rows;
    Rows() {
        for (std::size_t i = 0; i < std::size(kFields); ++i) {
            rows[i].field = &kFields[i];
            rows[i].key = std::string("particleEmitter/") + kFields[i].leaf;
        }
    }
    [[nodiscard]] const Row& at(std::string_view leaf) const {
        for (const Row& r : rows) {
            if (leaf == r.field->leaf) {
                return r;
            }
        }
        return rows[0];
    }
    [[nodiscard]] float f(const E& e, std::string_view leaf) const {
        const Row& r = at(leaf);
        return e.values.getFloat(r.key, r.field->storedDefault);
    }
    [[nodiscard]] glm::vec3 c(const E& e, std::string_view leaf) const {
        const Row& r = at(leaf);
        return e.values.getColor(r.key, r.field->storedColor);
    }
    [[nodiscard]] bool b(const E& e, std::string_view leaf) const {
        const Row& r = at(leaf);
        return e.values.getBool(r.key, r.field->storedDefault >= 0.5f);
    }
    void set(E& e, std::string_view leaf, float v) const { e.values.setFloat(at(leaf).key, v); }
    void setColor(E& e, std::string_view leaf, glm::vec3 v) const { e.values.setColor(at(leaf).key, v); }
    void setBool(E& e, std::string_view leaf, bool v) const { e.values.setBool(at(leaf).key, v); }
};

const Rows& rows() {
    static const Rows kRows;
    return kRows;
}

// ---- the looks: base configurations of the existing system ---------------------------------------

void baseFireflies(scene::ParticleSystem& s) {
    s.capacity = 4096;
    s.shape = scene::EmitterShape::Sphere;
    s.spread = 1.0f;
    s.gravity = glm::vec3(0.0f);
    s.drag = 1.2f;
    s.turbulenceScale = 0.35f;
    s.turbulenceSpeed = 0.15f;
    s.sizeEnd = -1.0f; // marker: same as start (set below)
    s.colorStart = glm::vec4(0.78f, 1.0f, 0.35f, 1.0f);
    s.colorEnd = glm::vec4(0.55f, 0.95f, 0.25f, 0.0f);
    s.blend = scene::ParticleBlend::Additive;
    s.pulseSharpness = 3.0f;
    s.pauseRate = 0.3f;
    s.pauseFraction = 0.4f;
    s.softness = 0.5f;
    s.fogCoupling = 1.0f;
    s.velocityStretch = 0.0f;
}

void baseEmbers(scene::ParticleSystem& s) {
    s.capacity = 8192;
    s.shape = scene::EmitterShape::Disc;
    s.direction = glm::vec3(0.0f, 1.0f, 0.0f);
    s.spread = 0.25f;
    s.drag = 0.4f;
    s.turbulenceScale = 0.8f;
    s.turbulenceSpeed = 0.6f;
    s.sizeEnd = 0.25f; // marker: a fraction of start (set below)
    // Blackbody over life: white-hot, orange, red, out. The ramp is what makes an ember read as one
    // cooling rather than as an orange dot fading.
    s.colorCurve.keys = {{0.0f, {1.0f, 0.86f, 0.55f}},
                         {0.25f, {1.0f, 0.48f, 0.12f}},
                         {0.65f, {0.72f, 0.12f, 0.02f}},
                         {1.0f, {0.18f, 0.02f, 0.0f}}};
    s.opacityCurve.keys = {{0.0f, 1.0f}, {0.7f, 0.9f}, {1.0f, 0.0f}};
    s.colorStart = glm::vec4(1.0f);
    s.colorEnd = glm::vec4(1.0f, 1.0f, 1.0f, 0.0f);
    s.blend = scene::ParticleBlend::Additive;
    s.velocityStretch = 0.5f;
    s.stretchMax = 0.2f;
    s.softness = 0.3f;
    s.fogCoupling = 1.0f;
}

void baseMagic(scene::ParticleSystem& s) {
    s.capacity = 8192;
    s.shape = scene::EmitterShape::Sphere;
    s.spread = 1.0f;
    s.gravity = glm::vec3(0.0f);
    s.drag = 0.8f;
    s.turbulenceScale = 0.9f;
    s.turbulenceSpeed = 0.4f;
    s.attractorStrength = 1.5f;
    s.sizeEnd = 0.0f;
    s.colorCurve.keys = {{0.0f, {0.35f, 0.95f, 1.0f}},
                         {0.5f, {0.95f, 0.35f, 1.0f}},
                         {1.0f, {1.0f, 0.82f, 0.35f}}};
    s.opacityCurve.keys = {{0.0f, 0.0f}, {0.1f, 1.0f}, {1.0f, 0.0f}};
    s.colorStart = glm::vec4(1.0f);
    s.colorEnd = glm::vec4(1.0f, 1.0f, 1.0f, 0.0f);
    s.blend = scene::ParticleBlend::Additive;
    s.trailLength = 10;
    s.trailTaper = 0.0f;
    s.trailFade = 0.0f;
    s.softness = 0.4f;
    s.fogCoupling = 1.0f;
}

// A stable 32-bit seed from the instance id, so two instances never share a stream and one instance
// has the same stream in every run (FNV-1a).
std::uint32_t seedOf(std::string_view id) {
    std::uint32_t h = 2166136261u;
    for (const char c : id) {
        h = (h ^ static_cast<std::uint8_t>(c)) * 16777619u;
    }
    return h == 0 ? 1u : h;
}

// ---- styles: a look and its rows, together ---------------------------------------------------------

struct Preset {
    const char* name;
    Look look;
    float rate, brightness, size, radius, lifetime, speed, rise, turbulence, wind, orbit, pulseRate,
        pulseDepth, pulseSync;
    glm::vec3 color;
    bool trail;
};

constexpr Preset kPresets[] = {
    {"Meadow Fireflies", Look::Fireflies, 60, 8, 0.06f, 6, 6, 0.25f, 0, 0.6f, 0.2f, 0, 0.6f, 1, 0.2f, {1, 1, 1}, false},
    {"Synchronous Swarm", Look::Fireflies, 90, 10, 0.05f, 4, 7, 0.2f, 0, 0.4f, 0.1f, 0, 0.8f, 1, 0.9f, {1, 1, 1}, false},
    {"Campfire Embers", Look::Embers, 120, 9, 0.035f, 0.6f, 2.5f, 2.0f, 1.6f, 1.4f, 0.6f, 0, 0, 0, 0, {1, 1, 1}, false},
    {"Forge Sparks", Look::Embers, 400, 14, 0.02f, 0.3f, 1.2f, 5.0f, -1.5f, 0.8f, 0.2f, 0, 0, 0, 0, {1, 1, 1}, false},
    {"Fairy Dust", Look::Magic, 150, 7, 0.03f, 1.5f, 1.8f, 0.6f, 0.1f, 0.8f, 0, 2.5f, 3.0f, 0.6f, 0, {1, 1, 1}, true},
    {"Arcane Swirl", Look::Magic, 260, 9, 0.025f, 2.5f, 2.2f, 1.0f, 0, 1.2f, 0, 5.0f, 0, 0, 0, {0.7f, 0.8f, 1.0f}, true},
};

void applyPreset(E& e, const Preset& p) {
    const Rows& r = rows();
    r.set(e, "look", static_cast<float>(p.look));
    r.set(e, "rate", p.rate);
    r.set(e, "brightness", p.brightness);
    r.set(e, "size", p.size);
    r.set(e, "radius", p.radius);
    r.set(e, "life", p.lifetime);
    r.set(e, "speed", p.speed);
    r.set(e, "rise", p.rise);
    r.set(e, "turbulence", p.turbulence);
    r.set(e, "wind", p.wind);
    r.set(e, "orbit", p.orbit);
    r.set(e, "pulseRate", p.pulseRate);
    r.set(e, "pulseDepth", p.pulseDepth);
    r.set(e, "pulseSync", p.pulseSync);
    r.setColor(e, "color", p.color);
    r.setBool(e, "trail", p.trail);
    e.style = p.name;
}

void style0(E& e) { applyPreset(e, kPresets[0]); }
void style1(E& e) { applyPreset(e, kPresets[1]); }
void style2(E& e) { applyPreset(e, kPresets[2]); }
void style3(E& e) { applyPreset(e, kPresets[3]); }
void style4(E& e) { applyPreset(e, kPresets[4]); }
void style5(E& e) { applyPreset(e, kPresets[5]); }

constexpr EffectStyle kStyles[] = {
    {kPresets[0].name, style0}, {kPresets[1].name, style1}, {kPresets[2].name, style2},
    {kPresets[3].name, style3}, {kPresets[4].name, style4}, {kPresets[5].name, style5},
};

// The swarm answers the music out of the box: its density breathes with the level, and a hit
// throws a handful more into the air.
constexpr EffectRoute kRoutes[] = {
    {"audio.rms", "rate", 400.0f, 80.0f, 600.0f},
    {"audio.onset", "burst", 60.0f, 5.0f, 60.0f},
};

E make(std::string name) {
    E e;
    e.name = std::move(name);
    e.kind = EffectKind::ParticleEmitter;
    e.activation = Activation::Always;
    e.timing.fadeIn = 0.0;
    e.timing.fadeOut = 0.0;
    applyPreset(e, kPresets[0]);
    return e;
}

Result<void> validate(const E& e) {
    const float look = rows().f(e, "look");
    if (!(look >= 0.0f && look <= 2.0f)) {
        return fail("effect '{}': look {} is not one of fireflies/embers/magic", e.id, look);
    }
    return {};
}

EffectSchema buildSchema() {
    EffectSchema s;
    s.kind = EffectKind::ParticleEmitter;
    s.key = "particleEmitter";
    s.enumName = "ParticleEmitter";
    s.displayName = "Particle Emitter";
    s.description = "A particle system that rides its owner: fireflies that blink in loose synchrony, "
                    "embers that rise and cool, magic motes that orbit and trail.";
    s.performance = PerformanceClass::Low;
    s.primaryCost = CostCompute | CostFragment;
    s.addLabel = "Particle Emitter";
    s.addTip = "Fireflies, embers or magic motes, attached to this owner.\n"
               "Pick a look from the presets; every number is modulatable.";
    s.targets = targetBit(EffectTarget::Entity) | targetBit(EffectTarget::World);
    s.category = EffectCategory::Particles;
    s.stage = RenderStage::Particles;
    s.fields = kFields;
    s.styles = kStyles;
    s.routes = kRoutes;
    s.beatLeaf = "burst";
    s.factory = make;
    s.resolve.bucket = EffectBucket::Emitter;
    s.resolve.records = particleEmitterRecords;
    s.validate = validate;
    return s;
}

} // namespace

const EffectSchema& particleEmitterSchema() {
    static const EffectSchema kSchema = buildSchema();
    return kSchema;
}

void describeParticleSystem(const EffectInstance& e, float envelope, scene::ParticleSystem& s) {
    const Rows& r = rows();
    // Reset to a default system first, keeping the name (its storage) and the curves' vectors'
    // capacity: a look change must not inherit the previous look's ramp.
    std::string name = std::move(s.name);
    s.colorCurve.keys.clear();
    s.opacityCurve.keys.clear();
    s.sizeCurve.keys.clear();
    {
        scene::ParticleSystem fresh;
        fresh.name = std::move(name);
        fresh.colorCurve.keys = std::move(s.colorCurve.keys);
        fresh.opacityCurve.keys = std::move(s.opacityCurve.keys);
        fresh.sizeCurve.keys = std::move(s.sizeCurve.keys);
        s = std::move(fresh);
    }
    const int look = std::clamp(static_cast<int>(r.f(e, "look") + 0.5f), 0, 2);
    switch (static_cast<Look>(look)) {
    case Look::Fireflies: baseFireflies(s); break;
    case Look::Embers: baseEmbers(s); break;
    case Look::Magic: baseMagic(s); break;
    }

    const float lifetime = std::max(r.f(e, "life"), 0.1f);
    const float speed = std::max(r.f(e, "speed"), 0.0f);
    const float size = std::max(r.f(e, "size"), 0.001f);
    const glm::vec3 tint = r.c(e, "color");
    s.seed = seedOf(e.id);
    s.spawnRate = std::max(r.f(e, "rate"), 0.0f) * std::clamp(envelope, 0.0f, 1.0f);
    s.burst = envelope > 0.0f ? std::max(r.f(e, "burst"), 0.0f) : 0.0f;
    s.lifetimeMin = lifetime * 0.6f;
    s.lifetimeMax = lifetime * 1.4f;
    s.speedMin = speed * 0.3f;
    s.speedMax = speed;
    s.gravity = glm::vec3(0.0f, r.f(e, "rise"), 0.0f);
    s.turbulence = std::max(r.f(e, "turbulence"), 0.0f);
    s.windInfluence = std::max(r.f(e, "wind"), 0.0f);
    s.orbit = r.f(e, "orbit");
    s.emissive = std::max(r.f(e, "brightness"), 0.0f);
    // sizeEnd was set by the look as a marker: <0 means "same as start", otherwise a fraction.
    const float endMarker = s.sizeEnd;
    s.sizeStart = size;
    s.sizeEnd = endMarker < 0.0f ? size : size * endMarker;
    s.colorStart = glm::vec4(glm::vec3(s.colorStart) * tint, s.colorStart.a);
    s.colorEnd = glm::vec4(glm::vec3(s.colorEnd) * tint, s.colorEnd.a);
    for (scene::ColorKey& k : s.colorCurve.keys) {
        k.color *= tint;
    }
    s.pulseRate = std::max(r.f(e, "pulseRate"), 0.0f);
    s.pulseDepth = std::clamp(r.f(e, "pulseDepth"), 0.0f, 1.0f);
    s.pulseSync = std::clamp(r.f(e, "pulseSync"), 0.0f, 1.0f);
    s.trailEnabled = r.b(e, "trail");
    const float radius = std::max(r.f(e, "radius"), 0.05f);
    s.extent = glm::vec3(radius);
    s.attractorRadius = radius * 2.0f;
    // World placement; an entity owner overrides it in the builder.
    s.position = glm::vec3(r.f(e, "centerX"), r.f(e, "centerY"), r.f(e, "centerZ"));
    s.attractorPosition = s.position;
    s.enabled = e.enabled;
}

std::size_t particleEmitterRecords(const EffectInstance& e, const EffectContext& ctx) {
    if (!e.enabled) {
        return 0;
    }
    if (e.owner.kind == EffectTarget::Entity) {
        NodeView view;
        if (ctx.scene == nullptr || !ctx.scene->nodeView(e.owner.name, view)) {
            return 0;
        }
    }
    const auto window = resolveActivationWindow(e.activation, e.timing, ctx.seconds, ctx.shots,
                                                e.owner.kind != EffectTarget::Entity, e.owner.name);
    return window ? 1u : 0u;
}

} // namespace avgen::world
