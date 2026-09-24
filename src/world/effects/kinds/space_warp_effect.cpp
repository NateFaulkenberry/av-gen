// Space Warp (Effect Library Wave 1, catalog-distortion.md "Space Warp"): the view bending around a
// thing -- a UFO's field, a gravity well, a spell, the mouth of a portal.
//
// **What it is.** One DF producer: an ellipsoidal proxy around its owner, carrying a field with three
// weighted terms -- a *radial* lens pull toward the centre, a *bow* wave along the owner's motion
// (compressed ahead, stretched behind), and a *swirl* about the view axis -- plus a low, animated,
// divergence-free turbulence that breaks the symmetry without shimmering. Background lines bend
// around the owner; the owner itself stays crisp (DF's self-exclusion: the lens plane sits behind the
// owner's bounds, so nothing of the owner is bent or pulled into the bend); a faint chromatic fringe
// sits at peak gradient; an optional thin emissive rim marks the field's edge. The four styles are
// weightings of the one field, not four effects.
//
// **Owners.** An Entity, primarily: the proxy is fitted to the node's DRAWN bounds (`nodeView`),
// follows its transform, and stretches along its velocity (`nodeVelocity`; zero when the scene cannot
// answer yet, which leaves a round warp rather than no warp). The World: a warp standing at a fixed
// point, sized in metres.
//
// **Where its numbers live.** `EffectInstance::values` under `spaceWarp/<leaf>` (a kind declared
// after ADR-500 has no struct on a shared header). The resolver reads them through `stored` below with
// the key built on the stack, so the per-frame path allocates nothing.
//
// **Determinism.** A pure function of the transport second, the owner's drawn bounds and its
// step-grid velocity (ADR-091): the turbulence phase is `seconds * speed`, the seed is a hash of the
// instance id, and nothing is accumulated.

#include "world/effects/distortion_frame.hpp"
#include "world/effects/effect_registry.hpp"

#include <glm/glm.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <string>
#include <string_view>

namespace avgen::world {
namespace {

using E = EffectInstance;

constexpr std::string_view kKey = "spaceWarp";

constexpr EffectField kFields[] = {
    // ---- Main: what an artist reaches for first ---------------------------------------------------
    storedFloat("strength", "Strength", 0.55f, 0.0f, 4.0f, 0.0f, 2.0f).main().sec("Warp")
        .tooltip("How far the view bends. 1 moves the background by a quarter of the field's radius\n"
                 "at the strongest point. The default route drives it from the owner's speed, so a\n"
                 "hovering owner warps gently and a dashing one warps hard."),
    storedFloat("boundsScale", "Size over owner", 2.4f, 1.0f, 12.0f, 1.2f, 6.0f).main()
        .tooltip("On an entity: the field's radius as a multiple of the owner's bounding radius.\n"
                 "The bend starts at the owner's edge and fades out at this size.\n"
                 "Ignored on the World, which uses Radius."),
    storedFloat("radius", "Radius", 8.0f, 0.1f, 5000.0f, 0.5f, 80.0f).main().fmt("%.1f m")
        .tooltip("On the World (or an owner with no drawn geometry): the field's radius in metres.\n"
                 "Ignored on an entity with bounds, which uses Size over owner."),
    storedFloat("radialWeight", "Lens pull", 0.3f, 0.0f, 1.0f, 0.0f, 1.0f).main()
        .tooltip("A lens pull toward the centre: background lines bow around the owner."),
    storedFloat("bowWeight", "Bow wave", 0.8f, 0.0f, 1.0f, 0.0f, 1.0f).main()
        .tooltip("Space compressed ahead of the owner and stretched behind it, along its motion.\n"
                 "Only moving owners have a bow wave: it grows with speed."),
    storedFloat("swirl", "Swirl", 0.0f, -1.0f, 1.0f, -1.0f, 1.0f).main()
        .tooltip("A twist about the view axis. Negative turns the other way."),
    storedFloat("turbulence", "Turbulence", 0.2f, 0.0f, 1.0f, 0.0f, 1.0f).main()
        .tooltip("A slow, swirling break-up of the field (a curl flow, so it drifts rather than\n"
                 "boils). 0 is a perfectly smooth lens."),
    storedFloat("chroma", "Chroma", 0.2f, 0.0f, 1.0f, 0.0f, 1.0f).main()
        .tooltip("Splits the colours along the bend, strongest where the bend is steepest."),
    storedColor("rimColor", "Edge glow colour", glm::vec3(0.55f, 0.85f, 1.0f)).main().sec("Edge glow"),
    storedFloat("rimIntensity", "Edge glow", 0.35f, 0.0f, 60.0f, 0.0f, 8.0f).main()
        .tooltip("A thin emissive ring at the field's edge, in HDR: it reaches the bloom.\n"
                 "0 is no ring."),
    // ---- Advanced ---------------------------------------------------------------------------------
    storedFloat("falloff", "Falloff", 2.0f, 0.5f, 4.0f, 0.5f, 4.0f).sec("Shape")
        .tooltip("Where across the field the bend peaks: low is broad and reaches the edge,\n"
                 "high is concentrated near the owner."),
    storedFloat("edgeSoftness", "Edge softness", 0.35f, 0.0f, 1.0f, 0.0f, 1.0f)
        .tooltip("How gradually the field fades out toward its edge."),
    storedFloat("velocityStretch", "Stretch with speed", 1.2f, 0.0f, 4.0f, 0.0f, 4.0f).sec("Motion")
        .tooltip("How much longer the field grows along the owner's motion at full speed.\n"
                 "1 doubles its length."),
    storedFloat("speedForFull", "Speed for full stretch", 14.0f, 0.1f, 500.0f, 1.0f, 60.0f).fmt("%.1f m/s")
        .floorAt(0.1f)
        .tooltip("The owner's speed at which the stretch and the bow wave reach full size."),
    storedFloat("turbulenceScale", "Turbulence scale", 2.0f, 0.25f, 12.0f, 0.5f, 6.0f).sec("Turbulence")
        .tooltip("How many swirls fit across the field."),
    storedFloat("turbulenceSpeed", "Turbulence speed", 0.5f, 0.0f, 6.0f, 0.0f, 3.0f)
        .tooltip("How fast the turbulence flows. Slow reads as heat and gravity, fast as magic."),
    storedFloat("rimWidth", "Edge glow width", 0.08f, 0.01f, 0.5f, 0.02f, 0.3f).sec("Edge glow")
        .tooltip("The ring's width as a fraction of the field's radius."),
    storedFloat("offsetX", "Offset X", 0.0f, -100000.0f, 100000.0f, -50.0f, 50.0f).fmt("%.2f m").sec("Placement")
        .tooltip("On the World: where the warp stands. On an entity: an offset from the centre of\n"
                 "the owner's bounds, in world metres."),
    storedFloat("offsetY", "Offset Y", 0.0f, -100000.0f, 100000.0f, -50.0f, 50.0f).fmt("%.2f m"),
    storedFloat("offsetZ", "Offset Z", 0.0f, -100000.0f, 100000.0f, -50.0f, 50.0f).fmt("%.2f m"),
};

// The row's declared default, so the resolver and the registry cannot disagree about it.
constexpr float defaultOf(std::string_view leaf) {
    for (const EffectField& f : kFields) {
        if (std::string_view(f.leaf) == leaf) {
            return f.storedDefault;
        }
    }
    return 0.0f;
}
constexpr glm::vec3 defaultColorOf(std::string_view leaf) {
    for (const EffectField& f : kFields) {
        if (std::string_view(f.leaf) == leaf) {
            return f.storedColor;
        }
    }
    return glm::vec3(0.0f);
}

// `spaceWarp/<leaf>` on the stack: the per-frame path must not allocate, and `storeKey` returns a
// std::string.
struct KeyBuf {
    std::array<char, 48> chars{};
    std::size_t size = 0;
    explicit KeyBuf(std::string_view leaf) {
        std::memcpy(chars.data(), kKey.data(), kKey.size());
        chars[kKey.size()] = '/';
        const std::size_t n = std::min(leaf.size(), chars.size() - kKey.size() - 1);
        std::memcpy(chars.data() + kKey.size() + 1, leaf.data(), n);
        size = kKey.size() + 1 + n;
    }
    [[nodiscard]] std::string_view view() const { return {chars.data(), size}; }
};

float stored(const E& e, std::string_view leaf) { return e.values.getFloat(KeyBuf(leaf).view(), defaultOf(leaf)); }
glm::vec3 storedRgb(const E& e, std::string_view leaf) {
    return e.values.getColor(KeyBuf(leaf).view(), defaultColorOf(leaf));
}
void put(E& e, std::string_view leaf, float v) { e.values.setFloat(KeyBuf(leaf).view(), v); }
void putRgb(E& e, std::string_view leaf, glm::vec3 v) { e.values.setColor(KeyBuf(leaf).view(), v); }

bool finite(glm::vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

// FNV-1a of the id, folded into a small float: a per-instance turbulence seed that is the same on
// every run and every machine, so two warps side by side do not swirl in lockstep.
float seedOf(const std::string& id) {
    std::uint32_t h = 2166136261u;
    for (const char c : id) {
        h ^= static_cast<std::uint8_t>(c);
        h *= 16777619u;
    }
    return static_cast<float>(h % 997u);
}

// ---- styles ---------------------------------------------------------------------------------------

struct Look {
    const char* name;
    float strength, radial, bow, swirl, turbulence, turbulenceScale, turbulenceSpeed, chroma;
    glm::vec3 rimColor;
    float rimIntensity, rimWidth, falloff, velocityStretch;
};

// The catalog's four presets. Each is a complete look: applying one never depends on what the
// instance held before.
constexpr Look kLooks[] = {
    // A craft's field: the bow wave carries it, speed-driven by the default route.
    {"UFO Warp", 0.55f, 0.3f, 0.8f, 0.0f, 0.2f, 2.0f, 0.5f, 0.2f, {0.55f, 0.85f, 1.0f}, 0.35f, 0.08f, 2.0f, 1.2f},
    // Pure lensing: no motion terms, no rim, a broad falloff.
    {"Gravitational Warp", 0.9f, 1.0f, 0.0f, 0.0f, 0.05f, 1.5f, 0.2f, 0.1f, {1.0f, 1.0f, 1.0f}, 0.0f, 0.08f, 1.2f, 0.0f},
    // A spell: swirl and a faster flow, with a violet rim.
    {"Magical Warp", 0.6f, 0.2f, 0.0f, 0.7f, 0.5f, 3.0f, 1.2f, 0.35f, {0.72f, 0.38f, 1.0f}, 2.0f, 0.1f, 2.0f, 0.3f},
    // The mouth of a portal: full swirl, a strong bright rim.
    {"Portal Warp", 0.9f, 0.5f, 0.0f, 1.0f, 0.25f, 2.5f, 0.8f, 0.3f, {0.35f, 0.9f, 1.0f}, 6.0f, 0.06f, 1.6f, 0.0f},
};

void applyLook(E& e, const Look& l) {
    put(e, "strength", l.strength);
    put(e, "radialWeight", l.radial);
    put(e, "bowWeight", l.bow);
    put(e, "swirl", l.swirl);
    put(e, "turbulence", l.turbulence);
    put(e, "turbulenceScale", l.turbulenceScale);
    put(e, "turbulenceSpeed", l.turbulenceSpeed);
    put(e, "chroma", l.chroma);
    putRgb(e, "rimColor", l.rimColor);
    put(e, "rimIntensity", l.rimIntensity);
    put(e, "rimWidth", l.rimWidth);
    put(e, "falloff", l.falloff);
    put(e, "velocityStretch", l.velocityStretch);
    e.style = l.name;
}
void style0(E& e) { applyLook(e, kLooks[0]); }
void style1(E& e) { applyLook(e, kLooks[1]); }
void style2(E& e) { applyLook(e, kLooks[2]); }
void style3(E& e) { applyLook(e, kLooks[3]); }

constexpr EffectStyle kStyles[] = {
    {kLooks[0].name, style0},
    {kLooks[1].name, style1},
    {kLooks[2].name, style2},
    {kLooks[3].name, style3},
};

// The default route is the catalog's: the owner's speed drives the strength, so a hovering craft is
// calm and a dashing one warps. `owner.` is resolved against the instance's owner when routes are
// installed (parameters-and-modulation.md §2.3). The beat route is a gentle breath on the downbeat,
// so a World-owned warp -- which has no `owner.speed` -- is not silent either.
constexpr EffectRoute kRoutes[] = {
    {"owner.speed", "strength", 0.8f, 80.0f, 600.0f},
    {"beat.pulse", "strength", 0.15f, 10.0f, 260.0f},
};

E make(std::string name) {
    E e;
    e.name = std::move(name);
    e.kind = EffectKind::SpaceWarp;
    e.activation = Activation::Always;
    applyLook(e, kLooks[0]);
    return e;
}

EffectSchema buildSchema() {
    EffectSchema s;
    s.kind = EffectKind::SpaceWarp;
    s.key = "spaceWarp";
    s.enumName = "SpaceWarp";
    s.displayName = "Space Warp";
    s.description =
        "The view bends around its owner: a lens pull toward it, a bow wave along its motion and an "
        "optional swirl, with a faint colour fringe and an optional glowing edge. The owner itself stays "
        "crisp while what is behind it bends. On an entity it follows the entity and stretches with its "
        "speed; on the World it stands at a point.";
    s.performance = PerformanceClass::Medium;
    s.primaryCost = CostFragment | CostBandwidth | CostExtraPass;
    s.targets = targetBit(EffectTarget::Entity) | targetBit(EffectTarget::World);
    s.category = EffectCategory::Distortion;
    s.stage = RenderStage::ScreenSpace;
    s.priority = 0;
    s.addLabel = "Space Warp";
    s.addTip = "The view bends around the owner -- a UFO's field, a gravity well, a spell.\n"
               "On an entity: follows it and stretches with its speed.";
    s.fields = kFields;
    s.styles = kStyles;
    s.routes = kRoutes;
    s.beatLeaf = "strength";
    s.factory = make;
    s.resolve.bucket = EffectBucket::Distortion;
    s.resolve.records = distortionRecords;
    return s;
}

} // namespace

// The producer (declared in distortion_frame.cpp's table). One ellipsoid per live instance.
std::size_t spaceWarpProxies(const EffectInstance& e, const EffectContext& ctx, float envelope,
                             std::span<DistortionProxy> out) {
    if (out.empty() || envelope <= 0.0f) {
        return 0;
    }
    const glm::vec3 offset(stored(e, "offsetX"), stored(e, "offsetY"), stored(e, "offsetZ"));
    glm::vec3 centre = offset;
    float ownerRadius = 0.0f;
    float radius = stored(e, "radius");
    glm::vec3 velocity(0.0f);
    switch (e.owner.kind) {
    case EffectTarget::World: break;
    case EffectTarget::Entity: {
        NodeView view;
        if (ctx.scene == nullptr || !ctx.scene->nodeView(e.owner.name, view)) {
            return 0; // no drawn owner this frame: nothing to warp around
        }
        if (view.hasBounds) {
            centre = 0.5f * (view.boundsMin + view.boundsMax) + offset;
            ownerRadius = 0.5f * glm::length(view.boundsMax - view.boundsMin);
            radius = ownerRadius * std::max(stored(e, "boundsScale"), 1.0f);
        } else {
            centre = glm::vec3(view.world[3]) + offset;
        }
        glm::vec3 v(0.0f);
        // False until the history slice answers it; that is zero velocity, a round warp.
        if (ctx.scene->nodeVelocity(e.owner.name, v) && finite(v)) {
            velocity = v;
        }
        break;
    }
    case EffectTarget::Camera:
    case EffectTarget::Light: return 0; // not a target this type declares
    }
    if (!finite(centre) || !(radius > 1e-3f) || !std::isfinite(radius)) {
        return 0;
    }

    // The motion terms: direction and how much of them applies. A smoothstep on the saturation, so a
    // slow drift does not snap the stretch on, and a direction with no speed behind it has no weight.
    const float speed = glm::length(velocity);
    const float s = std::clamp(speed / std::max(stored(e, "speedForFull"), 0.1f), 0.0f, 1.0f);
    const float motionWeight = s * s * (3.0f - 2.0f * s);
    const glm::vec3 dir = speed > 1e-3f ? velocity / speed : glm::vec3(1.0f, 0.0f, 0.0f);
    glm::vec3 a0, a1, a2;
    distortionBasis(dir, a0, a1, a2);
    const float stretch = 1.0f + std::max(stored(e, "velocityStretch"), 0.0f) * motionWeight;

    // The inner radius is where an entity's own silhouette ends; the field is zero inside it so the
    // bend does not peak on the owner's outline, where DF has nothing behind the owner to show.
    const float inner = std::clamp(ownerRadius / radius, 0.0f, 0.95f);

    DistortionProxy& p = out[0];
    p.centre = glm::vec4(centre, ownerRadius);
    p.axis0 = glm::vec4(a0 * radius * stretch, static_cast<float>(DistortionShape::Ellipsoid));
    p.axis1 = glm::vec4(a1 * radius, static_cast<float>(DistortionField::Warp));
    // The depth band the bend fades in over behind the lens plane: half the radius, so a floor or a
    // wall cutting the field has a soft transition instead of a seam.
    p.axis2 = glm::vec4(a2 * radius, std::max(0.5f * radius, 0.05f));
    const float peakMetres = std::max(stored(e, "strength"), 0.0f) * 0.25f * radius * envelope;
    p.terms = glm::vec4(stored(e, "radialWeight"), stored(e, "bowWeight"), stored(e, "swirl"), peakMetres);
    p.motion = glm::vec4(a0, motionWeight);
    p.shape = glm::vec4(stored(e, "falloff"), std::clamp(stored(e, "edgeSoftness"), 0.0f, 1.0f),
                        std::clamp(stored(e, "turbulence"), 0.0f, 1.0f), stored(e, "turbulenceScale"));
    // The phase wraps every 4096 s of animation so it keeps float precision on a long timeline.
    const double phase = std::fmod(ctx.seconds * static_cast<double>(stored(e, "turbulenceSpeed")), 4096.0);
    p.noise = glm::vec4(static_cast<float>(phase), std::clamp(stored(e, "chroma"), 0.0f, 1.0f),
                        std::clamp(stored(e, "rimWidth"), 0.01f, 0.5f), seedOf(e.id));
    p.rim = glm::vec4(storedRgb(e, "rimColor") * std::max(stored(e, "rimIntensity"), 0.0f) * envelope, inner);
    return 1;
}

const EffectSchema& spaceWarpSchema() {
    static const EffectSchema kSchema = buildSchema();
    return kSchema;
}

} // namespace avgen::world
