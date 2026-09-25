// Charge-Up (Effect Library Wave 3, catalog-energy.md "Charge-Up"): the gather before a release.
// Energy streams in towards the owner, whose core swells, brightens and boils harder over
// `chargeSeconds`, holds at the peak, and is released in a flare.
//
// **Phases.** A pure function of the transport second and the activation window: from the window's
// start (TRIGGER: the event), `charge = (t / chargeSeconds)^ease` rises 0 -> 1; it holds at 1 for
// `hold` seconds (0: until the window ends); then the release plays for `releaseTime` -- the core
// blows out and fades, a ring of glare expands, the motes are thrown outward -- and the instance is
// dormant until the next window. Because every phase is recomputed from the window each frame, a
// scrub lands exactly where a play does; the particles follow the particle system's own seek rule
// (empty after a seek, warm within a lifetime -- ADR-360/395). The release is cut short so it ends
// inside the window: a window that closes first would otherwise hide the flare.
//
// **How it is drawn.** Three existing primitives, no shader of its own: the core is a SHELL Plasma orb
// (shading `Plasma`) whose radius, brightness, strand sharpness and turbulence all rise with the charge;
// a SHELL Glare (Halo's shading) round it grows into the held flare and carries the release's
// expanding ring; EMIT (`EffectResolve::particles`) streams motes in from a sphere of `radius` -- an
// attractor at the core and an orbit for the swirl, the rate times the charge -- and on the release
// frame flips the attractor to a push and throws a burst; LIGHTMOD's pool gives the core a light that
// swells with the charge and flashes on release.
//
// **Its charge as a signal.** The catalog asks for `fx.<id>.charge` on the signal bus, so a Glow can
// route it onto its gain. SIGNALS today publishes entity signals (`entity.<name>.*`, registered and
// written by the engine before the routes run) and has no path for an EFFECT to publish one:
// adding it is engine work (register a signal per live instance, write it in `publishEntitySignals`)
// outside this type. `chargeUpCharge` below is the pure function such a publisher would call.

#include "scene/particles.hpp"
#include "world/effects/effect_lights.hpp"
#include "world/effects/effect_registry.hpp"
#include "world/effects/kinds/shell_fx_kind.hpp"

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <string>

namespace avgen::world {
namespace {

using E = EffectInstance;

constexpr EffectField kFields[] = {
    storedFloat("chargeSeconds", "Charge time", 2.5f, 0.05f, 120.0f, 0.2f, 12.0f).fmt("%.2f s").main().floorAt(0.05f)
        .tooltip("How long the gather takes to reach full charge from the activation."),
    storedFloat("ease", "Ease", 1.6f, 0.2f, 6.0f, 0.3f, 4.0f).main().clampTo(0.2f, 6.0f)
        .tooltip("The charge's curve: 1 rises evenly; above 1 starts slowly and rushes at the end."),
    storedFloat("hold", "Hold", 0.5f, 0.0f, 120.0f, 0.0f, 6.0f).fmt("%.2f s").main().floorAt(0.0f)
        .tooltip("How long it holds at full charge before the release. 0 holds until the activation\n"
                 "ends (a Window, or a Trigger's lifetime)."),
    storedFloat("releaseTime", "Release", 0.45f, 0.05f, 10.0f, 0.1f, 2.0f).fmt("%.2f s").clampTo(0.05f, 10.0f)
        .tooltip("How long the release flare lasts."),
    storedFloat("radius", "Gather radius", 3.0f, 0.1f, 200.0f, 0.5f, 20.0f).fmt("%.2f m").main().log().floorAt(0.1f)
        .tooltip("How far out the incoming motes start."),
    storedFloat("coreRadius", "Core radius", 0.35f, 0.02f, 50.0f, 0.05f, 3.0f).fmt("%.2f m").main().sec("Core").log().floorAt(0.02f)
        .tooltip("The core's radius at full charge."),
    storedColor("coreColor", "Core colour", glm::vec3(0.9f, 0.95f, 1.0f)).main(),
    storedFloat("coreIntensity", "Core brightness", 3.0f, 0.0f, 100.0f, 0.0f, 10.0f).main().floorAt(0.0f)
        .tooltip("HDR brightness at the core's centre at full charge."),
    storedFloat("flare", "Flare", 1.0f, 0.0f, 10.0f, 0.0f, 3.0f).sec("Core").floorAt(0.0f)
        .tooltip("The glare round the core as it nears full charge, and the release's flash."),
    storedColor("particleColor", "Energy colour", glm::vec3(0.35f, 0.65f, 1.0f)).main().sec("Energy"),
    storedFloat("particleRate", "Motes", 220.0f, 0.0f, 5000.0f, 0.0f, 800.0f).fmt("%.0f /s").main().floorAt(0.0f)
        .tooltip("Motes streaming in per second at full charge (the rate follows the charge)."),
    storedFloat("swirl", "Swirl", 1.5f, -20.0f, 20.0f, -6.0f, 6.0f).sec("Energy")
        .tooltip("How much the incoming motes circle the core. Negative circles the other way."),
    storedFloat("releaseBurst", "Release burst", 300.0f, 0.0f, 10000.0f, 0.0f, 1500.0f).fmt("%.0f").floorAt(0.0f)
        .tooltip("Motes thrown outward on the release frame."),
    storedFloat("light", "Light", 150.0f, 0.0f, 100000.0f, 0.0f, 1000.0f).sec("Light").floorAt(0.0f)
        .tooltip("The core's light at full charge, in candela; it flashes on release. Shares the 16-light pool."),
    storedFloat("offsetX", "Offset X", 0.0f, -100000.0f, 100000.0f, -20.0f, 20.0f).fmt("%.2f m").sec("Placement")
        .tooltip("On the World: the charging point. On an entity: an offset from its centre."),
    storedFloat("offsetY", "Offset Y", 0.0f, -100000.0f, 100000.0f, -20.0f, 20.0f).fmt("%.2f m"),
    storedFloat("offsetZ", "Offset Z", 0.0f, -100000.0f, 100000.0f, -20.0f, 20.0f).fmt("%.2f m"),
};

constexpr kinds::StoredRows kRows{"chargeUp", kFields};

Trigger repeat(double period, double phase) {
    Trigger t;
    t.source = TriggerSource::Repeat;
    t.period = period;
    t.phase = phase;
    return t;
}
Trigger music(const char* name) {
    Trigger t;
    t.source = TriggerSource::MusicEvent;
    t.name = name;
    return t;
}

struct Look {
    const char* name;
    float chargeSeconds, ease, hold, release;
    glm::vec3 core, energy;
    float coreIntensity, rate, swirl;
    Trigger trigger;
};

const Look kLooks[] = {
    // A power gather: blue-white, a steady build, a short hold -- every four seconds as a default loop.
    {"Power Gather", 2.5f, 1.6f, 0.5f, 0.45f, {0.9f, 0.95f, 1.0f}, {0.35f, 0.65f, 1.0f}, 3.0f, 220.0f, 1.5f, repeat(4.0, 0.5)},
    // A beam wind-up: hot orange, fast and tight, a hard snap.
    {"Beam Wind-Up", 1.2f, 2.2f, 0.25f, 0.3f, {1.0f, 0.85f, 0.6f}, {1.0f, 0.42f, 0.12f}, 3.5f, 380.0f, 3.0f, repeat(3.0, 0.5)},
    // Build to drop: a long gather from the music's build, released just after it peaks.
    {"Build-to-Drop", 6.0f, 1.8f, 0.5f, 0.6f, {0.95f, 0.85f, 1.0f}, {0.7f, 0.3f, 1.0f}, 3.0f, 260.0f, 2.0f, music("build")},
};

void applyLook(E& e, const Look& l) {
    kRows.set(e, "chargeSeconds", l.chargeSeconds);
    kRows.set(e, "ease", l.ease);
    kRows.set(e, "hold", l.hold);
    kRows.set(e, "releaseTime", l.release);
    kRows.setRgb(e, "coreColor", l.core);
    kRows.setRgb(e, "particleColor", l.energy);
    kRows.set(e, "coreIntensity", l.coreIntensity);
    kRows.set(e, "particleRate", l.rate);
    kRows.set(e, "swirl", l.swirl);
    e.activation = Activation::Trigger;
    e.timing.trigger = l.trigger;
    e.timing.lifetime = 0.0;
    e.style = l.name;
}
void style0(E& e) { applyLook(e, kLooks[0]); }
void style1(E& e) { applyLook(e, kLooks[1]); }
void style2(E& e) { applyLook(e, kLooks[2]); }

constexpr EffectStyle kStyles[] = {{"Power Gather", style0}, {"Beam Wind-Up", style1}, {"Build-to-Drop", style2}};

// The catalogue's modulation: the level feeds the stream of motes.
constexpr EffectRoute kRoutes[] = {
    {"audio.rms", "particleRate", 200.0f, 30.0f, 300.0f},
};

E make(std::string name) {
    E e;
    e.name = std::move(name);
    e.kind = EffectKind::ChargeUp;
    e.timing = Timing{};
    // The charge and the release ARE the envelope; the fades would fight them.
    e.timing.fadeIn = 0.0;
    e.timing.fadeOut = 0.0;
    applyLook(e, kLooks[0]);
    return e;
}

// ---- the phases -------------------------------------------------------------------------------------

struct State {
    glm::vec3 centre{0.0f};
    float ownerRadius = 0.0f;
    float charge = 0.0f;   // 0..1
    float release = -1.0f; // < 0 charging or holding; 0..1 through the release
    double releaseAt = -1.0; // the release's instant on the transport clock (-1: none scheduled)
    float envelope = 1.0f;
};

bool phaseOf(const E& e, const EffectContext& ctx, State& out) {
    out = State{};
    kinds::FxLive live;
    if (e.owner.kind == EffectTarget::Light || !kinds::fxLive(e, ctx, live, false)) {
        return false;
    }
    const double chargeSeconds = std::max(static_cast<double>(kRows.f(e, "chargeSeconds")), 0.05);
    const double hold = std::max(static_cast<double>(kRows.f(e, "hold")), 0.0);
    const double releaseTime = std::clamp(static_cast<double>(kRows.f(e, "releaseTime")), 0.05, 10.0);
    double releaseAt = hold > 0.0 ? chargeSeconds + hold : std::numeric_limits<double>::infinity();
    if (std::isfinite(live.end)) {
        // The release must finish inside the window, or the window's close would hide the flare.
        releaseAt = std::min(releaseAt, std::max(live.end - live.start - releaseTime, 0.0));
    }
    out.releaseAt = std::isfinite(releaseAt) ? live.start + releaseAt : -1.0;
    out.envelope = live.envelope > 0.0f ? live.envelope : 1.0f;
    const double local = live.local;
    if (local >= releaseAt) {
        const double rel = (local - releaseAt) / releaseTime;
        if (rel >= 1.0) {
            return false; // released: dormant until the next window
        }
        out.release = static_cast<float>(rel);
        out.charge = 1.0f;
    } else {
        const double x = std::clamp(local / chargeSeconds, 0.0, 1.0);
        out.charge = static_cast<float>(std::pow(x, static_cast<double>(std::clamp(kRows.f(e, "ease"), 0.2f, 6.0f))));
    }
    const glm::vec3 offset(kRows.f(e, "offsetX"), kRows.f(e, "offsetY"), kRows.f(e, "offsetZ"));
    kinds::FxAnchor anchor;
    if (!kinds::fxAnchor(e, ctx, offset, anchor)) {
        return false;
    }
    out.centre = anchor.centre;
    out.ownerRadius = glm::length(anchor.halfExtents);
    return true;
}

std::size_t records(const E& e, const EffectContext& ctx) {
    State s;
    return phaseOf(e, ctx, s) ? 2u : 0u;
}

// Two shells per live instance, its core and its flare (what the conformance probe is entitled to).
std::size_t shellCount(const E&) { return 2; }

EffectStatus emit(const E& e, const EffectContext& ctx, ShellSink& sink, std::string& reason) {
    const bool lost = !reason.empty();
    State s;
    if (!phaseOf(e, ctx, s)) {
        return kinds::shellDormant(e, ctx, reason);
    }
    const float c = s.charge;
    const float rel = std::max(s.release, 0.0f);
    const bool releasing = s.release >= 0.0f;
    const float fade = releasing ? (1.0f - rel) * (1.0f - rel) : 1.0f;
    const float seed = kinds::seedOf(e.id);
    const glm::vec3 core = kRows.rgb(e, "coreColor");
    const glm::vec3 energy = kRows.rgb(e, "particleColor");
    const float coreRadius = std::max(kRows.f(e, "coreRadius"), 0.02f);

    // The core: a plasma orb that swells, brightens and boils harder with the charge, and blows out on
    // release. Record layout: shaders/shell.wgsl `fs_plasma`.
    ShellInstance orb;
    const float radius = coreRadius * (0.3f + 0.7f * c) * (1.0f + 1.4f * rel);
    orb.model = kinds::shellModel(s.centre, glm::vec3(radius));
    const float coreI = std::max(kRows.f(e, "coreIntensity"), 0.0f) * (0.2f + 0.8f * c * c) *
                        (releasing ? fade * (1.0f + 0.6f * (1.0f - rel)) : 1.0f) * s.envelope;
    orb.params[0] = glm::vec4(core, coreI);
    orb.params[1] = glm::vec4(seed, static_cast<float>(ctx.seconds * 1.6), 0.0f, 0.0f);
    orb.params[2] = glm::vec4(energy, (0.45f + 0.45f * c) * fade * s.envelope);
    orb.params[3] = glm::vec4(0.6f + 2.0f * c, 2.4f, 16.0f, 0.35f);
    orb.params[4] = glm::vec4(0.22f, 0.3f + 0.45f * c, 1.0f, 0.6f * fade * s.envelope);

    // The flare: a glare that grows into the held peak; on release, its ring races outward.
    // Record layout: shaders/shell_fx.wgsl `fs_glare`.
    ShellInstance glare;
    const float flare = std::max(kRows.f(e, "flare"), 0.0f);
    const float glareR = coreRadius * (1.6f + 2.4f * c) * (1.0f + 3.0f * rel);
    glare.model = kinds::fxModel(s.centre, glm::vec3(glareR, 0.0f, 0.0f), glm::vec3(0.0f, glareR, 0.0f),
                                 glm::vec3(0.0f, 0.0f, glareR));
    const float glareI = flare * (releasing ? 1.2f * fade : 0.05f + 0.55f * c * c * c) * s.envelope;
    glare.params[0] = glm::vec4(glm::mix(core, energy, 0.35f), glareI);
    glare.params[1] = glm::vec4(seed, static_cast<float>(ctx.seconds), 0.0f, 0.0f);
    glare.params[2] = glm::vec4(s.centre, glareR);
    glare.params[3] = glm::vec4(0.16f, releasing ? std::clamp(0.2f + 0.75f * rel, 0.05f, 1.0f) : 0.6f, 0.05f,
                                releasing ? 0.9f * (1.0f - rel) : 0.0f);
    glare.params[4] = glm::vec4(0.3f, std::max(s.ownerRadius, 0.2f), 0.3f, 0.0f);
    glare.params[5] = glm::vec4(0.1f, 90.0f, 0.0f, 0.0f);

    if (sink.remaining() < 2) {
        reason = "The shell budget (128) is full this frame: not drawn.";
        return EffectStatus::Dropped;
    }
    static_cast<void>(sink.append(ShellShading::Plasma, ShellMesh::Sphere, orb));
    static_cast<void>(sink.append(ShellShading::Glare, ShellMesh::Quad, glare));
    sink.usesDepth();

    const float light = std::max(kRows.f(e, "light"), 0.0f) * (releasing ? 2.5f * fade : c * c) * s.envelope;
    if (light > 0.0f) {
        glm::vec3 hue = core * 0.5f + energy * 0.5f;
        hue /= std::max({hue.r, hue.g, hue.b, 1e-4f});
        EffectLight l;
        l.position = s.centre;
        l.intensity = light;
        l.color = hue;
        l.range = std::max(kRows.f(e, "radius"), coreRadius * 8.0f) * 2.0f;
        l.volumetric = 0.35f;
        sink.requestLight(l);
    }
    return kinds::fxDrawn(lost);
}

// EMIT: motes streaming in, pulled by an attractor at the core; pushed out and burst on release.
bool particles(const E& e, const EffectContext& ctx, scene::ParticleSystem& p) {
    p.capacity = 8192;
    p.shape = scene::EmitterShape::Sphere;
    p.spawnRate = 0.0f;
    p.burst = 0.0f;
    p.spread = 1.0f;
    p.speedMin = 0.05f;
    p.speedMax = 0.3f;
    p.gravity = glm::vec3(0.0f);
    p.drag = 0.35f;
    p.turbulence = 0.3f;
    p.turbulenceScale = 0.5f;
    p.turbulenceSpeed = 0.4f;
    p.lifetimeMin = 0.9f;
    p.lifetimeMax = 1.7f;
    p.opacityCurve.keys = {{0.0f, 0.0f}, {0.25f, 1.0f}, {0.85f, 0.9f}, {1.0f, 0.0f}};
    p.blend = scene::ParticleBlend::Additive;
    p.velocityStretch = 0.7f;
    p.stretchMax = 0.3f;
    p.softness = 0.2f;
    p.fogCoupling = 1.0f;
    State s;
    if (!phaseOf(e, ctx, s)) {
        return false;
    }
    const glm::vec3 energy = kRows.rgb(e, "particleColor");
    p.colorStart = glm::vec4(energy, 1.0f);
    p.colorEnd = glm::vec4(glm::mix(energy, glm::vec3(1.0f), 0.6f), 0.0f);
    p.emissive = 2.2f;
    const float radius = std::max(kRows.f(e, "radius"), 0.1f);
    // Motes sized to the gather, so a charge round a saucer is not dust and one round a hand not hail.
    p.sizeStart = 0.012f * radius;
    p.sizeEnd = 0.002f * radius;
    p.position = s.centre;
    p.extent = glm::vec3(radius);
    p.attractorPosition = s.centre;
    p.attractorRadius = radius * 1.5f;
    p.orbit = kRows.f(e, "swirl");
    p.seed = static_cast<std::uint32_t>(kinds::seedOf(e.id)) + 5u;
    if (s.release >= 0.0f) {
        p.attractorStrength = -6.0f * radius; // thrown out
        if (s.releaseAt >= 0.0 && kinds::fxEdge(s.releaseAt, ctx)) {
            p.burst = std::max(kRows.f(e, "releaseBurst"), 0.0f);
            p.speedMin = 1.5f * radius;
            p.speedMax = 3.5f * radius;
        }
        return true;
    }
    p.attractorStrength = (1.0f + 3.0f * s.charge) * radius;
    p.spawnRate = std::max(kRows.f(e, "particleRate"), 0.0f) * s.charge;
    return true;
}

EffectSchema buildSchema() {
    EffectSchema s;
    s.kind = EffectKind::ChargeUp;
    s.key = "chargeUp";
    s.enumName = "ChargeUp";
    s.displayName = "Charge-Up";
    s.description = "The gather before a release: energy streams in towards the owner, whose core swells, "
                    "brightens and crackles harder as it charges, holds at the peak, then releases in a flare "
                    "and a burst. Starts on its activation (a beat, a build, a marker).";
    s.performance = PerformanceClass::Low;
    s.primaryCost = CostFragment | CostCompute;
    s.addLabel = "Charge-Up";
    s.addTip = "A gathering of energy into this entity, or at a point in the world, released in a flare.";
    s.targets = targetBit(EffectTarget::Entity) | targetBit(EffectTarget::World);
    s.category = EffectCategory::Lighting;
    s.stage = RenderStage::Particles;
    s.fields = kFields;
    s.styles = kStyles;
    s.routes = kRoutes;
    s.beatLeaf = "flare";
    s.factory = make;
    s.resolve.bucket = EffectBucket::Shell;
    s.resolve.records = records;
    s.resolve.count = shellCount;
    s.resolve.particles = particles;
    registerShellProducer(EffectKind::ChargeUp, ShellProducer{emit});
    return s;
}

} // namespace

const EffectSchema& chargeUpSchema() {
    static const EffectSchema kSchema = buildSchema();
    return kSchema;
}

// The charge (0..1) of an instance at `ctx.seconds` -- 1 through the release, 0 when dormant: what a
// SIGNALS publisher of `fx.<id>.charge` would write (see the header comment).
float chargeUpCharge(const EffectInstance& e, const EffectContext& ctx) {
    State s;
    return phaseOf(e, ctx, s) ? s.charge : 0.0f;
}

} // namespace avgen::world
