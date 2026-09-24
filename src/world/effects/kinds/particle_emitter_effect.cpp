// Particle Emitter (ADR-703, EMIT): an effect-owned particle system riding its owner.
//
// One type, many looks. `look` picks a base configuration of the engine's existing particle system
// -- the part that makes fireflies blink in loose synchrony, embers rise and cool through a
// blackbody ramp, magic motes orbit their caster with short trails -- and the rows are the controls
// an artist reaches for on top of it. The looks are configurations, not code paths: everything a
// look sets is an ordinary `scene::ParticleSystem` field (docs/design/effect-library/
// catalog-particles.md, "nine of ten are presets"). The presets in the Add Effect menu are styles
// that set `look` and the rows together.
//
// Wave 2 adds the catalog's remaining looks -- snow, rain, ash, leaves, spores, cosmic dust -- and
// TRIGGER bursts: a Trigger-activated emitter adds `triggerBurst` particles on the frame each of its
// events lands on (`effectTriggerEdge`). The weather looks (snow, rain, ash) are carried by the camera
// (ADR-520's `volumeFollow` + `volumeWrap`), so on a World owner their World X/Y/Z is an offset FROM
// THE CAMERA and `radius` is the half-size of the box that travels with it.

#include "world/effects/effect_registry.hpp"
#include "world/effects/particle_emitter.hpp"

#include <glm/gtc/matrix_access.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <utility>

namespace avgen::world {
namespace {

using E = EffectInstance;

constexpr const char* kLooks[] = {"fireflies", "embers", "magic", "snow", "rain", "ash", "leaves", "spores",
                                  "cosmicDust"};
enum class Look : int {
    Fireflies = 0,
    Embers = 1,
    Magic = 2,
    // Wave 2 (catalog-particles.md's remaining presets). Stars is not here: it is a Sky-stage type.
    Snow = 3,
    Rain = 4,
    Ash = 5,
    Leaves = 6,
    Spores = 7,
    CosmicDust = 8,
};
constexpr int kLookCount = static_cast<int>(std::size(kLooks));

constexpr EffectField kFields[] = {
    storedChoice("look", "Look", 0, kLooks).main()
        .tooltip("The kind of particle: fireflies, embers, magic motes, snow, rain, ash, leaves, spores\n"
                 "or cosmic dust. Each is a base configuration the rows below adjust."),
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
    storedFloat("triggerBurst", "Burst per trigger", 0.0f, 0.0f, 20000.0f, 0.0f, 2000.0f).fmt("%.0f")
        .tooltip("With the Trigger activation: particles thrown out on the frame each trigger lands\n"
                 "(a beat, an onset, a drop, a marker). A puff of spores on the bar, a spray of\n"
                 "sparks on an impact."),
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

// ---- Wave 2 looks --------------------------------------------------------------------------------

// Weather is carried by the camera (ADR-520): the box follows the view on every axis and wraps, so a
// steady fall never thins out at the box's faces and a fly-through never outruns it.
void cameraVolume(scene::ParticleSystem& s) {
    s.shape = scene::EmitterShape::Box;
    s.volumeFollow = glm::vec3(1.0f);
    s.volumeWrap = true;
}

void baseSnow(scene::ParticleSystem& s) {
    s.capacity = 16384;
    cameraVolume(s);
    s.direction = glm::vec3(0.0f, -1.0f, 0.0f);
    s.spread = 0.35f;
    s.drag = 1.6f;
    s.turbulenceScale = 0.45f;
    s.turbulenceSpeed = 0.25f;
    s.sizeEnd = -1.0f;
    s.colorStart = glm::vec4(1.0f, 1.0f, 1.0f, 0.9f);
    s.colorEnd = glm::vec4(1.0f, 1.0f, 1.0f, 0.9f);
    s.opacityCurve.keys = {{0.0f, 0.0f}, {0.08f, 0.9f}, {0.9f, 0.9f}, {1.0f, 0.0f}};
    s.blend = scene::ParticleBlend::Alpha;
    s.sizeVariance = 0.7f;
    s.sizeSkew = 2.2f; // many fine flakes, a few big soft ones
    s.dragSizeBias = 0.6f;
    s.softness = 0.6f;
    s.fogCoupling = 1.0f;
}

void baseRain(scene::ParticleSystem& s) {
    s.capacity = 32768;
    cameraVolume(s);
    s.direction = glm::vec3(0.0f, -1.0f, 0.0f);
    s.spread = 0.02f;
    s.drag = 0.0f;
    s.turbulenceScale = 0.3f;
    s.turbulenceSpeed = 0.2f;
    s.sizeEnd = -1.0f;
    s.colorStart = glm::vec4(0.82f, 0.9f, 1.0f, 0.55f);
    s.colorEnd = glm::vec4(0.82f, 0.9f, 1.0f, 0.55f);
    s.blend = scene::ParticleBlend::Additive;
    // Streaks: the billboard is one shutter's travel long, which is what reads as rain.
    s.velocityStretch = 1.0f;
    s.stretchMax = 0.9f;
    s.stretchMin = 0.02f;
    // Backlit drops blaze and frontlit ones vanish (Tatarchuk), which is most of rain's look.
    s.scatterStrength = 1.4f;
    s.scatterAnisotropy = 0.75f;
    s.sizeVariance = 0.4f;
    s.dragSizeBias = 0.3f;
    s.softness = 0.2f;
    s.fogCoupling = 1.0f;
}

void baseAsh(scene::ParticleSystem& s) {
    s.capacity = 12288;
    cameraVolume(s);
    s.direction = glm::vec3(0.0f, -1.0f, 0.0f);
    s.spread = 0.6f;
    s.drag = 1.8f;
    s.turbulenceScale = 0.35f;
    s.turbulenceSpeed = 0.2f;
    s.sizeEnd = -1.0f;
    s.colorStart = glm::vec4(0.62f, 0.6f, 0.58f, 0.85f);
    s.colorEnd = glm::vec4(0.45f, 0.43f, 0.42f, 0.85f);
    s.opacityCurve.keys = {{0.0f, 0.0f}, {0.1f, 0.85f}, {0.85f, 0.85f}, {1.0f, 0.0f}};
    s.blend = scene::ParticleBlend::Alpha;
    // Flakes, not dots: small tumbling cards.
    s.shape2d = scene::ParticleShape::Leaf;
    s.leafAspect = 0.75f;
    s.tumbleRate = 1.6f;
    s.twoSided = 0.5f;
    s.sizeVariance = 0.6f;
    s.sizeSkew = 1.8f;
    s.dragSizeBias = 0.5f;
    s.softness = 0.4f;
    s.fogCoupling = 1.0f;
}

void baseLeaves(scene::ParticleSystem& s) {
    s.capacity = 4096;
    s.shape = scene::EmitterShape::Sphere;
    s.direction = glm::vec3(0.0f, -1.0f, 0.0f);
    s.spread = 0.8f;
    s.drag = 1.4f;
    s.turbulenceScale = 0.5f;
    s.turbulenceSpeed = 0.35f;
    s.sizeEnd = -1.0f;
    // An autumn palette over life (a leaf does not change colour as it falls; the spread across the
    // population comes from each particle's own position on this ramp at birth-time variance).
    s.colorCurve.keys = {{0.0f, {0.95f, 0.55f, 0.12f}},
                         {0.35f, {0.85f, 0.28f, 0.08f}},
                         {0.7f, {0.75f, 0.62f, 0.15f}},
                         {1.0f, {0.55f, 0.22f, 0.06f}}};
    s.opacityCurve.keys = {{0.0f, 0.0f}, {0.05f, 1.0f}, {0.9f, 1.0f}, {1.0f, 0.0f}};
    s.colorStart = glm::vec4(1.0f);
    s.colorEnd = glm::vec4(1.0f);
    s.blend = scene::ParticleBlend::Alpha;
    s.shape2d = scene::ParticleShape::Leaf;
    s.leafAspect = 0.42f;
    s.tumbleRate = 2.4f;
    s.twoSided = 0.45f;
    s.sizeVariance = 0.45f;
    s.dragSizeBias = 0.4f;
    s.softness = 0.3f;
    s.fogCoupling = 1.0f;
}

void baseSpores(scene::ParticleSystem& s) {
    s.capacity = 8192;
    s.shape = scene::EmitterShape::Sphere;
    s.spread = 1.0f;
    s.drag = 1.1f;
    s.turbulenceScale = 0.7f;
    s.turbulenceSpeed = 0.3f;
    s.sizeEnd = 0.6f;
    s.colorStart = glm::vec4(0.55f, 1.0f, 0.85f, 0.0f);
    s.colorEnd = glm::vec4(0.35f, 0.75f, 1.0f, 0.0f);
    s.opacityCurve.keys = {{0.0f, 0.0f}, {0.15f, 1.0f}, {0.8f, 0.8f}, {1.0f, 0.0f}};
    s.blend = scene::ParticleBlend::Additive;
    s.pulseSharpness = 1.5f; // a soft breathing glow, not a firefly's flash
    s.sizeVariance = 0.5f;
    s.sizeSkew = 1.6f;
    s.softness = 0.4f;
    s.fogCoupling = 1.0f;
}

void baseCosmicDust(scene::ParticleSystem& s) {
    s.capacity = 32768;
    s.shape = scene::EmitterShape::Disc;
    s.direction = glm::vec3(0.0f, 1.0f, 0.0f);
    s.spread = 1.0f;
    s.gravity = glm::vec3(0.0f);
    s.drag = 0.3f;
    s.turbulenceScale = 0.25f;
    s.turbulenceSpeed = 0.1f;
    s.attractorStrength = 0.25f;
    s.sizeEnd = -1.0f;
    s.colorCurve.keys = {{0.0f, {0.55f, 0.65f, 1.0f}},
                         {0.5f, {0.85f, 0.55f, 1.0f}},
                         {1.0f, {1.0f, 0.75f, 0.6f}}};
    s.opacityCurve.keys = {{0.0f, 0.0f}, {0.1f, 1.0f}, {0.9f, 1.0f}, {1.0f, 0.0f}};
    s.colorStart = glm::vec4(1.0f);
    s.colorEnd = glm::vec4(1.0f);
    s.blend = scene::ParticleBlend::Additive;
    s.sizeVariance = 0.9f;
    s.sizeSkew = 3.0f; // a haze of faint grains and a few bright ones
    s.softness = 0.3f;
    s.fogCoupling = 0.6f;
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
    const char* name = "";
    Look look = Look::Fireflies;
    float rate = 60, brightness = 8, size = 0.06f, radius = 3, lifetime = 5, speed = 0.3f, rise = 0,
          turbulence = 0.6f, wind = 0.2f, orbit = 0, pulseRate = 0, pulseDepth = 1, pulseSync = 0;
    glm::vec3 color{1, 1, 1};
    bool trail = false;
    float centerY = 2.0f;  // World Y (an offset above the camera for the carried looks)
    float triggerBurst = 0;
    int triggerEveryN = 0; // > 0: a Trigger activation on every Nth beat
};

constexpr Preset kPresets[] = {
    // ---- Wave 1 ----
    {.name = "Meadow Fireflies", .look = Look::Fireflies, .rate = 60, .brightness = 8, .size = 0.06f, .radius = 6,
     .lifetime = 6, .speed = 0.25f, .turbulence = 0.6f, .wind = 0.2f, .pulseRate = 0.6f, .pulseSync = 0.2f},
    {.name = "Synchronous Swarm", .look = Look::Fireflies, .rate = 90, .brightness = 10, .size = 0.05f, .radius = 4,
     .lifetime = 7, .speed = 0.2f, .turbulence = 0.4f, .wind = 0.1f, .pulseRate = 0.8f, .pulseSync = 0.9f},
    {.name = "Campfire Embers", .look = Look::Embers, .rate = 120, .brightness = 9, .size = 0.035f, .radius = 0.6f,
     .lifetime = 2.5f, .speed = 2.0f, .rise = 1.6f, .turbulence = 1.4f, .wind = 0.6f, .pulseDepth = 0},
    {.name = "Forge Sparks", .look = Look::Embers, .rate = 400, .brightness = 14, .size = 0.02f, .radius = 0.3f,
     .lifetime = 1.2f, .speed = 5.0f, .rise = -1.5f, .turbulence = 0.8f, .wind = 0.2f, .pulseDepth = 0},
    {.name = "Fairy Dust", .look = Look::Magic, .rate = 150, .brightness = 7, .size = 0.03f, .radius = 1.5f,
     .lifetime = 1.8f, .speed = 0.6f, .rise = 0.1f, .turbulence = 0.8f, .wind = 0, .orbit = 2.5f, .pulseRate = 3.0f,
     .pulseDepth = 0.6f, .trail = true},
    {.name = "Arcane Swirl", .look = Look::Magic, .rate = 260, .brightness = 9, .size = 0.025f, .radius = 2.5f,
     .lifetime = 2.2f, .speed = 1.0f, .turbulence = 1.2f, .wind = 0, .orbit = 5.0f, .pulseDepth = 0,
     .color = {0.7f, 0.8f, 1.0f}, .trail = true},
    // ---- Wave 2: the rest of the catalog's presets ----
    {.name = "Glowmere Canopy", .look = Look::Fireflies, .rate = 70, .brightness = 9, .size = 0.05f, .radius = 8,
     .lifetime = 7, .speed = 0.2f, .turbulence = 0.5f, .wind = 0.3f, .pulseRate = 0.45f, .pulseSync = 0.35f,
     .color = {0.6f, 1.0f, 0.95f}},
    {.name = "Burning Debris", .look = Look::Embers, .rate = 60, .brightness = 7, .size = 0.07f, .radius = 1.5f,
     .lifetime = 4.0f, .speed = 1.2f, .rise = 0.8f, .turbulence = 1.8f, .wind = 0.9f, .pulseDepth = 0},
    {.name = "Healing Motes", .look = Look::Magic, .rate = 90, .brightness = 6, .size = 0.035f, .radius = 1.2f,
     .lifetime = 2.6f, .speed = 0.3f, .rise = 0.6f, .turbulence = 0.6f, .wind = 0, .orbit = 1.2f, .pulseRate = 1.2f,
     .pulseDepth = 0.5f, .color = {0.55f, 1.0f, 0.55f}, .trail = true},
    {.name = "Light Snow", .look = Look::Snow, .rate = 700, .brightness = 1.1f, .size = 0.022f, .radius = 14,
     .lifetime = 14, .speed = 0.4f, .rise = -0.9f, .turbulence = 0.35f, .wind = 0.6f, .pulseDepth = 0, .centerY = 4},
    {.name = "Blizzard", .look = Look::Snow, .rate = 4500, .brightness = 1.0f, .size = 0.018f, .radius = 12,
     .lifetime = 8, .speed = 3.0f, .rise = -2.5f, .turbulence = 1.6f, .wind = 1.6f, .pulseDepth = 0, .centerY = 3},
    {.name = "Magical Snow", .look = Look::Snow, .rate = 500, .brightness = 3.0f, .size = 0.025f, .radius = 12,
     .lifetime = 16, .speed = 0.2f, .rise = -0.35f, .turbulence = 0.5f, .wind = 0.3f, .pulseRate = 0.5f,
     .pulseDepth = 0.5f, .color = {0.75f, 0.85f, 1.0f}, .centerY = 4},
    {.name = "Drizzle", .look = Look::Rain, .rate = 2500, .brightness = 0.9f, .size = 0.006f, .radius = 12,
     .lifetime = 2.0f, .speed = 7.0f, .rise = -9.8f, .turbulence = 0.1f, .wind = 0.4f, .pulseDepth = 0, .centerY = 5},
    {.name = "Storm", .look = Look::Rain, .rate = 12000, .brightness = 1.1f, .size = 0.008f, .radius = 12,
     .lifetime = 1.4f, .speed = 12.0f, .rise = -9.8f, .turbulence = 0.3f, .wind = 1.5f, .pulseDepth = 0, .centerY = 6},
    {.name = "Neon Rain", .look = Look::Rain, .rate = 5000, .brightness = 4.0f, .size = 0.007f, .radius = 12,
     .lifetime = 1.8f, .speed = 9.0f, .rise = -9.8f, .turbulence = 0.1f, .wind = 0.3f, .pulseDepth = 0,
     .color = {1.0f, 0.35f, 0.9f}, .centerY = 5},
    {.name = "Volcanic Ashfall", .look = Look::Ash, .rate = 900, .brightness = 1.4f, .size = 0.03f, .radius = 12,
     .lifetime = 14, .speed = 0.3f, .rise = -0.5f, .turbulence = 0.6f, .wind = 0.8f, .pulseDepth = 0,
     .color = {0.9f, 0.88f, 0.86f}, .centerY = 4},
    {.name = "Burned Forest", .look = Look::Ash, .rate = 350, .brightness = 1.1f, .size = 0.04f, .radius = 12,
     .lifetime = 16, .speed = 0.15f, .rise = -0.3f, .turbulence = 0.4f, .wind = 0.5f, .pulseDepth = 0,
     .color = {0.8f, 0.76f, 0.72f}, .centerY = 4},
    {.name = "Autumn Leaves", .look = Look::Leaves, .rate = 25, .brightness = 1.0f, .size = 0.07f, .radius = 3,
     .lifetime = 7, .speed = 0.3f, .rise = -0.7f, .turbulence = 0.5f, .wind = 1.0f, .pulseDepth = 0, .centerY = 4},
    {.name = "Cherry Petals", .look = Look::Leaves, .rate = 60, .brightness = 1.1f, .size = 0.04f, .radius = 3,
     .lifetime = 8, .speed = 0.25f, .rise = -0.4f, .turbulence = 0.7f, .wind = 1.2f, .pulseDepth = 0,
     .color = {1.0f, 1.3f, 5.0f}, .centerY = 4},
    {.name = "Glowmere Drift", .look = Look::Leaves, .rate = 30, .brightness = 3.0f, .size = 0.06f, .radius = 4,
     .lifetime = 9, .speed = 0.2f, .rise = -0.4f, .turbulence = 0.6f, .wind = 0.8f, .pulseRate = 0.3f,
     .pulseDepth = 0.4f, .color = {0.5f, 1.6f, 2.2f}, .centerY = 4},
    {.name = "Mushroom Puff", .look = Look::Spores, .rate = 40, .brightness = 3.0f, .size = 0.03f, .radius = 0.8f,
     .lifetime = 5, .speed = 0.3f, .rise = 0.15f, .turbulence = 0.9f, .wind = 0.4f, .pulseRate = 0.4f,
     .pulseDepth = 0.5f, .centerY = 0.5f},
    {.name = "Glowmere Spores", .look = Look::Spores, .rate = 120, .brightness = 4.0f, .size = 0.025f, .radius = 5,
     .lifetime = 9, .speed = 0.15f, .rise = 0.08f, .turbulence = 1.4f, .wind = 0.5f, .pulseRate = 0.3f,
     .pulseDepth = 0.6f, .color = {0.7f, 1.0f, 1.2f}},
    {.name = "Spore Burst", .look = Look::Spores, .rate = 0, .brightness = 5.0f, .size = 0.03f, .radius = 0.6f,
     .lifetime = 4, .speed = 1.4f, .rise = 0.2f, .turbulence = 1.2f, .wind = 0.4f, .pulseRate = 0.4f,
     .pulseDepth = 0.4f, .centerY = 0.5f, .triggerBurst = 300, .triggerEveryN = 4},
    {.name = "Nebula Drift", .look = Look::CosmicDust, .rate = 900, .brightness = 2.0f, .size = 0.02f, .radius = 20,
     .lifetime = 20, .speed = 0.1f, .turbulence = 0.3f, .wind = 0, .orbit = 0.6f, .pulseDepth = 0},
    {.name = "Hyperspace Dust", .look = Look::CosmicDust, .rate = 3000, .brightness = 3.0f, .size = 0.012f,
     .radius = 15, .lifetime = 3, .speed = 6.0f, .turbulence = 0.1f, .wind = 0, .pulseDepth = 0,
     .color = {0.8f, 0.9f, 1.2f}},
    {.name = "Vortex Motes", .look = Look::CosmicDust, .rate = 1200, .brightness = 2.5f, .size = 0.02f, .radius = 10,
     .lifetime = 14, .speed = 0.4f, .turbulence = 0.5f, .wind = 0, .orbit = 2.5f, .pulseDepth = 0},
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
    r.set(e, "centerY", p.centerY);
    r.set(e, "triggerBurst", p.triggerBurst);
    if (p.triggerEveryN > 0) {
        e.activation = Activation::Trigger;
        e.timing.trigger = Trigger{};
        e.timing.trigger.source = TriggerSource::Beat;
        e.timing.trigger.everyN = p.triggerEveryN;
    } else if (e.activation == Activation::Trigger) {
        e.activation = Activation::Always; // a preset is a complete look, including when it runs
    }
    e.style = p.name;
}

template <std::size_t I>
void stylePreset(E& e) {
    applyPreset(e, kPresets[I]);
}

template <std::size_t... I>
constexpr std::array<EffectStyle, sizeof...(I)> makeStyles(std::index_sequence<I...>) {
    return {EffectStyle{kPresets[I].name, &stylePreset<I>}...};
}

constexpr auto kStyles = makeStyles(std::make_index_sequence<std::size(kPresets)>{});

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
    if (!(look >= 0.0f && look <= static_cast<float>(kLookCount - 1))) {
        return fail("effect '{}': look {} is not one of the {} looks", e.id, look, kLookCount);
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
                    "embers that rise and cool, magic motes that orbit and trail, snow, rain and ash "
                    "carried with the camera, tumbling leaves, drifting spores and cosmic dust. On a "
                    "trigger it can throw out a burst.";
    s.performance = PerformanceClass::Low;
    s.primaryCost = CostCompute | CostFragment;
    s.addLabel = "Particle Emitter";
    s.addTip = "Fireflies, embers, magic motes, weather, leaves, spores or dust, attached to this owner.\n"
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
    const int look = std::clamp(static_cast<int>(r.f(e, "look") + 0.5f), 0, kLookCount - 1);
    switch (static_cast<Look>(look)) {
    case Look::Fireflies: baseFireflies(s); break;
    case Look::Embers: baseEmbers(s); break;
    case Look::Magic: baseMagic(s); break;
    case Look::Snow: baseSnow(s); break;
    case Look::Rain: baseRain(s); break;
    case Look::Ash: baseAsh(s); break;
    case Look::Leaves: baseLeaves(s); break;
    case Look::Spores: baseSpores(s); break;
    case Look::CosmicDust: baseCosmicDust(s); break;
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

float triggerBurstOf(const EffectInstance& e) { return std::max(rows().f(e, "triggerBurst"), 0.0f); }

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
    const auto window = resolveActivationWindow(e.activation, e.timing, ctx, e.owner.kind != EffectTarget::Entity,
                                                e.owner.name,
                                                e.owner.kind == EffectTarget::Entity ? std::string_view(e.owner.name)
                                                                                     : std::string_view());
    return window ? 1u : 0u;
}

} // namespace avgen::world
