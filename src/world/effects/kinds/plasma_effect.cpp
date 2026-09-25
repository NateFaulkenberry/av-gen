// Plasma (Effect Library Wave 3, catalog-energy.md "Plasma"): a turbulent glowing volume -- a ball of
// plasma, a fireball's core, an energy orb. It emits light and is not lit.
//
// **How it is drawn.** SHELL: a sphere proxy (world/effects/shell_frame) whose fragment stage marches
// the ray's analytic interval through the sphere -- entered at the front face, left where the sphere
// or the scene's linear depth ends it, whichever is nearer (shaders/shell.wgsl `fs_plasma`). Ridged fBM
// strands that run outward from the centre, through a divergence-free warp that boils in place, both
// emit and veil what is behind them, so a pixel's strand light saturates at the strand colour and the
// gaps stay dark; a bounded hot core (its peak IS the core brightness) glows through them, and the limb
// has a thin rim. Additive. Only the core writes the emission target at full weight, so the bloom
// haloes the orb without washing its strands out. Fog in front of it dims it at the shell's surface.
//
// **Light.** A LIGHTMOD pool light at the orb's centre, in the orb's colour, so it lights what it
// floats over. The pool is shared with the entity glows and is 16 slots; a plasma whose light does
// not fit still draws (`Partial`, with the reason).
//
// **Determinism.** Stateless: a function of the transport second (the boil is `seconds * speed`),
// the instance's id (the seed) and where its owner is drawn this frame. Activation and timing are
// honoured through the envelope, which scales the emission and the light together.
//
// **Owners.** An Entity (the orb rides the centre of its owner's bounds, plus an offset) or the World
// (placed at the offset). A camera inside the orb sees no front face and draws nothing -- orbs are
// looked at, not flown through; that limit is the price of marching from the front face, which is
// what keeps it depth-tested against the scene when the depth prepass is off.

#include "world/effects/effect_registry.hpp"
#include "world/effects/kinds/shell_kind.hpp"
#include "world/effects/shell_frame.hpp"

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <string>

namespace avgen::world {
namespace {

using E = EffectInstance;

constexpr EffectField kFields[] = {
    // ---- Main ------------------------------------------------------------------------------------
    storedFloat("radius", "Radius", 1.2f, 0.02f, 500.0f, 0.1f, 12.0f).fmt("%.2f m").main().log().floorAt(0.02f)
        .tooltip("The orb's radius in metres."),
    storedColor("coreColor", "Core colour", glm::vec3(1.0f, 0.93f, 0.8f)).main().sec("Glow"),
    storedFloat("coreIntensity", "Core brightness", 3.0f, 0.0f, 500.0f, 0.0f, 20.0f).main().floorAt(0.0f)
        .tooltip("HDR brightness at the very centre of the orb. Only the core should pass about 1:\n"
                 "it is what blooms, and it is seen through the strands in front of it."),
    storedColor("edgeColor", "Strand colour", glm::vec3(0.3f, 0.4f, 1.0f)).main(),
    storedFloat("edgeIntensity", "Strand brightness", 0.9f, 0.0f, 100.0f, 0.0f, 4.0f).main().floorAt(0.0f)
        .tooltip("Brightness where the swirling strands are dense. Kept near or below 1 the strands\n"
                 "read as structure; far above it they blow out into a flat glow."),
    storedFloat("rim", "Limb glow", 0.7f, 0.0f, 20.0f, 0.0f, 3.0f).main().floorAt(0.0f)
        .tooltip("A thin brighter edge where the view grazes the orb."),
    storedFloat("turbulence", "Turbulence", 0.8f, 0.0f, 6.0f, 0.0f, 3.0f).main().sec("Motion").floorAt(0.0f)
        .tooltip("How strongly the flow twists the filaments. 0 is a calm, marbled orb."),
    storedFloat("scale", "Filament scale", 2.2f, 0.1f, 20.0f, 0.5f, 8.0f).main().floorAt(0.1f)
        .tooltip("How many filaments fit across the orb: larger is finer."),
    storedFloat("speed", "Boil speed", 0.6f, 0.0f, 10.0f, 0.0f, 3.0f).main().floorAt(0.0f)
        .tooltip("How fast the plasma churns."),
    storedFloat("light", "Light", 60.0f, 0.0f, 100000.0f, 0.0f, 600.0f).main().sec("Light").floorAt(0.0f)
        .tooltip("A real point light at the orb's centre, in candela, so it lights what is around it.\n"
                 "0 is none. Shares the 16-light effect pool with entity glows."),
    // ---- Advanced --------------------------------------------------------------------------------
    storedFloat("steps", "Quality (steps)", 20.0f, 4.0f, 48.0f, 8.0f, 32.0f).fmt("%.0f").sec("Shape").clampTo(4.0f, 48.0f)
        .tooltip("March steps through the orb. More is smoother and costs fragment time in\n"
                 "proportion to the orb's size on screen."),
    storedFloat("limbSoftness", "Limb softness", 0.35f, 0.02f, 1.0f, 0.05f, 1.0f).clampTo(0.02f, 1.0f)
        .tooltip("How gradually the orb fades at its edge: small is a crisp ball, 1 a diffuse glow."),
    storedFloat("coreSize", "Core size", 0.18f, 0.02f, 1.0f, 0.05f, 0.8f).clampTo(0.02f, 1.0f)
        .tooltip("The hot core's size, as a fraction of the radius."),
    storedFloat("filaments", "Strand sharpness", 0.45f, 0.0f, 0.95f, 0.0f, 0.9f).clampTo(0.0f, 0.95f)
        .tooltip("How thin the strands are: 0 is a soft marbled glow, higher leaves fine crackling\n"
                 "strands with dark gaps between them."),
    storedFloat("density", "Strand opacity", 1.0f, 0.0f, 8.0f, 0.0f, 3.0f).floorAt(0.0f)
        .tooltip("How much the strands hide what is behind them, the core included: higher is a\n"
                 "denser, more solid-looking orb; lower is a see-through web."),
    storedFloat("lightRange", "Light reach", 8.0f, 1.0f, 60.0f, 2.0f, 20.0f).fmt("%.1f x radius").sec("Light").floorAt(1.0f)
        .tooltip("How far the light reaches, in orb radii."),
    storedFloat("lightFog", "Light in fog", 0.35f, 0.0f, 1.0f, 0.0f, 1.0f).clampTo(0.0f, 1.0f)
        .tooltip("How much the light shows in the volumetric fog."),
    storedFloat("offsetX", "Offset X", 0.0f, -100000.0f, 100000.0f, -20.0f, 20.0f).fmt("%.2f m").sec("Placement")
        .tooltip("On the World: where the orb is. On an entity: an offset from the centre of the\n"
                 "owner's bounds, in world metres."),
    storedFloat("offsetY", "Offset Y", 0.0f, -100000.0f, 100000.0f, -20.0f, 20.0f).fmt("%.2f m"),
    storedFloat("offsetZ", "Offset Z", 0.0f, -100000.0f, 100000.0f, -20.0f, 20.0f).fmt("%.2f m"),
};

constexpr kinds::StoredRows kRows{"plasma", kFields};

// ---- styles ---------------------------------------------------------------------------------------

struct Look {
    const char* name;
    glm::vec3 core;
    float coreIntensity;
    glm::vec3 edge;
    float edgeIntensity, turbulence, scale, speed, light, limb, coreSize, filaments, density, rim;
};

// Every look writes every look row (radius and placement are the author's), so switching looks never
// leaves the last one's settings behind.
constexpr Look kLooks[] = {
    // A contained ball of plasma: a white-hot heart inside a web of blue-violet strands, a slow boil.
    {"Plasma Ball", {1.0f, 0.93f, 0.85f}, 3.0f, {0.3f, 0.4f, 1.0f}, 0.9f, 0.8f, 2.2f, 0.6f, 60.0f, 0.35f, 0.18f, 0.45f, 1.0f, 0.7f},
    // A fireball's core: yellow-white inside dense orange strands, turbulent and fast, a big warm light.
    {"Fireball Core", {1.0f, 0.85f, 0.5f}, 3.5f, {1.0f, 0.32f, 0.05f}, 1.0f, 1.6f, 1.6f, 1.4f, 180.0f, 0.5f, 0.25f, 0.3f, 1.4f, 0.5f},
    // Ball lightning: small and cyan-white, fine crackling strands, a sharp bright limb.
    {"Ball Lightning", {0.85f, 0.97f, 1.0f}, 4.0f, {0.2f, 0.75f, 1.0f}, 0.9f, 2.2f, 2.4f, 2.2f, 90.0f, 0.2f, 0.16f, 0.75f, 0.65f, 0.9f},
    // An arcane orb: magenta strands round a pale violet core, calm and soft.
    {"Arcane Orb", {0.95f, 0.8f, 1.0f}, 2.5f, {0.75f, 0.15f, 1.0f}, 0.85f, 0.5f, 1.8f, 0.35f, 40.0f, 0.6f, 0.2f, 0.4f, 1.0f, 0.7f},
};

void applyLook(E& e, const Look& l) {
    kRows.setRgb(e, "coreColor", l.core);
    kRows.set(e, "coreIntensity", l.coreIntensity);
    kRows.setRgb(e, "edgeColor", l.edge);
    kRows.set(e, "edgeIntensity", l.edgeIntensity);
    kRows.set(e, "turbulence", l.turbulence);
    kRows.set(e, "scale", l.scale);
    kRows.set(e, "speed", l.speed);
    kRows.set(e, "light", l.light);
    kRows.set(e, "limbSoftness", l.limb);
    kRows.set(e, "coreSize", l.coreSize);
    kRows.set(e, "filaments", l.filaments);
    kRows.set(e, "density", l.density);
    kRows.set(e, "rim", l.rim);
    e.style = l.name;
}
void style0(E& e) { applyLook(e, kLooks[0]); }
void style1(E& e) { applyLook(e, kLooks[1]); }
void style2(E& e) { applyLook(e, kLooks[2]); }
void style3(E& e) { applyLook(e, kLooks[3]); }

constexpr EffectStyle kStyles[] = {
    {kLooks[0].name, style0}, {kLooks[1].name, style1}, {kLooks[2].name, style2}, {kLooks[3].name, style3},
};

// The catalogue's modulation: the bass swells the orb, the level brightens its heart (the beat
// response slider drives the turbulence).
constexpr EffectRoute kRoutes[] = {
    {"audio.bass", "radius", 0.25f, 10.0f, 260.0f},
    {"audio.rms", "coreIntensity", 3.0f, 20.0f, 300.0f},
};

E make(std::string name) {
    E e;
    e.name = std::move(name);
    e.kind = EffectKind::Plasma;
    e.activation = Activation::Always;
    e.timing = Timing{};
    e.timing.fadeIn = 0.6;
    e.timing.fadeOut = 0.8;
    applyLook(e, kLooks[0]);
    return e;
}

// ---- resolution -----------------------------------------------------------------------------------

std::size_t records(const E& e, const EffectContext& ctx) {
    kinds::ShellLive live;
    kinds::ShellAnchor anchor;
    const glm::vec3 offset(kRows.f(e, "offsetX"), kRows.f(e, "offsetY"), kRows.f(e, "offsetZ"));
    return kinds::shellLive(e, ctx, live) && kRows.f(e, "radius") > 0.0f && kinds::shellAnchor(e, ctx, offset, anchor)
               ? 1u
               : 0u;
}

EffectStatus emit(const E& e, const EffectContext& ctx, ShellSink& sink, std::string& reason) {
    kinds::ShellLive live;
    if (!kinds::shellLive(e, ctx, live)) {
        return kinds::shellDormant(e, ctx, reason);
    }
    const float radius = std::max(kRows.f(e, "radius"), 0.02f);
    kinds::ShellAnchor anchor;
    const glm::vec3 offset(kRows.f(e, "offsetX"), kRows.f(e, "offsetY"), kRows.f(e, "offsetZ"));
    if (!kinds::shellAnchor(e, ctx, offset, anchor)) {
        return EffectStatus::Dormant; // the owner is not drawn this frame
    }
    const float envelope = live.envelope;
    const glm::vec3 core = kRows.rgb(e, "coreColor");
    const glm::vec3 edge = kRows.rgb(e, "edgeColor");

    ShellInstance shell;
    shell.model = kinds::shellModel(anchor.centre, glm::vec3(radius));
    shell.params[0] = glm::vec4(core, std::max(kRows.f(e, "coreIntensity"), 0.0f) * envelope);
    shell.params[1] = glm::vec4(kinds::seedOf(e.id), static_cast<float>(ctx.seconds * static_cast<double>(std::max(kRows.f(e, "speed"), 0.0f))), 0.0f, 0.0f);
    shell.params[2] = glm::vec4(edge, std::max(kRows.f(e, "edgeIntensity"), 0.0f) * envelope);
    shell.params[3] = glm::vec4(std::max(kRows.f(e, "turbulence"), 0.0f), std::max(kRows.f(e, "scale"), 0.1f),
                                std::clamp(kRows.f(e, "steps"), 4.0f, 48.0f),
                                std::clamp(kRows.f(e, "limbSoftness"), 0.02f, 1.0f));
    shell.params[4] = glm::vec4(std::clamp(kRows.f(e, "coreSize"), 0.02f, 1.0f),
                                std::clamp(kRows.f(e, "filaments"), 0.0f, 0.95f), std::max(kRows.f(e, "density"), 0.0f),
                                std::max(kRows.f(e, "rim"), 0.0f) * envelope);
    if (!sink.append(ShellShading::Plasma, ShellMesh::Sphere, shell)) {
        reason = "The shell budget (128) is full this frame: not drawn.";
        return EffectStatus::Dropped;
    }
    sink.usesDepth();

    const float light = std::max(kRows.f(e, "light"), 0.0f) * envelope;
    if (light > 0.0f) {
        // The light's colour is the orb's overall hue -- the core and filaments weighted as the eye
        // sees them -- normalised so `light` alone says how bright it is.
        glm::vec3 hue = core * 0.6f + edge * 0.4f;
        hue /= std::max({hue.r, hue.g, hue.b, 1e-4f});
        EffectLight l;
        l.position = anchor.centre;
        l.intensity = light;
        l.color = hue;
        l.range = radius * std::max(kRows.f(e, "lightRange"), 1.0f);
        l.volumetric = std::clamp(kRows.f(e, "lightFog"), 0.0f, 1.0f);
        sink.requestLight(l);
    }
    return EffectStatus::Drawn;
}

EffectSchema buildSchema() {
    EffectSchema s;
    s.kind = EffectKind::Plasma;
    s.key = "plasma";
    s.enumName = "Plasma";
    s.displayName = "Plasma";
    s.description = "A turbulent glowing orb -- a ball of plasma, a fireball's core, an energy orb: a "
                    "white-hot heart inside swirling coloured filaments that boil in place, with a soft "
                    "limb and a bloom halo. It emits light and throws a real light on what is around it.";
    s.performance = PerformanceClass::Medium;
    s.primaryCost = CostFragment;
    s.addLabel = "Plasma";
    s.addTip = "A glowing, boiling orb of energy on this entity or placed in the world.";
    s.targets = targetBit(EffectTarget::Entity) | targetBit(EffectTarget::World);
    s.category = EffectCategory::Lighting;
    s.stage = RenderStage::Particles;
    s.fields = kFields;
    s.styles = kStyles;
    s.routes = kRoutes;
    s.beatLeaf = "turbulence";
    s.factory = make;
    s.resolve.bucket = EffectBucket::Shell;
    s.resolve.records = records;
    // How this type becomes shells, registered with the Shell builder from here so the builder names
    // no type. Building the schema is what registers it.
    registerShellProducer(EffectKind::Plasma, ShellProducer{emit});
    return s;
}

} // namespace

const EffectSchema& plasmaSchema() {
    static const EffectSchema kSchema = buildSchema();
    return kSchema;
}

} // namespace avgen::world
