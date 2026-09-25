// Portal (Effect Library Wave 3, catalog-distortion.md "Portal Distortion", WITHOUT Remote View): an
// oriented opening -- a disc or an ellipse with a noisy, swirling, emissive rim, a band of swirl
// distortion just outside it and an interior with depth.
//
// **How it is drawn.** SHELL, shading `Portal` (shaders/shell_fx.wgsl `fs_portal`), on a disc proxy
// a little larger than the opening. The rim is an analytic ring in disc space whose radius is
// perturbed by noise in its angle that turns with time; a white-hot inner edge falls off to the rim
// colour, and filaments stream round it. The interior is parallax -- three layers behind the plane,
// found along the view ray (van Dongen's interior mapping, one plane each), each turned by a
// differential swirl (faster near the middle) -- in one of three modes: Nebula (two-colour fBM
// clouds and stars), Environment (the sky cubemap, swirled, seen through the opening) or Void (black,
// sparse stars, a faint glow). The interior darkens towards the rim, which is what makes it read as
// a hole rather than a painted disc. It is composited OVER the frame and WRITES DEPTH, so what stands
// behind the portal is hidden by it, what passes through it is cut by its plane, and the fog
// composite and DF that follow treat it as the surface it is. Outside the rim nothing is drawn (the
// rim's outer glow is the bloom's), so the depth it writes is only the opening's.
//
// **Around it.** A DF Warp proxy on the disc's plane (`EffectResolve::distortion`) swirls the scene in
// an annulus just outside the rim; an EMIT system (`EffectResolve::particles`) draws motes in towards
// it; LIGHTMOD's pool gives it a light in the rim's colour, so it lights the ground it stands on.
//
// **Open and close.** `open` (0..1) is a row -- key it on the timeline -- multiplied by the
// activation's envelope, eased: with the Trigger or Window activation the fade-in is the opening and
// the fade-out the closing. Stateless: a function of the transport second, the id and the owner.
//
// **Owners.** World (a placed gate, aimed by Heading and Tilt) or an Entity (opened `ahead` metres in
// front of it, facing where it faces). Remote View -- a second camera's render in the opening -- is a
// later wave (a secondary scene pass).

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

constexpr const char* kInteriors[] = {"Nebula", "Environment", "Void"};
// The disc proxy's radius over the opening's: room for the rim, its noise and its anti-aliasing.
constexpr float kExtent = 1.3f;

constexpr EffectField kFields[] = {
    storedFloat("radius", "Radius", 2.0f, 0.05f, 500.0f, 0.2f, 12.0f).fmt("%.2f m").main().log().floorAt(0.05f)
        .tooltip("The opening's radius when fully open (its width, for an ellipse)."),
    storedFloat("open", "Open", 1.0f, 0.0f, 1.0f, 0.0f, 1.0f).main().clampTo(0.0f, 1.0f)
        .tooltip("How far open it is. Key it on the timeline, or use the Trigger or Window activation:\n"
                 "the fade-in is the opening and the fade-out the closing."),
    storedFloat("aspect", "Aspect", 1.0f, 0.2f, 5.0f, 0.4f, 2.5f).main().clampTo(0.2f, 5.0f)
        .tooltip("Height over width: 1 is round, above 1 a tall ellipse."),
    storedColor("rimColor", "Rim colour", glm::vec3(0.7f, 0.35f, 1.0f)).main().sec("Rim"),
    storedFloat("rimEmission", "Rim brightness", 2.2f, 0.0f, 100.0f, 0.0f, 8.0f).main().floorAt(0.0f)
        .tooltip("HDR brightness of the rim's hot inner edge. The rest of the rim is dimmer, so keep this\n"
                 "a few times 1 at most and let the bloom halo it."),
    storedFloat("rimWidth", "Rim width", 0.1f, 0.005f, 0.5f, 0.01f, 0.25f).sec("Rim").clampTo(0.005f, 0.5f)
        .tooltip("The rim's width, as a fraction of the radius."),
    storedFloat("rimNoise", "Rim turbulence", 0.5f, 0.0f, 1.0f, 0.0f, 1.0f).clampTo(0.0f, 1.0f)
        .tooltip("How ragged and alive the rim is: 0 is a clean ring."),
    storedFloat("swirl", "Swirl", 0.6f, -6.0f, 6.0f, -3.0f, 3.0f).main().sec("Swirl")
        .tooltip("How fast the interior and the rim's filaments turn, and how strongly the scene round\n"
                 "the rim is swirled. Negative turns the other way."),
    storedFloat("swirlBand", "Swirl band", 0.5f, 0.0f, 2.0f, 0.0f, 1.2f).sec("Swirl").clampTo(0.0f, 2.0f)
        .tooltip("How far outside the rim the scene is swirled, as a fraction of the radius."),
    storedFloat("distortion", "Swirl distortion", 1.0f, 0.0f, 6.0f, 0.0f, 3.0f).floorAt(0.0f)
        .tooltip("How strongly the scene round the rim bends. 0 is none."),
    storedChoice("interior", "Interior", 0, kInteriors).main().sec("Interior")
        .tooltip("Nebula: swirling clouds and stars. Environment: the sky, seen through the opening.\n"
                 "Void: black space with sparse stars."),
    storedColor("interiorColor", "Interior colour", glm::vec3(0.45f, 0.2f, 0.9f)).main(),
    storedColor("interiorColor2", "Second colour", glm::vec3(0.15f, 0.55f, 1.0f)).sec("Interior"),
    storedFloat("interiorBrightness", "Interior brightness", 0.6f, 0.0f, 20.0f, 0.0f, 2.0f).floorAt(0.0f)
        .tooltip("Kept below 1 the interior reads as depth rather than a lamp."),
    storedFloat("interiorDepth", "Interior depth", 6.0f, 0.0f, 500.0f, 0.0f, 30.0f).fmt("%.1f m").floorAt(0.0f)
        .tooltip("How deep the parallax runs behind the opening."),
    storedFloat("stars", "Stars", 0.5f, 0.0f, 1.0f, 0.0f, 1.0f).clampTo(0.0f, 1.0f),
    storedFloat("particleRate", "Motes", 40.0f, 0.0f, 2000.0f, 0.0f, 300.0f).fmt("%.0f /s").sec("Motes and light").floorAt(0.0f)
        .tooltip("Sparks drawn in towards the rim, per second."),
    storedFloat("light", "Light", 120.0f, 0.0f, 100000.0f, 0.0f, 1000.0f).floorAt(0.0f)
        .tooltip("A light at the opening in the rim's colour, in candela. Shares the 16-light pool."),
    storedFloat("yaw", "Heading", 0.0f, -360.0f, 360.0f, -180.0f, 180.0f).fmt("%.1f deg").sec("Placement")
        .tooltip("Which way the opening faces, about the vertical (on an entity: from where it faces)."),
    storedFloat("tilt", "Tilt", 0.0f, -90.0f, 90.0f, -90.0f, 90.0f).fmt("%.1f deg")
        .tooltip("0 stands upright; 90 lies flat, facing up."),
    storedFloat("ahead", "Ahead of owner", 2.5f, -100.0f, 100.0f, 0.0f, 10.0f).fmt("%.2f m")
        .tooltip("On an entity: how far in front of it the portal opens."),
    storedFloat("offsetX", "Offset X", 0.0f, -100000.0f, 100000.0f, -20.0f, 20.0f).fmt("%.2f m")
        .tooltip("On the World: where the portal's centre is. On an entity: an offset."),
    storedFloat("offsetY", "Offset Y", 0.0f, -100000.0f, 100000.0f, -20.0f, 20.0f).fmt("%.2f m"),
    storedFloat("offsetZ", "Offset Z", 0.0f, -100000.0f, 100000.0f, -20.0f, 20.0f).fmt("%.2f m"),
};

constexpr kinds::StoredRows kRows{"portal", kFields};

struct Look {
    const char* name;
    int interior;
    glm::vec3 rim;
    float rimEmission, rimWidth, rimNoise, swirl;
    glm::vec3 inside, inside2;
    float brightness, stars;
};

constexpr Look kLooks[] = {
    // A magic portal: violet, ragged rim, nebula clouds.
    {"Magic Portal", 0, {0.72f, 0.35f, 1.0f}, 2.2f, 0.1f, 0.6f, 0.6f, {0.5f, 0.18f, 0.9f}, {0.12f, 0.5f, 1.0f}, 0.55f, 0.5f},
    // A sci-fi gate: a clean, hard cyan rim, the sky through it, a slow turn.
    {"Sci-Fi Gate", 1, {0.25f, 0.9f, 1.0f}, 2.6f, 0.04f, 0.1f, 0.2f, {0.6f, 0.9f, 1.0f}, {0.2f, 0.5f, 0.8f}, 0.9f, 0.0f},
    // A rift to space: a dark ember rim round black space and stars.
    {"Rift to Space", 2, {1.0f, 0.3f, 0.12f}, 1.1f, 0.05f, 0.8f, 0.25f, {0.25f, 0.1f, 0.4f}, {0.05f, 0.1f, 0.3f}, 0.3f, 1.0f},
};

void applyLook(E& e, const Look& l) {
    kRows.set(e, "interior", static_cast<float>(l.interior));
    kRows.setRgb(e, "rimColor", l.rim);
    kRows.set(e, "rimEmission", l.rimEmission);
    kRows.set(e, "rimWidth", l.rimWidth);
    kRows.set(e, "rimNoise", l.rimNoise);
    kRows.set(e, "swirl", l.swirl);
    kRows.setRgb(e, "interiorColor", l.inside);
    kRows.setRgb(e, "interiorColor2", l.inside2);
    kRows.set(e, "interiorBrightness", l.brightness);
    kRows.set(e, "stars", l.stars);
    e.style = l.name;
}
void style0(E& e) { applyLook(e, kLooks[0]); }
void style1(E& e) { applyLook(e, kLooks[1]); }
void style2(E& e) { applyLook(e, kLooks[2]); }

constexpr EffectStyle kStyles[] = {{kLooks[0].name, style0}, {kLooks[1].name, style1}, {kLooks[2].name, style2}};

// The catalogue's modulation: the bass flares the rim.
constexpr EffectRoute kRoutes[] = {
    {"audio.bass", "rimEmission", 1.2f, 15.0f, 300.0f},
};

E make(std::string name) {
    E e;
    e.name = std::move(name);
    e.kind = EffectKind::Portal;
    e.activation = Activation::Always;
    e.timing = Timing{};
    e.timing.fadeIn = 1.2;
    e.timing.fadeOut = 0.8;
    applyLook(e, kLooks[0]);
    return e;
}

// ---- the one state every hook reads ---------------------------------------------------------------

struct State {
    glm::vec3 centre{0.0f};
    glm::vec3 normal{0.0f, 0.0f, 1.0f};
    glm::vec3 right{1.0f, 0.0f, 0.0f};
    glm::vec3 up{0.0f, 1.0f, 0.0f};
    float radius = 1.0f;
    float aspect = 1.0f;
    float open = 1.0f;     // the opening's radius over the full radius
    float envelope = 1.0f;
};

bool stateOf(const E& e, const EffectContext& ctx, State& out) {
    out = State{};
    kinds::FxLive live;
    if (e.owner.kind == EffectTarget::Light || !kinds::fxLive(e, ctx, live)) {
        return false;
    }
    const glm::vec3 offset(kRows.f(e, "offsetX"), kRows.f(e, "offsetY"), kRows.f(e, "offsetZ"));
    kinds::FxAnchor anchor;
    if (!kinds::fxAnchor(e, ctx, offset, anchor)) {
        return false;
    }
    const float env = live.envelope;
    out.envelope = env;
    out.open = std::clamp(kRows.f(e, "open"), 0.0f, 1.0f) * kinds::smooth01(0.0f, 1.0f, env);
    if (!(out.open > 1e-3f)) {
        return false;
    }
    out.radius = std::max(kRows.f(e, "radius"), 0.05f);
    out.aspect = std::clamp(kRows.f(e, "aspect"), 0.2f, 5.0f);
    float yaw = kRows.f(e, "yaw");
    out.centre = anchor.centre;
    if (anchor.entity) {
        yaw += glm::degrees(std::atan2(anchor.forward.x, anchor.forward.z));
        const glm::vec3 flat(anchor.forward.x, 0.0f, anchor.forward.z);
        const glm::vec3 ahead = glm::length(flat) > 1e-4f ? glm::normalize(flat) : glm::vec3(0.0f, 0.0f, -1.0f);
        out.centre += ahead * kRows.f(e, "ahead");
    }
    out.normal = kinds::fxHeading(yaw, kRows.f(e, "tilt"), false);
    kinds::fxPlaneAxes(out.normal, out.right, out.up);
    return true;
}

std::size_t records(const E& e, const EffectContext& ctx) {
    State s;
    return stateOf(e, ctx, s) ? 1u : 0u;
}

EffectStatus emit(const E& e, const EffectContext& ctx, ShellSink& sink, std::string& reason) {
    const bool lost = !reason.empty();
    State s;
    if (!stateOf(e, ctx, s)) {
        return kinds::shellDormant(e, ctx, reason);
    }
    const float ext = s.radius * kExtent;
    ShellInstance shell;
    shell.model = kinds::fxModel(s.centre, s.right * ext, s.normal * ext, s.up * (ext * s.aspect));
    const glm::vec3 rim = kRows.rgb(e, "rimColor");
    shell.params[0] = glm::vec4(rim, std::max(kRows.f(e, "rimEmission"), 0.0f));
    shell.params[1] = glm::vec4(kinds::seedOf(e.id), static_cast<float>(ctx.seconds), 0.0f, 0.0f);
    shell.params[2] = glm::vec4(kRows.rgb(e, "interiorColor"), std::max(kRows.f(e, "interiorBrightness"), 0.0f));
    shell.params[3] = glm::vec4(s.open, std::clamp(kRows.f(e, "rimWidth"), 0.005f, 0.5f),
                                std::clamp(kRows.f(e, "rimNoise"), 0.0f, 1.0f), kRows.f(e, "swirl"));
    shell.params[4] = glm::vec4(static_cast<float>(kRows.choice(e, "interior")), std::max(kRows.f(e, "interiorDepth"), 0.0f),
                                1.0f / kExtent, s.aspect);
    shell.params[5] = glm::vec4(kRows.rgb(e, "interiorColor2"), std::clamp(kRows.f(e, "stars"), 0.0f, 1.0f));
    shell.params[6] = glm::vec4(s.radius, s.envelope, 0.0f, 0.0f);
    if (!sink.append(ShellShading::Portal, ShellMesh::Disc, shell)) {
        reason = "The shell budget (128) is full this frame: not drawn.";
        return EffectStatus::Dropped;
    }
    const float light = std::max(kRows.f(e, "light"), 0.0f) * s.open;
    if (light > 0.0f) {
        glm::vec3 hue = rim;
        hue /= std::max({hue.r, hue.g, hue.b, 1e-4f});
        EffectLight l;
        l.position = s.centre + s.normal * (0.15f * s.radius);
        l.intensity = light;
        l.color = hue;
        l.range = 6.0f * s.radius;
        l.volumetric = 0.4f;
        sink.requestLight(l);
    }
    return kinds::fxDrawn(lost);
}

// DF: the scene swirled in an annulus just outside the rim, in the portal's plane.
std::size_t distortion(const E& e, const EffectContext& ctx, std::span<DistortionProxy> out) {
    State s;
    const float strength = std::max(kRows.f(e, "distortion"), 0.0f);
    const float band = std::clamp(kRows.f(e, "swirlBand"), 0.0f, 2.0f);
    if (out.empty() || strength <= 0.0f || band <= 0.0f || !stateOf(e, ctx, s)) {
        return 0;
    }
    const float swirl = kRows.f(e, "swirl");
    const float outer = s.radius * (s.open + band);
    DistortionProxy p = kinds::fxWarpProxy(DistortionShape::Disc);
    p.centre = glm::vec4(s.centre, 0.1f * s.radius); // behind the rim: the rim is never smeared
    p.axis0 = glm::vec4(s.right * outer, p.axis0.w);
    p.axis1 = glm::vec4(s.up * (outer * s.aspect), p.axis1.w);
    p.axis2 = glm::vec4(s.normal * (0.1f * s.radius), 0.5f * s.radius);
    // A turn (swirl) with a little pull towards the opening; the sign of the swirl row is the sense.
    const float sense = swirl < 0.0f ? -1.0f : 1.0f;
    p.terms = glm::vec4(0.35f, 0.0f, sense, strength * 0.08f * s.radius * std::min(std::abs(swirl) + 0.3f, 2.0f) * s.open);
    p.shape = glm::vec4(1.0f, 0.35f, 0.25f, 2.0f);
    p.noise = glm::vec4(static_cast<float>(ctx.seconds) * 0.4f, 0.12f, 0.0f, kinds::seedOf(e.id));
    p.rim = glm::vec4(0.0f, 0.0f, 0.0f, std::clamp(s.open / (s.open + band), 0.0f, 0.95f));
    out[0] = p;
    return 1;
}

// EMIT: motes drawn in towards the rim, swirling.
bool particles(const E& e, const EffectContext& ctx, scene::ParticleSystem& p) {
    p.capacity = 4096;
    p.shape = scene::EmitterShape::Sphere;
    p.spawnRate = 0.0f;
    p.burst = 0.0f;
    p.spread = 1.0f;
    p.speedMin = 0.05f;
    p.speedMax = 0.3f;
    p.gravity = glm::vec3(0.0f);
    p.drag = 0.9f;
    p.turbulence = 0.4f;
    p.turbulenceScale = 0.6f;
    p.turbulenceSpeed = 0.4f;
    p.lifetimeMin = 1.2f;
    p.lifetimeMax = 2.4f;
    p.sizeStart = 0.035f;
    p.sizeEnd = 0.0f;
    p.opacityCurve.keys = {{0.0f, 0.0f}, {0.2f, 1.0f}, {0.8f, 0.8f}, {1.0f, 0.0f}};
    p.blend = scene::ParticleBlend::Additive;
    p.velocityStretch = 0.5f;
    p.stretchMax = 0.15f;
    p.softness = 0.3f;
    p.fogCoupling = 1.0f;
    State s;
    if (!stateOf(e, ctx, s)) {
        return false;
    }
    const glm::vec3 rim = kRows.rgb(e, "rimColor");
    p.colorStart = glm::vec4(glm::mix(rim, glm::vec3(1.0f), 0.4f), 1.0f);
    p.colorEnd = glm::vec4(rim, 0.0f);
    p.emissive = 2.0f;
    p.position = s.centre;
    p.extent = glm::vec3(s.radius * (s.open + 0.8f));
    p.attractorPosition = s.centre;
    p.attractorStrength = 1.2f * s.radius;
    p.attractorRadius = s.radius * 2.5f;
    p.orbit = 1.5f * kRows.f(e, "swirl") * s.radius;
    p.spawnRate = std::max(kRows.f(e, "particleRate"), 0.0f) * s.open;
    p.seed = static_cast<std::uint32_t>(kinds::seedOf(e.id)) + 29u;
    return true;
}

EffectSchema buildSchema() {
    EffectSchema s;
    s.kind = EffectKind::Portal;
    s.key = "portal";
    s.enumName = "Portal";
    s.displayName = "Portal";
    s.description = "An opening in the world: a ragged, swirling, glowing rim round an interior with depth "
                    "-- nebula clouds, the sky, or black space -- that hides what is behind it, with the scene "
                    "swirled just outside the rim and motes drawn in. It opens and closes with its activation "
                    "or a keyed Open.";
    s.performance = PerformanceClass::Medium;
    s.primaryCost = CostFragment;
    s.addLabel = "Portal";
    s.addTip = "A swirling opening placed in the world, or opened in front of this entity.";
    s.targets = targetBit(EffectTarget::World) | targetBit(EffectTarget::Entity);
    s.category = EffectCategory::Distortion;
    s.stage = RenderStage::Particles;
    s.fields = kFields;
    s.styles = kStyles;
    s.routes = kRoutes;
    s.beatLeaf = "swirl";
    s.factory = make;
    s.resolve.bucket = EffectBucket::Shell;
    s.resolve.records = records;
    s.resolve.distortion = distortion;
    s.resolve.particles = particles;
    registerShellProducer(EffectKind::Portal, ShellProducer{emit});
    return s;
}

} // namespace

const EffectSchema& portalSchema() {
    static const EffectSchema kSchema = buildSchema();
    return kSchema;
}

} // namespace avgen::world
