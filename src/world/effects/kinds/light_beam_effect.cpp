// Light Beam (Effect Library Wave 3, catalog-light.md "Light Beam"): a visible cone of light -- a
// searchlight, a stage spot, a saucer's tractor beam. The cheap look that needs no fog; Volumetric
// Beam is the marched one.
//
// **How it is drawn.** SHELL, shading `Beam` (shaders/shell_fx.wgsl `fs_beam`), on a box that bounds
// a truncated cone: the fragment stage intersects the view ray with the cone analytically, clamps the
// chord where the scene's linear depth ends it, and integrates lit air along it in a few jittered
// steps -- a radial profile (a core that sharpens with Core sharpness, an edge that softens with Edge
// softness), a falloff towards the far end, and drifting dust (fBM in world space). So the beam is
// brighter near its source, soft at its sides, fades in where it meets the floor or a wall (the depth
// fade) and never ends in a hard disc. The integral is saturated (1 - e^-x), so looking straight up
// the beam is bright, not blown out. Additive; only a quarter of it feeds the bloom, so the cone
// reads as lit air rather than a glowing bar.
//
// **Owners.** A Light: the beam leaves the light and matches it -- a spot's outer cone is the angle,
// its range the length (the rows when it has none), its colour tints the beam's; the light itself is
// what lights the ground. An Entity: the beam leaves the bottom of the owner's drawn bounds, pointing
// down and tilted by the rows (a saucer's searchlight), and it asks LIGHTMOD's pool for a light so
// the ground it reaches is lit. The World: placed at the offset, aimed by the rows.
//
// **Limits.** A surface, not a volume: a pillar inside the beam casts no shaft (use Volumetric Beam).
// A camera INSIDE the cone's box sees the cone through the box's far faces, which the depth test can
// hide where the ground is nearer; the pool light is a point light half-way down the beam, not a spot.
// Stateless: a function of the transport second, the id and where the owner is drawn.

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
    storedColor("color", "Colour", glm::vec3(1.0f, 0.95f, 0.85f)).main()
        .tooltip("The beam's colour. On a Light owner it is multiplied by the light's own colour."),
    storedFloat("intensity", "Brightness", 0.6f, 0.0f, 50.0f, 0.0f, 3.0f).main().floorAt(0.0f)
        .tooltip("HDR brightness through the core of the beam near its source. Kept near or below 1 the\n"
                 "cone reads as lit air; far above it, a glowing bar."),
    storedFloat("length", "Length", 12.0f, 0.5f, 2000.0f, 1.0f, 120.0f).fmt("%.1f m").main().log().floorAt(0.5f)
        .tooltip("How far the beam reaches. A spot light owner with a range uses its range."),
    storedFloat("angle", "Cone angle", 12.0f, 0.5f, 80.0f, 1.0f, 45.0f).fmt("%.1f deg").main().clampTo(0.5f, 80.0f)
        .tooltip("The half-angle of the cone. A spot light owner uses its outer cone."),
    storedFloat("aperture", "Source radius", 0.25f, 0.0f, 50.0f, 0.0f, 3.0f).fmt("%.2f m").main().floorAt(0.0f)
        .tooltip("The beam's width where it leaves its source: a lamp's lens, a saucer's hatch."),
    storedFloat("dust", "Dust", 0.5f, 0.0f, 1.0f, 0.0f, 1.0f).main().sec("Air").clampTo(0.0f, 1.0f)
        .tooltip("How uneven the lit air is: 0 is a clean, even cone; 1 is thick drifting dust."),
    storedFloat("dustScale", "Dust scale", 0.6f, 0.02f, 10.0f, 0.1f, 3.0f).fmt("%.2f /m").main().log().floorAt(0.02f)
        .tooltip("How fine the dust is: larger is finer."),
    storedFloat("drift", "Dust drift", 0.3f, 0.0f, 10.0f, 0.0f, 2.0f).fmt("%.2f m/s").sec("Air").floorAt(0.0f)
        .tooltip("How fast the dust drifts up through the beam."),
    storedFloat("coreSharpness", "Core sharpness", 2.0f, 0.0f, 12.0f, 0.0f, 6.0f).sec("Shape").floorAt(0.0f)
        .tooltip("0 is an evenly filled cone; higher concentrates the light on the axis."),
    storedFloat("edgeSoftness", "Edge softness", 0.5f, 0.02f, 1.0f, 0.05f, 1.0f).clampTo(0.02f, 1.0f)
        .tooltip("How gradually the cone fades at its sides: small is a crisp edge."),
    storedFloat("falloff", "Falloff", 1.0f, 0.0f, 8.0f, 0.0f, 4.0f).floorAt(0.0f)
        .tooltip("How much the beam dims towards its far end."),
    storedFloat("endFade", "End fade", 0.25f, 0.0f, 1.0f, 0.0f, 1.0f).clampTo(0.0f, 1.0f)
        .tooltip("The fraction of its length over which the beam fades out at its end."),
    storedFloat("depthFade", "Soft contact", 0.8f, 0.0f, 20.0f, 0.0f, 4.0f).fmt("%.2f m").floorAt(0.0f)
        .tooltip("The distance over which the beam fades where it meets the ground or a wall.\n"
                 "Needs the depth prepass (the panel says when it is off)."),
    storedFloat("steps", "Quality (steps)", 10.0f, 4.0f, 32.0f, 4.0f, 24.0f).fmt("%.0f").clampTo(4.0f, 32.0f)
        .tooltip("Samples along each ray through the beam. More is smoother dust."),
    storedBool("castLight", "Light the ground", true).sec("Light")
        .tooltip("Entity and World owners: ask the effect light pool for a light that lights what the\n"
                 "beam reaches. A Light owner is its own light."),
    storedFloat("light", "Light", 150.0f, 0.0f, 100000.0f, 0.0f, 1000.0f).floorAt(0.0f)
        .tooltip("That light's intensity, in candela. Shares the 16-light effect pool."),
    storedFloat("lightFog", "Light in fog", 0.4f, 0.0f, 1.0f, 0.0f, 1.0f).clampTo(0.0f, 1.0f),
    storedFloat("tilt", "Tilt", 0.0f, -89.0f, 89.0f, -60.0f, 60.0f).fmt("%.1f deg").sec("Aim (Entity, World)")
        .tooltip("How far the beam leans from straight down."),
    storedFloat("yaw", "Heading", 0.0f, -360.0f, 360.0f, -180.0f, 180.0f).fmt("%.1f deg")
        .tooltip("Which way it leans, about the vertical."),
    storedFloat("offsetX", "Offset X", 0.0f, -100000.0f, 100000.0f, -20.0f, 20.0f).fmt("%.2f m").sec("Placement")
        .tooltip("On the World: where the beam's source is. On an entity: an offset from the bottom\n"
                 "of its bounds. On a light: an offset from the light."),
    storedFloat("offsetY", "Offset Y", 0.0f, -100000.0f, 100000.0f, -20.0f, 20.0f).fmt("%.2f m"),
    storedFloat("offsetZ", "Offset Z", 0.0f, -100000.0f, 100000.0f, -20.0f, 20.0f).fmt("%.2f m"),
};

constexpr kinds::StoredRows kRows{"lightBeam", kFields};

struct Look {
    const char* name;
    glm::vec3 color;
    float intensity, length, angle, aperture, dust, dustScale, sharp, soft, falloff, light;
};

constexpr Look kLooks[] = {
    // A searchlight: narrow, long, cool white, a hard core and little dust.
    {"Searchlight", {0.9f, 0.95f, 1.0f}, 0.55f, 60.0f, 4.0f, 0.5f, 0.25f, 0.35f, 3.0f, 0.3f, 0.6f, 400.0f},
    // A stage spot: warm, wider, dusty, evenly filled.
    {"Stage Spot", {1.0f, 0.86f, 0.62f}, 0.5f, 14.0f, 16.0f, 0.2f, 0.7f, 0.9f, 1.0f, 0.55f, 0.8f, 150.0f},
    // A saucer's tractor beam: green, wide, dusty, soft at the edges.
    {"UFO Tractor", {0.4f, 1.0f, 0.55f}, 0.7f, 18.0f, 20.0f, 1.2f, 0.8f, 0.5f, 1.2f, 0.35f, 0.5f, 600.0f},
};

void applyLook(E& e, const Look& l) {
    kRows.setRgb(e, "color", l.color);
    kRows.set(e, "intensity", l.intensity);
    kRows.set(e, "length", l.length);
    kRows.set(e, "angle", l.angle);
    kRows.set(e, "aperture", l.aperture);
    kRows.set(e, "dust", l.dust);
    kRows.set(e, "dustScale", l.dustScale);
    kRows.set(e, "coreSharpness", l.sharp);
    kRows.set(e, "edgeSoftness", l.soft);
    kRows.set(e, "falloff", l.falloff);
    kRows.set(e, "light", l.light);
    e.style = l.name;
}
void style0(E& e) { applyLook(e, kLooks[0]); }
void style1(E& e) { applyLook(e, kLooks[1]); }
void style2(E& e) { applyLook(e, kLooks[2]); }

constexpr EffectStyle kStyles[] = {{kLooks[0].name, style0}, {kLooks[1].name, style1}, {kLooks[2].name, style2}};

// The catalogue's modulation: the bass pumps the beam, the mids stir its dust.
constexpr EffectRoute kRoutes[] = {
    {"audio.bass", "intensity", 0.35f, 15.0f, 300.0f},
    {"audio.mid", "dust", 0.25f, 40.0f, 500.0f},
};

E make(std::string name) {
    E e;
    e.name = std::move(name);
    e.kind = EffectKind::LightBeam;
    e.activation = Activation::Always;
    e.timing = Timing{};
    e.timing.fadeIn = 0.4;
    e.timing.fadeOut = 0.6;
    applyLook(e, kLooks[1]);
    return e;
}

// ---- resolution -----------------------------------------------------------------------------------

struct Beam {
    glm::vec3 apex{0.0f};
    glm::vec3 axis{0.0f, -1.0f, 0.0f};
    float length = 1.0f;
    float angle = 0.2f; // radians
    glm::vec3 tint{1.0f};
    bool lightOwner = false;
};

bool resolveBeam(const E& e, const EffectContext& ctx, Beam& out) {
    const glm::vec3 offset(kRows.f(e, "offsetX"), kRows.f(e, "offsetY"), kRows.f(e, "offsetZ"));
    kinds::FxAnchor anchor;
    if (!kinds::fxAnchor(e, ctx, offset, anchor)) {
        return false;
    }
    out.length = std::max(kRows.f(e, "length"), 0.5f);
    out.angle = glm::radians(std::clamp(kRows.f(e, "angle"), 0.5f, 80.0f));
    if (anchor.isLight) {
        if (anchor.light.directional || !(anchor.light.intensity > 0.0f)) {
            return false; // a sun has no cone to show, and a light that is off shows none
        }
        out.lightOwner = true;
        out.apex = anchor.centre;
        out.axis = anchor.light.direction;
        if (anchor.light.spot && anchor.light.outerCone > 0.0f) {
            out.angle = std::clamp(anchor.light.outerCone, glm::radians(0.5f), glm::radians(80.0f));
        }
        if (anchor.light.range > 0.0f) {
            out.length = anchor.light.range;
        }
        out.tint = anchor.light.color;
        return true;
    }
    out.apex = anchor.entity ? glm::vec3(anchor.centre.x, anchor.baseY, anchor.centre.z) : anchor.centre;
    out.axis = kinds::fxHeading(kRows.f(e, "yaw"), kRows.f(e, "tilt"), true);
    return true;
}

std::size_t records(const E& e, const EffectContext& ctx) {
    kinds::FxLive live;
    Beam beam;
    return kinds::fxLive(e, ctx, live) && resolveBeam(e, ctx, beam) ? 1u : 0u;
}

EffectStatus emit(const E& e, const EffectContext& ctx, ShellSink& sink, std::string& reason) {
    const bool lost = !reason.empty();
    kinds::FxLive live;
    if (!kinds::fxLive(e, ctx, live)) {
        return kinds::shellDormant(e, ctx, reason);
    }
    Beam beam;
    if (!resolveBeam(e, ctx, beam)) {
        if (e.owner.kind == EffectTarget::Light) {
            reason = "Its light is off, hidden, or a directional light (which has no cone to show).";
        }
        return EffectStatus::Dormant;
    }
    const float aperture = std::max(kRows.f(e, "aperture"), 0.0f);
    const float baseR = aperture + beam.length * std::tan(beam.angle);
    const float apertureFrac = std::clamp(aperture / std::max(baseR, 1e-4f), 0.0f, 0.95f);
    glm::vec3 u;
    glm::vec3 w;
    kinds::fxPlaneAxes(beam.axis, u, w);

    ShellInstance shell;
    shell.model = kinds::fxModel(beam.apex + beam.axis * (0.5f * beam.length), u * baseR,
                                 -beam.axis * (0.5f * beam.length), w * baseR);
    const glm::vec3 color = kRows.rgb(e, "color") * beam.tint;
    shell.params[0] = glm::vec4(color, std::max(kRows.f(e, "intensity"), 0.0f) * live.envelope);
    shell.params[1] = glm::vec4(kinds::seedOf(e.id), static_cast<float>(ctx.seconds), 0.0f, 0.0f);
    shell.params[2] = glm::vec4(std::max(kRows.f(e, "coreSharpness"), 0.0f),
                                std::clamp(kRows.f(e, "edgeSoftness"), 0.02f, 1.0f),
                                std::max(kRows.f(e, "falloff"), 0.0f), std::clamp(kRows.f(e, "endFade"), 0.0f, 1.0f));
    shell.params[3] = glm::vec4(std::clamp(kRows.f(e, "dust"), 0.0f, 1.0f), std::max(kRows.f(e, "dustScale"), 0.02f),
                                std::max(kRows.f(e, "depthFade"), 0.0f), std::clamp(kRows.f(e, "steps"), 4.0f, 32.0f));
    shell.params[4] = glm::vec4(0.0f, std::max(kRows.f(e, "drift"), 0.0f), 0.0f, beam.length);
    shell.params[5] = glm::vec4(apertureFrac, baseR, 0.0f, 0.0f);
    if (!sink.append(ShellShading::Beam, ShellMesh::Box, shell)) {
        reason = "The shell budget (128) is full this frame: not drawn.";
        return EffectStatus::Dropped;
    }
    if (kRows.f(e, "depthFade") > 0.0f) {
        sink.usesDepth();
    }
    const float light = std::max(kRows.f(e, "light"), 0.0f) * live.envelope;
    if (!beam.lightOwner && kRows.f(e, "castLight") > 0.5f && light > 0.0f) {
        glm::vec3 hue = color;
        hue /= std::max({hue.r, hue.g, hue.b, 1e-4f});
        EffectLight l;
        l.position = beam.apex + beam.axis * (0.6f * beam.length);
        l.intensity = light;
        l.color = hue;
        l.range = 0.55f * beam.length + baseR;
        l.volumetric = std::clamp(kRows.f(e, "lightFog"), 0.0f, 1.0f);
        sink.requestLight(l);
    }
    return kinds::fxDrawn(lost);
}

EffectSchema buildSchema() {
    EffectSchema s;
    s.kind = EffectKind::LightBeam;
    s.key = "lightBeam";
    s.enumName = "LightBeam";
    s.displayName = "Light Beam";
    s.description = "A visible cone of light -- a searchlight, a stage spot, a saucer's tractor beam: bright "
                    "near its source, soft at its sides, dusty, fading where it meets the ground. From a "
                    "spot light it matches the light's cone; from an entity it lights what it reaches.";
    s.performance = PerformanceClass::Low;
    s.primaryCost = CostFragment;
    s.addLabel = "Light Beam";
    s.addTip = "A visible cone of light from this light or entity, or placed in the world.";
    s.targets = targetBit(EffectTarget::Light) | targetBit(EffectTarget::Entity) | targetBit(EffectTarget::World);
    s.category = EffectCategory::Lighting;
    s.stage = RenderStage::Particles;
    s.fields = kFields;
    s.styles = kStyles;
    s.routes = kRoutes;
    s.beatLeaf = "intensity";
    s.factory = make;
    s.resolve.bucket = EffectBucket::Shell;
    s.resolve.records = records;
    registerShellProducer(EffectKind::LightBeam, ShellProducer{emit});
    return s;
}

} // namespace

const EffectSchema& lightBeamSchema() {
    static const EffectSchema kSchema = buildSchema();
    return kSchema;
}

} // namespace avgen::world
