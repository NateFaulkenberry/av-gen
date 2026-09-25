// Halo (Effect Library Wave 3, catalog-light.md "Halo"): (a) optical -- the glare disc and faint ring
// round a bright point, a lamp, an orb, the moon through thin cloud; (b) iconographic -- a luminous
// ring floating above the owner's head. Two modes of one effect.
//
// **Optical (Glare).** SHELL, shading `Glare` (shaders/shell_fx.wgsl): a camera-facing billboard
// (turned to the camera in its own vertex stage, so it faces whatever camera draws the frame) with a
// radial profile -- a bounded core, a soft glare tail with faint fine radial rays (the eye's ciliary
// corona), and a ring whose three colour channels sit at slightly different radii (Dispersion: 0 a
// white ring, 1 a corona's blue-inside, red-outside fringe). It is glare, not a surface, so it is not
// depth-tested per pixel: the fragment stage reads the linear depth in a 3x3 neighbourhood at the
// projected SOURCE and fades the whole glare as the source is hidden (a lamp behind a tree dims
// smoothly). Without the depth prepass nothing hides it, and the panel says so.
//
// **Ring.** SHELL, shading `Ring`: a torus marched inside a box proxy -- a solid emissive tube
// anti-aliased by its distance, and a soft glow round it -- depth-tested against the world, floating
// `height` above the top of the owner's bounds with a slow bob (stateless: a sine of the transport
// clock), tilted by the rows.
//
// **Owners.** A Light (at the light, in its colour), an Entity (optical at its centre, the ring over
// its head) or the World (placed). Stateless.

#include "world/effects/effect_registry.hpp"
#include "world/effects/kinds/shell_fx_kind.hpp"

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <string>

namespace avgen::world {
namespace {

using E = EffectInstance;

constexpr const char* kModes[] = {"Glare", "Ring"};
enum Mode : int { Glare = 0, Ring = 1 };

constexpr EffectField kFields[] = {
    storedChoice("mode", "Mode", 0, kModes).main()
        .tooltip("Glare: a soft disc and ring of light round a bright point (a lamp, the moon).\n"
                 "Ring: a luminous ring floating above the owner's head."),
    storedColor("color", "Colour", glm::vec3(1.0f, 0.9f, 0.72f)).main()
        .tooltip("On a Light owner it is multiplied by the light's own colour."),
    storedFloat("intensity", "Brightness", 1.6f, 0.0f, 100.0f, 0.0f, 6.0f).main().floorAt(0.0f)
        .tooltip("HDR brightness at the glare's centre, or of the ring's tube. Only the very core should\n"
                 "pass about 1: it is what blooms."),
    storedFloat("radius", "Radius", 1.6f, 0.02f, 5000.0f, 0.1f, 20.0f).fmt("%.2f m").main().log().floorAt(0.02f)
        .tooltip("Glare: how far the glare reaches from the source. Ring: the ring's radius."),
    storedFloat("coreSize", "Core size", 0.08f, 0.005f, 1.0f, 0.01f, 0.4f).sec("Glare").clampTo(0.005f, 1.0f)
        .tooltip("The bright core, as a fraction of the radius."),
    storedFloat("ringRadius", "Ring radius", 0.62f, 0.05f, 1.0f, 0.1f, 1.0f).clampTo(0.05f, 1.0f)
        .tooltip("Where the faint outer ring sits, as a fraction of the radius."),
    storedFloat("ringWidth", "Ring width", 0.05f, 0.005f, 0.5f, 0.01f, 0.25f).clampTo(0.005f, 0.5f),
    storedFloat("ringIntensity", "Ring brightness", 0.18f, 0.0f, 4.0f, 0.0f, 1.0f).floorAt(0.0f)
        .tooltip("The outer ring's brightness relative to the core."),
    storedFloat("dispersion", "Dispersion", 0.3f, 0.0f, 1.0f, 0.0f, 1.0f).clampTo(0.0f, 1.0f)
        .tooltip("How far the ring's colours separate: 0 white; 1 a corona's blue-inside, red-outside."),
    storedFloat("rays", "Rays", 0.2f, 0.0f, 1.0f, 0.0f, 1.0f).clampTo(0.0f, 1.0f)
        .tooltip("Faint fine radial streaks in the glare, as the eye sees round a lamp."),
    storedFloat("occlusionSoftness", "Occlusion softness", 0.4f, 0.01f, 50.0f, 0.05f, 4.0f).fmt("%.2f m").floorAt(0.01f)
        .tooltip("How gradually the glare fades as something moves in front of its source."),
    storedFloat("occlusionSlack", "Source depth", 0.3f, 0.0f, 100.0f, 0.0f, 5.0f).fmt("%.2f m").floorAt(0.0f)
        .tooltip("How far in front of the source a surface may be and still count as the source's\n"
                 "own (a lamp's glass, the owner itself) rather than something hiding it."),
    storedBool("screenSize", "Constant screen size", false)
        .tooltip("Glare: the radius is an angle (degrees) rather than metres, so the glare is the same\n"
                 "size on screen near and far -- a moon's corona."),
    storedFloat("thickness", "Tube thickness", 0.035f, 0.002f, 20.0f, 0.005f, 0.3f).fmt("%.3f m").sec("Ring").log().floorAt(0.002f),
    storedFloat("glow", "Glow", 0.5f, 0.0f, 8.0f, 0.0f, 2.0f).floorAt(0.0f)
        .tooltip("The soft glow round the ring, relative to its tube."),
    storedFloat("glowWidth", "Glow width", 0.08f, 0.005f, 20.0f, 0.01f, 1.0f).fmt("%.3f m").floorAt(0.005f),
    storedFloat("flow", "Shimmer", 0.35f, 0.0f, 1.0f, 0.0f, 1.0f).clampTo(0.0f, 1.0f)
        .tooltip("Light running round the ring."),
    storedFloat("height", "Height above", 0.3f, -100.0f, 100.0f, -2.0f, 3.0f).fmt("%.2f m")
        .tooltip("On an entity: how far above the top of its bounds the ring floats."),
    storedFloat("tilt", "Tilt", 8.0f, -90.0f, 90.0f, -45.0f, 45.0f).fmt("%.1f deg"),
    storedFloat("bob", "Bob", 0.03f, 0.0f, 10.0f, 0.0f, 0.3f).fmt("%.3f m").floorAt(0.0f),
    storedFloat("offsetX", "Offset X", 0.0f, -100000.0f, 100000.0f, -20.0f, 20.0f).fmt("%.2f m").sec("Placement")
        .tooltip("On the World: where the halo is. On an entity or a light: an offset from it."),
    storedFloat("offsetY", "Offset Y", 0.0f, -100000.0f, 100000.0f, -20.0f, 20.0f).fmt("%.2f m"),
    storedFloat("offsetZ", "Offset Z", 0.0f, -100000.0f, 100000.0f, -20.0f, 20.0f).fmt("%.2f m"),
};

constexpr kinds::StoredRows kRows{"halo", kFields};

void lampGlare(E& e) {
    kRows.set(e, "mode", Glare);
    kRows.setRgb(e, "color", {1.0f, 0.86f, 0.62f});
    kRows.set(e, "intensity", 1.6f);
    kRows.set(e, "radius", 1.6f);
    kRows.set(e, "coreSize", 0.08f);
    kRows.set(e, "ringRadius", 0.62f);
    kRows.set(e, "ringWidth", 0.05f);
    kRows.set(e, "ringIntensity", 0.1f);
    kRows.set(e, "dispersion", 0.25f);
    kRows.set(e, "rays", 0.2f);
    kRows.set(e, "screenSize", 0.0f);
    e.style = "Lamp Glare";
}
void saintRing(E& e) {
    kRows.set(e, "mode", Ring);
    kRows.setRgb(e, "color", {1.0f, 0.82f, 0.42f});
    kRows.set(e, "intensity", 2.2f);
    kRows.set(e, "radius", 0.32f);
    kRows.set(e, "thickness", 0.028f);
    kRows.set(e, "glow", 0.6f);
    kRows.set(e, "glowWidth", 0.07f);
    kRows.set(e, "flow", 0.35f);
    kRows.set(e, "height", 0.3f);
    kRows.set(e, "tilt", 8.0f);
    kRows.set(e, "bob", 0.03f);
    e.style = "Saint Ring";
}
void moonCorona(E& e) {
    kRows.set(e, "mode", Glare);
    kRows.setRgb(e, "color", {0.82f, 0.9f, 1.0f});
    kRows.set(e, "intensity", 0.9f);
    kRows.set(e, "radius", 5.0f); // degrees: constant screen size
    kRows.set(e, "coreSize", 0.12f);
    kRows.set(e, "ringRadius", 0.55f);
    kRows.set(e, "ringWidth", 0.07f);
    kRows.set(e, "ringIntensity", 0.35f);
    kRows.set(e, "dispersion", 1.0f);
    kRows.set(e, "rays", 0.0f);
    kRows.set(e, "screenSize", 1.0f);
    e.style = "Moon Corona";
}

constexpr EffectStyle kStyles[] = {{"Lamp Glare", lampGlare}, {"Saint Ring", saintRing}, {"Moon Corona", moonCorona}};

// The catalogue's modulation: the treble sparkles the halo.
constexpr EffectRoute kRoutes[] = {
    {"audio.treble", "intensity", 0.5f, 10.0f, 200.0f},
};

E make(std::string name) {
    E e;
    e.name = std::move(name);
    e.kind = EffectKind::Halo;
    e.activation = Activation::Always;
    e.timing = Timing{};
    e.timing.fadeIn = 0.4;
    e.timing.fadeOut = 0.6;
    lampGlare(e);
    return e;
}

// ---- resolution -----------------------------------------------------------------------------------

bool anchorOf(const E& e, const EffectContext& ctx, kinds::FxAnchor& anchor) {
    const glm::vec3 offset(kRows.f(e, "offsetX"), kRows.f(e, "offsetY"), kRows.f(e, "offsetZ"));
    if (!kinds::fxAnchor(e, ctx, offset, anchor)) {
        return false;
    }
    return !anchor.isLight || (anchor.light.intensity > 0.0f && !anchor.light.directional);
}

std::size_t records(const E& e, const EffectContext& ctx) {
    kinds::FxLive live;
    kinds::FxAnchor anchor;
    return kinds::fxLive(e, ctx, live) && anchorOf(e, ctx, anchor) ? 1u : 0u;
}

EffectStatus emit(const E& e, const EffectContext& ctx, ShellSink& sink, std::string& reason) {
    const bool lost = !reason.empty();
    kinds::FxLive live;
    if (!kinds::fxLive(e, ctx, live)) {
        return kinds::shellDormant(e, ctx, reason);
    }
    kinds::FxAnchor anchor;
    if (!anchorOf(e, ctx, anchor)) {
        if (e.owner.kind == EffectTarget::Light) {
            reason = "Its light is off, hidden, or a directional light (which has no position to glow at).";
        }
        return EffectStatus::Dormant;
    }
    glm::vec3 color = kRows.rgb(e, "color");
    if (anchor.isLight) {
        color *= anchor.light.color;
    }
    const float intensity = std::max(kRows.f(e, "intensity"), 0.0f) * live.envelope;
    const float seed = kinds::seedOf(e.id);
    ShellInstance shell;

    if (kRows.choice(e, "mode") == Ring) {
        const float radius = std::max(kRows.f(e, "radius"), 0.02f);
        const float tube = std::max(kRows.f(e, "thickness"), 0.002f);
        const float glowWidth = std::max(kRows.f(e, "glowWidth"), 0.005f);
        const float reach = tube + 3.0f * glowWidth;
        // Stateless bob: a sine of the transport clock, phased by the seed.
        const float bob = std::max(kRows.f(e, "bob"), 0.0f) *
                          static_cast<float>(std::sin(ctx.seconds * 2.0 * 0.45 + static_cast<double>(seed)));
        glm::vec3 centre = anchor.centre;
        if (anchor.entity) {
            centre.y = anchor.topY + kRows.f(e, "height");
        }
        centre.y += bob;
        // The ring's plane: horizontal, tilted about the owner's right axis by the Tilt row.
        const float tilt = glm::radians(kRows.f(e, "tilt"));
        glm::vec3 fwd(anchor.forward.x, 0.0f, anchor.forward.z);
        fwd = glm::length(fwd) > 1e-4f ? glm::normalize(fwd) : glm::vec3(0.0f, 0.0f, -1.0f);
        const glm::vec3 right = glm::normalize(glm::cross(fwd, glm::vec3(0.0f, 1.0f, 0.0f)));
        const glm::vec3 up = glm::normalize(glm::vec3(0.0f, std::cos(tilt), 0.0f) + fwd * std::sin(tilt));
        const glm::vec3 ahead = glm::normalize(glm::cross(right, up));
        shell.model = kinds::fxModel(centre, right * (radius + reach), up * reach, ahead * (radius + reach));
        shell.params[0] = glm::vec4(color, intensity);
        shell.params[1] = glm::vec4(seed, static_cast<float>(ctx.seconds), 0.0f, 0.0f);
        shell.params[2] = glm::vec4(radius, tube, glowWidth, std::max(kRows.f(e, "glow"), 0.0f));
        shell.params[3] = glm::vec4(std::clamp(kRows.f(e, "flow"), 0.0f, 1.0f), 0.6f, 32.0f, 0.0f);
        if (!sink.append(ShellShading::Ring, ShellMesh::Box, shell)) {
            reason = "The shell budget (128) is full this frame: not drawn.";
            return EffectStatus::Dropped;
        }
        sink.usesDepth();
        return kinds::fxDrawn(lost);
    }

    float radius = std::max(kRows.f(e, "radius"), 0.02f);
    if (kRows.f(e, "screenSize") > 0.5f) {
        const float distance = glm::length(anchor.centre - ctx.cameraPosition);
        radius = distance * std::tan(glm::radians(std::clamp(radius, 0.02f, 60.0f)));
    }
    shell.model = kinds::fxModel(anchor.centre, glm::vec3(radius, 0.0f, 0.0f), glm::vec3(0.0f, radius, 0.0f),
                                 glm::vec3(0.0f, 0.0f, radius));
    shell.params[0] = glm::vec4(color, intensity);
    shell.params[1] = glm::vec4(seed, static_cast<float>(ctx.seconds), 0.0f, 0.0f);
    shell.params[2] = glm::vec4(anchor.centre, radius);
    shell.params[3] = glm::vec4(std::clamp(kRows.f(e, "coreSize"), 0.005f, 1.0f), std::clamp(kRows.f(e, "ringRadius"), 0.05f, 1.0f),
                                std::clamp(kRows.f(e, "ringWidth"), 0.005f, 0.5f), std::max(kRows.f(e, "ringIntensity"), 0.0f));
    shell.params[4] = glm::vec4(std::clamp(kRows.f(e, "dispersion"), 0.0f, 1.0f), std::max(kRows.f(e, "occlusionSlack"), 0.0f),
                                std::max(kRows.f(e, "occlusionSoftness"), 0.01f), 0.0f);
    shell.params[5] = glm::vec4(std::clamp(kRows.f(e, "rays"), 0.0f, 1.0f), 90.0f, 0.0f, 0.0f);
    if (!sink.append(ShellShading::Glare, ShellMesh::Quad, shell)) {
        reason = "The shell budget (128) is full this frame: not drawn.";
        return EffectStatus::Dropped;
    }
    sink.usesDepth(); // its occlusion
    return kinds::fxDrawn(lost);
}

EffectSchema buildSchema() {
    EffectSchema s;
    s.kind = EffectKind::Halo;
    s.key = "halo";
    s.enumName = "Halo";
    s.displayName = "Halo";
    s.description = "A halo: the soft glare disc and faint ring round a bright light -- a lamp, a glowing "
                    "orb, the moon through thin cloud -- fading as the light is hidden; or a luminous ring "
                    "floating above the owner's head.";
    s.performance = PerformanceClass::VeryLow;
    s.primaryCost = CostFragment;
    s.addLabel = "Halo";
    s.addTip = "A glare round this light or entity, or a ring above its head.";
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
    registerShellProducer(EffectKind::Halo, ShellProducer{emit});
    return s;
}

} // namespace

const EffectSchema& haloSchema() {
    static const EffectSchema kSchema = buildSchema();
    return kSchema;
}

} // namespace avgen::world
