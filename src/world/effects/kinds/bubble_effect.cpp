// Bubble (Effect Library Wave 3, catalog-distortion.md "Bubble"): a thin refracting, reflecting
// spherical film -- a soap bubble, a protective orb, a water orb.
//
// **How it is drawn.** SHELL, shading `Bubble` (shaders/shell_fx.wgsl): a sphere whose own vertex
// stage wobbles it with two low-order shape modes (a stateless function of the transport clock). The
// fragment stage is a thin film: the film's thickness is a base plus drifting fBM, thicker towards the
// bottom as it drains (a real bubble's top thins to black just before it pops); its interference
// colour is the two-beam reflectance 1 - cos(4 pi n d cos(theta_t) / lambda) at three wavelengths; the
// Fresnel term (Schlick) makes it nearly invisible face-on and bright at the rim; what it reflects is
// the environment cubemap. It is composited OVER the frame, premultiplied, with alpha = its
// reflectance, so it transmits exactly what it does not reflect -- the physically right blend for a
// film, and why it can never glow on its own. Both faces draw (the far side dimmer). Where it meets
// the ground it fades out over a few centimetres of linear depth.
//
// **Refraction.** A DF Warp proxy on the bubble (`EffectResolve::distortion`): a lensing band at the
// rim only (the field is zero inside 55 % of the radius), behind the bubble -- what is inside it (its
// owner) stays crisp. Refraction 0 is none; the Water Orb look is strong.
//
// **The pop (TRIGGER).** With the Trigger activation each event POPS the bubble rather than showing
// it: a hole opens from a point (a direction hashed from the id and the event's own time, so a scrub
// pops where a play pops) and races round the sphere in `popTime` seconds behind a bright receding
// rim, a burst of droplets (EMIT) flies off, and after `reform` seconds a new bubble inflates. Before
// the first event the bubble is whole. Every other activation shows the bubble while it is active,
// inflating and deflating with the timing's fades.
//
// **Owners.** An Entity (it encases the owner: at least its bounding radius) or the World (placed).

#include "scene/particles.hpp"
#include "world/effects/effect_registry.hpp"
#include "world/effects/kinds/shell_fx_kind.hpp"

#include <glm/glm.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace avgen::world {
namespace {

using E = EffectInstance;

constexpr EffectField kFields[] = {
    storedFloat("radius", "Radius", 1.2f, 0.02f, 500.0f, 0.1f, 12.0f).fmt("%.2f m").main().log().floorAt(0.02f)
        .tooltip("The bubble's radius. On an entity it is at least the owner's bounding radius."),
    storedFloat("thickness", "Film thickness", 380.0f, 0.0f, 3000.0f, 50.0f, 1200.0f).fmt("%.0f nm").main().floorAt(0.0f)
        .tooltip("The film's mean thickness. 250-600 nm gives the saturated magentas, greens and golds\n"
                 "of a soap bubble; very thin is black, very thick washes to white."),
    storedFloat("variation", "Thickness variation", 220.0f, 0.0f, 2000.0f, 0.0f, 800.0f).fmt("%.0f nm").main().floorAt(0.0f)
        .tooltip("How much the thickness swirls across the film: the width of the colour bands."),
    storedFloat("iridescence", "Iridescence", 0.85f, 0.0f, 1.0f, 0.0f, 1.0f).main().clampTo(0.0f, 1.0f)
        .tooltip("How strongly the film's colours show: 0 is a clear glass-like reflection."),
    storedFloat("reflectivity", "Reflection", 1.0f, 0.0f, 4.0f, 0.0f, 2.0f).main().floorAt(0.0f)
        .tooltip("How much of the sky the film reflects (1 is a real film)."),
    storedFloat("refraction", "Refraction", 0.1f, 0.0f, 4.0f, 0.0f, 2.0f).main().floorAt(0.0f)
        .tooltip("How much the rim bends what is behind the bubble. A soap film barely does; a water orb\n"
                 "strongly."),
    storedColor("tint", "Tint", glm::vec3(1.0f)).main().sec("Body"),
    storedFloat("tintOpacity", "Tint opacity", 0.0f, 0.0f, 1.0f, 0.0f, 0.6f).sec("Body").clampTo(0.0f, 1.0f)
        .tooltip("A body colour seen through the bubble: 0 for a soap film, higher for a water orb or a\n"
                 "tinted protective bubble."),
    storedFloat("wobbleAmount", "Wobble", 0.03f, 0.0f, 0.3f, 0.0f, 0.12f).sec("Motion").clampTo(0.0f, 0.3f)
        .tooltip("How much the bubble's shape wobbles, as a fraction of its radius."),
    storedFloat("wobbleSpeed", "Wobble speed", 0.6f, 0.0f, 10.0f, 0.0f, 3.0f).fmt("%.2f Hz").floorAt(0.0f),
    storedFloat("drainage", "Film flow", 0.12f, 0.0f, 3.0f, 0.0f, 1.0f).floorAt(0.0f)
        .tooltip("How fast the colour bands swirl and drain down the film."),
    storedFloat("popTime", "Pop time", 0.16f, 0.02f, 3.0f, 0.05f, 1.0f).fmt("%.2f s").sec("Pop (Trigger activation)").clampTo(0.02f, 3.0f)
        .tooltip("With the Trigger activation each event pops the bubble: how long the hole takes to\n"
                 "race round it."),
    storedFloat("reform", "Re-form after", 1.2f, 0.0f, 60.0f, 0.0f, 6.0f).fmt("%.2f s").floorAt(0.0f)
        .tooltip("How long after a pop a new bubble inflates."),
    storedFloat("droplets", "Droplets", 80.0f, 0.0f, 4000.0f, 0.0f, 400.0f).fmt("%.0f").floorAt(0.0f)
        .tooltip("Droplets thrown off by each pop."),
    storedFloat("edgeFade", "Soft contact", 0.15f, 0.0f, 5.0f, 0.0f, 1.0f).fmt("%.2f m").sec("Advanced").floorAt(0.0f)
        .tooltip("Where the bubble meets the ground it fades over this distance (needs the depth prepass)."),
    storedFloat("inside", "Far side", 0.6f, 0.0f, 1.0f, 0.0f, 1.0f).clampTo(0.0f, 1.0f)
        .tooltip("How much the inside of the far side of the film shows."),
    storedFloat("offsetX", "Offset X", 0.0f, -100000.0f, 100000.0f, -20.0f, 20.0f).fmt("%.2f m").sec("Placement")
        .tooltip("On the World: where the bubble is. On an entity: an offset from its centre."),
    storedFloat("offsetY", "Offset Y", 0.0f, -100000.0f, 100000.0f, -20.0f, 20.0f).fmt("%.2f m"),
    storedFloat("offsetZ", "Offset Z", 0.0f, -100000.0f, 100000.0f, -20.0f, 20.0f).fmt("%.2f m"),
};

constexpr kinds::StoredRows kRows{"bubble", kFields};

struct Look {
    const char* name;
    float thickness, variation, iridescence, reflectivity, refraction;
    glm::vec3 tint;
    float tintOpacity, wobble;
};

constexpr Look kLooks[] = {
    // A soap bubble: saturated interference bands, nearly clear face-on, a slight rim lens.
    {"Soap Bubble", 380.0f, 220.0f, 0.85f, 1.4f, 0.1f, {1.0f, 1.0f, 1.0f}, 0.0f, 0.03f},
    // A protective bubble: a thicker, paler film, a faint cyan body, steadier.
    {"Protective Bubble", 700.0f, 260.0f, 0.55f, 1.2f, 0.15f, {0.45f, 0.85f, 1.0f}, 0.1f, 0.012f},
    // A water orb: little interference, a blue body and strong refraction.
    {"Water Orb", 1500.0f, 200.0f, 0.1f, 1.0f, 1.2f, {0.3f, 0.6f, 0.95f}, 0.28f, 0.04f},
};

void applyLook(E& e, const Look& l) {
    kRows.set(e, "thickness", l.thickness);
    kRows.set(e, "variation", l.variation);
    kRows.set(e, "iridescence", l.iridescence);
    kRows.set(e, "reflectivity", l.reflectivity);
    kRows.set(e, "refraction", l.refraction);
    kRows.setRgb(e, "tint", l.tint);
    kRows.set(e, "tintOpacity", l.tintOpacity);
    kRows.set(e, "wobbleAmount", l.wobble);
    e.style = l.name;
}
void style0(E& e) { applyLook(e, kLooks[0]); }
void style1(E& e) { applyLook(e, kLooks[1]); }
void style2(E& e) { applyLook(e, kLooks[2]); }

constexpr EffectStyle kStyles[] = {{kLooks[0].name, style0}, {kLooks[1].name, style1}, {kLooks[2].name, style2}};

// The catalogue's modulation: the mids wobble it.
constexpr EffectRoute kRoutes[] = {
    {"audio.mid", "wobbleAmount", 0.04f, 20.0f, 350.0f},
};

E make(std::string name) {
    E e;
    e.name = std::move(name);
    e.kind = EffectKind::Bubble;
    e.activation = Activation::Always;
    e.timing = Timing{};
    e.timing.fadeIn = 0.5;
    e.timing.fadeOut = 0.5;
    applyLook(e, kLooks[0]);
    return e;
}

// ---- the one state every hook reads ---------------------------------------------------------------

struct State {
    glm::vec3 centre{0.0f};
    float radius = 1.0f;
    float inflate = 1.0f;  // the scale the bubble is drawn at (inflating, and the fades)
    float opacity = 1.0f;  // the envelope
    float pop = 0.0f;      // 0 whole; (0, 1] the hole's progress round the sphere
    glm::vec3 popDir{0.0f, 1.0f, 0.0f};
    double popAt = -1.0;   // the latest pop's instant (-1: none)
};

// False: nothing to draw this frame (dormant, popped and not yet re-formed, owner not drawn).
bool stateOf(const E& e, const EffectContext& ctx, State& out) {
    out = State{};
    if (!e.enabled) {
        return false;
    }
    const glm::vec3 offset(kRows.f(e, "offsetX"), kRows.f(e, "offsetY"), kRows.f(e, "offsetZ"));
    kinds::FxAnchor anchor;
    if (e.owner.kind == EffectTarget::Light || !kinds::fxAnchor(e, ctx, offset, anchor)) {
        return false;
    }
    out.centre = anchor.centre;
    out.radius = std::max(kRows.f(e, "radius"), 0.02f);
    if (anchor.entity) {
        out.radius = std::max(out.radius, glm::length(anchor.halfExtents) * 1.05f);
    }
    if (e.activation == Activation::Trigger) {
        // Each event pops the bubble; before the first it is whole.
        if (ctx.triggers == nullptr) {
            return true;
        }
        std::array<double, 1> last{};
        const std::string_view owner = anchor.entity ? std::string_view(e.owner.name) : std::string_view();
        if (ctx.triggers->lastTriggers(e.timing.trigger, owner, ctx.seconds, last) == 0) {
            return true;
        }
        const double age = ctx.seconds - last[0];
        const double popTime = std::clamp(static_cast<double>(kRows.f(e, "popTime")), 0.02, 3.0);
        const double reform = std::max(static_cast<double>(kRows.f(e, "reform")), 0.0);
        constexpr double kInflate = 0.45;
        out.popAt = last[0];
        out.popDir = shellHitDirection(kinds::seedOf(e.id), last[0]);
        if (age < popTime) {
            out.pop = static_cast<float>(std::max(age / popTime, 1e-3));
            return true;
        }
        if (age < popTime + reform) {
            return false; // popped, not yet re-formed
        }
        const float grow = static_cast<float>(std::min((age - popTime - reform) / kInflate, 1.0));
        // Inflating with a slight overshoot, as a film blown out of a ring does.
        out.inflate = grow >= 1.0f ? 1.0f : std::max(1e-3f, grow * (1.0f + 0.25f * std::sin(grow * 3.14159265f)));
        return true;
    }
    kinds::FxLive live;
    if (!kinds::fxLive(e, ctx, live)) {
        return false;
    }
    out.opacity = live.envelope;
    out.inflate = 0.6f + 0.4f * live.envelope;
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
        if (e.enabled && e.activation == Activation::Trigger && e.owner.kind != EffectTarget::Light) {
            reason = "Popped: a new bubble inflates when its Re-form time has passed.";
            return EffectStatus::Dormant;
        }
        return kinds::shellDormant(e, ctx, reason);
    }
    ShellInstance shell;
    const float r = s.radius * s.inflate;
    shell.model = kinds::fxModel(s.centre, glm::vec3(r, 0.0f, 0.0f), glm::vec3(0.0f, r, 0.0f), glm::vec3(0.0f, 0.0f, r));
    shell.params[0] = glm::vec4(kRows.rgb(e, "tint"), std::max(kRows.f(e, "reflectivity"), 0.0f) * s.opacity);
    shell.params[1] = glm::vec4(kinds::seedOf(e.id), static_cast<float>(ctx.seconds), 0.0f, 0.0f);
    shell.params[2] = glm::vec4(std::max(kRows.f(e, "thickness"), 0.0f), std::max(kRows.f(e, "variation"), 0.0f),
                                std::clamp(kRows.f(e, "iridescence"), 0.0f, 1.0f), std::max(kRows.f(e, "drainage"), 0.0f));
    shell.params[3] = glm::vec4(std::clamp(kRows.f(e, "wobbleAmount"), 0.0f, 0.3f), std::max(kRows.f(e, "wobbleSpeed"), 0.0f),
                                s.pop, s.opacity);
    shell.params[4] = glm::vec4(s.popDir, std::clamp(kRows.f(e, "tintOpacity"), 0.0f, 1.0f) * s.opacity);
    shell.params[5] = glm::vec4(0.0f, std::clamp(kRows.f(e, "inside"), 0.0f, 1.0f), std::max(kRows.f(e, "edgeFade"), 0.0f), 0.0f);
    if (!sink.append(ShellShading::Bubble, ShellMesh::Sphere, shell)) {
        reason = "The shell budget (128) is full this frame: not drawn.";
        return EffectStatus::Dropped;
    }
    if (kRows.f(e, "edgeFade") > 0.0f) {
        sink.usesDepth();
    }
    return kinds::fxDrawn(lost);
}

// DF: lensing at the rim, behind the bubble. Gone while it pops.
std::size_t distortion(const E& e, const EffectContext& ctx, std::span<DistortionProxy> out) {
    State s;
    const float refraction = std::max(kRows.f(e, "refraction"), 0.0f);
    if (out.empty() || refraction <= 0.0f || !stateOf(e, ctx, s) || s.pop > 0.0f) {
        return 0;
    }
    const float r = s.radius * s.inflate;
    DistortionProxy p = kinds::fxWarpProxy(DistortionShape::Ellipsoid);
    p.centre = glm::vec4(s.centre, r); // the lens plane at the bubble's back: its inside stays crisp
    p.axis0.x = r;
    p.axis1.y = r;
    p.axis2 = glm::vec4(0.0f, 0.0f, r, 0.5f * r);
    p.terms = glm::vec4(1.0f, 0.0f, 0.0f, refraction * 0.05f * r * s.opacity);
    p.shape = glm::vec4(1.2f, 0.12f, 0.0f, 0.0f);
    p.noise = glm::vec4(0.0f, std::min(0.1f * refraction, 0.5f), 0.0f, kinds::seedOf(e.id));
    p.rim = glm::vec4(0.0f, 0.0f, 0.0f, 0.55f);
    out[0] = p;
    return 1;
}

// EMIT: droplets thrown off by a pop, on the frame it starts.
bool particles(const E& e, const EffectContext& ctx, scene::ParticleSystem& p) {
    p.capacity = 2048;
    p.shape = scene::EmitterShape::Sphere;
    p.spawnRate = 0.0f;
    p.burst = 0.0f;
    p.spread = 1.0f;
    p.gravity = glm::vec3(0.0f, -3.0f, 0.0f);
    p.drag = 0.6f;
    p.turbulence = 0.2f;
    p.lifetimeMin = 0.5f;
    p.lifetimeMax = 1.1f;
    p.sizeStart = 0.018f;
    p.sizeEnd = 0.006f;
    p.colorStart = glm::vec4(0.85f, 0.92f, 1.0f, 0.9f);
    p.colorEnd = glm::vec4(0.7f, 0.85f, 1.0f, 0.0f);
    p.emissive = 1.2f;
    p.blend = scene::ParticleBlend::Additive;
    p.velocityStretch = 0.6f;
    p.stretchMax = 0.08f;
    p.fogCoupling = 1.0f;
    State s;
    if (!stateOf(e, ctx, s)) {
        return false;
    }
    const float r = s.radius * s.inflate;
    p.position = s.centre;
    p.extent = glm::vec3(r);
    p.direction = glm::vec3(0.0f, 1.0f, 0.0f);
    p.speedMin = 0.8f * std::sqrt(r);
    p.speedMax = 2.4f * std::sqrt(r);
    p.seed = static_cast<std::uint32_t>(kinds::seedOf(e.id)) + 11u;
    if (s.popAt >= 0.0 && kinds::fxEdge(s.popAt, ctx)) {
        p.burst = std::max(kRows.f(e, "droplets"), 0.0f);
    }
    return true;
}

EffectSchema buildSchema() {
    EffectSchema s;
    s.kind = EffectKind::Bubble;
    s.key = "bubble";
    s.enumName = "Bubble";
    s.displayName = "Bubble";
    s.description = "A thin refracting, reflecting film -- a soap bubble, a protective orb, a water orb: "
                    "nearly invisible face-on, swirling interference colours and a reflection of the sky "
                    "towards its rim, a slight lens at its edge and a slow wobble. With the Trigger "
                    "activation each event pops it and a new one inflates.";
    s.performance = PerformanceClass::Medium;
    s.primaryCost = CostFragment;
    s.addLabel = "Bubble";
    s.addTip = "A soap bubble or a water orb round this entity, or placed in the world.";
    s.targets = targetBit(EffectTarget::Entity) | targetBit(EffectTarget::World);
    s.category = EffectCategory::Distortion;
    s.stage = RenderStage::Particles;
    s.fields = kFields;
    s.styles = kStyles;
    s.routes = kRoutes;
    s.beatLeaf = "wobbleAmount";
    s.factory = make;
    s.resolve.bucket = EffectBucket::Shell;
    s.resolve.records = records;
    s.resolve.distortion = distortion;
    s.resolve.particles = particles;
    registerShellProducer(EffectKind::Bubble, ShellProducer{emit});
    return s;
}

} // namespace

const EffectSchema& bubbleSchema() {
    static const EffectSchema kSchema = buildSchema();
    return kSchema;
}

} // namespace avgen::world
