// Heat Shimmer (Effect Library Wave 3, catalog-distortion.md "Heat Shimmer", Volume mode): the view
// wobbling through a column of hot air -- above a campfire, behind a jet, over sun-baked ground, at the
// mouth of a forge.
//
// **What it is.** One DF producer, one CYLINDER proxy per instance (`DistortionShape::Cylinder`). The
// offset pass intersects each view ray with the column analytically, so it knows how much hot air lies
// between the camera and the scene surface behind the pixel, and bends the pixel by a turbulent flow
// sampled in the middle of that stretch, in proportion to it (capped). So:
//   - what is in FRONT of the column is not bent at all, what is inside it only by the air in front of
//     it, and the sky or a wall behind it by the whole column -- depth-aware without a threshold;
//   - it is strongest near the source (the column's base) and fades up the column and toward its
//     edge, and out with distance from the camera;
//   - the displacement is authored in metres at the column and projected, so distant shimmer is
//     smaller on screen, as it should be.
//
// **The flow.** `flowCurl` from noise.wgsl (divergence-free, analytic gradients) sampled at the world
// point scrolled DOWN by the rise, so the eddies travel up the column. A single scrolled noise would
// need its scroll distance to grow without bound (float precision, a pop at any wrap), so the field is
// TWO layers, each living one cycle of `kCycleSeconds` and reborn with a new seed while its weight is
// zero: weights sin(pi phi) and |cos(pi phi)|, whose squares sum to one, so the shimmer's energy is
// constant through the crossfade. Every lane is a function of the transport second alone (ADR-091):
// phases from `floor`/`fract` of seconds over the cycle, seeds from the instance id and the cycle
// index. No state, so a scrub lands on the same shimmer as play.
//
// **Owners.** The World: a column standing at a point (its base at the offset). An Entity: the column
// rides the owner -- rising from the bottom of its drawn bounds (a fire, a brazier, a vent), or
// streaming out behind it along its backward axis (an engine exhaust).

#include "world/effects/distortion_frame.hpp"
#include "world/effects/effect_registry.hpp"
#include "world/effects/kinds/stored_rows.hpp"

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

namespace avgen::world {
namespace {

using E = EffectInstance;

// One cycle of a flow layer, in seconds. Long enough that the crossfade is not a visible breathing
// (half a cycle between the two layers' peaks), short enough that the scroll distance stays small.
constexpr double kCycleSeconds = 4.0;

constexpr const char* kRises[] = {"up", "behind owner"};
enum class Rise : int { Up = 0, BehindOwner = 1 };

constexpr EffectField kFields[] = {
    storedFloat("strength", "Strength", 0.04f, 0.0f, 2.0f, 0.0f, 0.25f).main().fmt("%.3f m").sec("Shimmer")
        .tooltip("How far the hot air moves what is behind it, in metres at the column, through the\n"
                 "reference thickness of air. Heat haze is small: a few centimetres reads as a campfire."),
    storedFloat("radius", "Radius", 1.0f, 0.05f, 2000.0f, 0.2f, 40.0f).main().fmt("%.2f m").log().floorAt(0.05f)
        .tooltip("The column's radius."),
    storedFloat("height", "Height", 4.0f, 0.1f, 2000.0f, 0.5f, 60.0f).main().fmt("%.2f m").log().floorAt(0.1f)
        .tooltip("How far the hot air rises (or streams out behind an exhaust) before it has cooled."),
    storedFloat("scale", "Eddy size", 0.3f, 0.02f, 50.0f, 0.05f, 3.0f).main().fmt("%.2f m").log().floorAt(0.02f)
        .tooltip("The size of one ripple of hot air. Small is a fine, fast wobble; large a slow swim."),
    storedFloat("riseSpeed", "Rise speed", 1.6f, 0.0f, 200.0f, 0.0f, 20.0f).main().fmt("%.2f m/s")
        .tooltip("How fast the eddies travel up the column (or out of an exhaust)."),
    storedChoice("rise", "Rises", 0, kRises).main()
        .tooltip("Up from the base (a fire, hot ground, a vent), or on an entity, streaming out behind\n"
                 "it along its backward axis (an engine exhaust)."),
    storedFloat("chroma", "Chroma", 0.0f, 0.0f, 1.0f, 0.0f, 1.0f).main()
        .tooltip("Real heat haze is achromatic; a little colour split reads as magic or plasma heat."),
    storedFloat("churn", "Churn", 0.6f, 0.0f, 8.0f, 0.0f, 3.0f).sec("Shape")
        .tooltip("How fast the eddies change shape as they rise. 0 is a pattern that only scrolls."),
    storedFloat("edgeSoftness", "Edge softness", 0.5f, 0.0f, 1.0f, 0.0f, 1.0f)
        .tooltip("How gradually the shimmer fades toward the column's side."),
    storedFloat("heightFalloff", "Fades with height", 1.2f, 0.1f, 6.0f, 0.2f, 4.0f)
        .tooltip("How quickly the shimmer weakens up the column: high keeps it close to the source."),
    storedFloat("thicknessRef", "Reference thickness", 2.0f, 0.05f, 2000.0f, 0.2f, 40.0f).fmt("%.2f m").log()
        .floorAt(0.05f)
        .tooltip("The depth of hot air that gives the full Strength. A view through less air (the column's\n"
                 "edge, a surface inside it) bends less; through more, up to 1.5x as much."),
    storedFloat("fadeDistance", "Fade distance", 150.0f, 0.0f, 100000.0f, 0.0f, 600.0f).fmt("%.0f m")
        .tooltip("Beyond this distance from the camera the shimmer has faded out (it starts fading at half\n"
                 "of it). 0 never fades."),
    storedFloat("offsetX", "Offset X", 0.0f, -100000.0f, 100000.0f, -50.0f, 50.0f).fmt("%.2f m").sec("Placement")
        .tooltip("On the World: where the column's base stands. On an entity: an offset from where the\n"
                 "column starts on the owner, in world metres."),
    storedFloat("offsetY", "Offset Y", 0.0f, -100000.0f, 100000.0f, -50.0f, 50.0f).fmt("%.2f m"),
    storedFloat("offsetZ", "Offset Z", 0.0f, -100000.0f, 100000.0f, -50.0f, 50.0f).fmt("%.2f m"),
};

constexpr kinds::StoredRows kRows{"heatShimmer", kFields};

struct Look {
    const char* name;
    float strength, radius, height, scale, riseSpeed;
    Rise rise;
    float churn, edgeSoftness, heightFalloff, thicknessRef;
};

constexpr Look kLooks[] = {
    // A column of hot air over a fire: fine eddies climbing at walking pace.
    {"Campfire", 0.035f, 0.9f, 3.5f, 0.25f, 1.8f, Rise::Up, 0.8f, 0.5f, 1.2f, 1.8f},
    // An engine's exhaust: narrow, long, fast and strong right at the nozzle.
    {"Jet Exhaust", 0.06f, 0.5f, 7.0f, 0.18f, 16.0f, Rise::BehindOwner, 2.2f, 0.45f, 1.6f, 1.0f},
    // Sun-baked ground: a wide, low slab of slow, broad swimming air.
    {"Desert Ground", 0.03f, 40.0f, 1.6f, 0.55f, 0.6f, Rise::Up, 0.35f, 0.6f, 0.8f, 20.0f},
    // A forge's mouth: strong, churning, a little taller than a campfire.
    {"Forge", 0.06f, 1.4f, 5.0f, 0.3f, 2.6f, Rise::Up, 1.2f, 0.45f, 1.0f, 2.5f},
};

void applyLook(E& e, const Look& l) {
    kRows.set(e, "strength", l.strength);
    kRows.set(e, "radius", l.radius);
    kRows.set(e, "height", l.height);
    kRows.set(e, "scale", l.scale);
    kRows.set(e, "riseSpeed", l.riseSpeed);
    kRows.set(e, "rise", static_cast<float>(l.rise));
    kRows.set(e, "churn", l.churn);
    kRows.set(e, "edgeSoftness", l.edgeSoftness);
    kRows.set(e, "heightFalloff", l.heightFalloff);
    kRows.set(e, "thicknessRef", l.thicknessRef);
    kRows.set(e, "chroma", 0.0f);
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

// The routes: loudness swells the heat, the bass lifts the column (a fire roaring), and an exhaust
// answers its owner's speed (the catalog's `owner.speed`; published raw in m/s, so the depth is per
// metre per second: 30 m/s adds 0.03 m, a Jet Exhaust's half again; on the World it has nothing to
// read and stays silent). Nothing routes onto the rise speed, churn or eddy size: each scales a phase
// or a noise coordinate, so a modulated one would jump the pattern rather than drive it.
constexpr EffectRoute kRoutes[] = {
    {"audio.rms", "strength", 0.03f, 60.0f, 400.0f},
    {"audio.bass", "height", 1.0f, 80.0f, 600.0f},
    {"owner.speed", "strength", 0.001f, 120.0f, 800.0f},
};

E make(std::string name) {
    E e;
    e.name = std::move(name);
    e.kind = EffectKind::HeatShimmer;
    e.activation = Activation::Always;
    applyLook(e, kLooks[0]);
    e.style.clear();
    return e;
}

EffectSchema buildSchema() {
    EffectSchema s;
    s.kind = EffectKind::HeatShimmer;
    s.key = "heatShimmer";
    s.enumName = "HeatShimmer";
    s.displayName = "Heat Shimmer";
    s.description =
        "The view wobbles through a column of hot air: fine eddies rising above a fire, streaming out of "
        "an exhaust or swimming over hot ground. Only what is behind the hot air bends, most through the "
        "thickest part and near the source, fading up the column, toward its edge and with distance.";
    s.performance = PerformanceClass::Low;
    s.primaryCost = CostFragment | CostBandwidth | CostExtraPass;
    s.targets = targetBit(EffectTarget::Entity) | targetBit(EffectTarget::World);
    s.category = EffectCategory::Distortion;
    s.stage = RenderStage::ScreenSpace;
    s.priority = 2;
    s.addLabel = "Heat Shimmer";
    s.addTip = "Hot air bending the view: above a fire, behind an exhaust, over hot ground.";
    s.fields = kFields;
    s.styles = kStyles;
    s.routes = kRoutes;
    s.beatLeaf = "strength";
    s.factory = make;
    s.resolve.bucket = EffectBucket::Distortion;
    s.resolve.records = distortionRecords;
    return s;
}

// A small integer from the instance seed and a cycle index, exact as a float: the flow layer's seed.
float layerSeed(float instanceSeed, std::int64_t cycle) {
    std::uint32_t h = 2166136261u ^ static_cast<std::uint32_t>(instanceSeed);
    h = (h ^ static_cast<std::uint32_t>(cycle & 0xffffffff)) * 16777619u;
    h = (h ^ static_cast<std::uint32_t>((cycle >> 32) & 0xffffffff)) * 16777619u;
    h ^= h >> 15;
    return static_cast<float>(h % 65521u);
}

} // namespace

// The producer (declared in distortion_frame.cpp's table). One cylinder per live instance.
std::size_t heatShimmerProxies(const EffectInstance& e, const EffectContext& ctx, float envelope,
                               std::span<DistortionProxy> out) {
    if (out.empty() || envelope <= 0.0f) {
        return 0;
    }
    const glm::vec3 offset(kRows.f(e, "offsetX"), kRows.f(e, "offsetY"), kRows.f(e, "offsetZ"));
    const float radius = std::max(kRows.f(e, "radius"), 0.05f);
    const float height = std::max(kRows.f(e, "height"), 0.1f);
    glm::vec3 base = offset;
    glm::vec3 up(0.0f, 1.0f, 0.0f);
    switch (e.owner.kind) {
    case EffectTarget::World: break;
    case EffectTarget::Entity: {
        NodeView view;
        glm::vec3 centre(0.0f);
        float ownerRadius = 0.0f;
        if (!kinds::ownerCentre(e, ctx, glm::vec3(0.0f), centre, ownerRadius, view)) {
            return 0; // no drawn owner this frame: nothing to rise from
        }
        if (static_cast<Rise>(kRows.choice(e, "rise")) == Rise::BehindOwner) {
            // The owner's backward axis: the node's drawn +Z is its forward (Ripple's rule), so the
            // exhaust streams out along -Z from the back of its bounding sphere.
            glm::vec3 fwd(view.world[2]);
            glm::vec3 f(0.0f);
            if (ctx.scene->nodeForward(e.owner.name, f) && glm::length(f) > 1e-6f) {
                fwd = f;
            }
            if (!(glm::length(fwd) > 1e-6f) || !kinds::finite3(fwd)) {
                return 0;
            }
            up = -glm::normalize(fwd);
            base = centre + up * ownerRadius * 0.5f + offset;
        } else {
            // Rising from the bottom of the owner's drawn bounds: the fire itself stands in the hot air.
            const float bottom = view.hasBounds ? view.boundsMin.y : centre.y;
            base = glm::vec3(centre.x, bottom, centre.z) + offset;
        }
        break;
    }
    case EffectTarget::Camera:
    case EffectTarget::Light: return 0; // not a target this type declares
    }
    if (!kinds::finite3(base)) {
        return 0;
    }

    glm::vec3 axis, side0, side1;
    distortionBasis(up, axis, side0, side1);

    // The two flow layers: phase in the cycle and a seed per cycle, from the transport second alone.
    const double tau = ctx.seconds / kCycleSeconds;
    const double cycleA = std::floor(tau);
    const double cycleB = std::floor(tau + 0.5);
    const float phiA = static_cast<float>(tau - cycleA);
    const float phiB = static_cast<float>(tau + 0.5 - cycleB);
    const float seed = kinds::seedOf(e.id);
    const auto risePerCycle = static_cast<float>(kCycleSeconds) * std::max(kRows.f(e, "riseSpeed"), 0.0f);
    const auto churnPerCycle = static_cast<float>(kCycleSeconds) * std::max(kRows.f(e, "churn"), 0.0f);

    DistortionProxy& p = out[0];
    // No exclusion radius: the lens plane of a column is where each ray ENTERS it, found per pixel.
    p.centre = glm::vec4(base + axis * (0.5f * height), 0.0f);
    p.axis0 = glm::vec4(side0 * radius, static_cast<float>(DistortionShape::Cylinder));
    p.axis1 = glm::vec4(axis * (0.5f * height), static_cast<float>(DistortionField::Shimmer));
    p.axis2 = glm::vec4(side1 * radius, 0.0f);
    p.terms = glm::vec4(1.5f, 0.0f, 0.0f, std::max(kRows.f(e, "strength"), 0.0f) * envelope);
    // The scroll one layer makes over its cycle, as a world vector (w stays 0: see the header).
    p.motion = glm::vec4(axis * risePerCycle, 0.0f);
    p.shape = glm::vec4(std::max(kRows.f(e, "scale"), 0.02f), std::clamp(kRows.f(e, "edgeSoftness"), 0.0f, 1.0f),
                        0.0f, std::max(kRows.f(e, "thicknessRef"), 0.05f));
    p.noise = glm::vec4(phiA, std::clamp(kRows.f(e, "chroma"), 0.0f, 1.0f), phiB,
                        layerSeed(seed, static_cast<std::int64_t>(cycleA)));
    p.rim = glm::vec4(std::clamp(kRows.f(e, "heightFalloff"), 0.1f, 6.0f), std::max(kRows.f(e, "fadeDistance"), 0.0f),
                      layerSeed(seed, static_cast<std::int64_t>(cycleB)), churnPerCycle);
    return 1;
}

const EffectSchema& heatShimmerSchema() {
    static const EffectSchema kSchema = buildSchema();
    return kSchema;
}

} // namespace avgen::world
