// Velocity Distortion (Effect Library Wave 2, catalog-motion.md "Velocity Distortion"): a refracting
// WAKE behind a fast owner -- space ripples along its recent path. Space Warp's bow wave is AROUND the
// owner; this is ALONG the path it has just flown.
//
// **What it is.** One DF producer: a chain of tube segments laid along the owner's recorded path
// (HIST, through `nodeDrawnPosition`, so an orbiting or bobbing owner's wake follows what was DRAWN).
// Each segment is an ellipsoid proxy whose long axis runs along the path; the offset pass (the `Wake`
// field) displaces what is behind it ACROSS the path with a fine travelling ripple
// `sin(2 pi (k rho - phase))`, fading to nothing at the tube's skin. Segments are centred on path
// samples and reach to their neighbours, and the along-path window is `cos^2`, so neighbouring segments
// sum to one: the wake is continuous, with no bead at each joint. The strength falls along the wake
// (older is calmer) and with speed below `speedForFull`; an owner slower than `minSpeed` leaves none.
//
// **The catalog's recommended technique** is a RIBBON drawn into the DF offset target. This is the
// same shape through DF's own proxies instead, which needs no second offset writer and no renderer
// change: six segments are six records of the 64-proxy budget, one copy and one resolve shared with
// every other distortion on the frame.
//
// **Determinism.** The path is HIST's, which a seek restores and replays; the ripple's phase is the
// transport second times its speed. Stateless given HIST.

#include "world/effects/distortion_frame.hpp"
#include "world/effects/effect_registry.hpp"
#include "world/effects/kinds/stored_rows.hpp"

#include <glm/glm.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace avgen::world {
namespace {

using E = EffectInstance;
using kinds::smooth01;

constexpr std::size_t kSegments = 6;

constexpr EffectField kFields[] = {
    storedFloat("strength", "Strength", 1.0f, 0.0f, 6.0f, 0.0f, 3.0f).main().sec("Wake")
        .tooltip("How far the wake moves what is behind it. The default route adds the owner's speed."),
    storedFloat("persistence", "Length", 1.0f, 0.1f, 6.0f, 0.2f, 3.0f).main().fmt("%.2f s")
        .tooltip("How many seconds of the owner's path the wake covers."),
    storedFloat("width", "Width", 1.0f, 0.1f, 8.0f, 0.2f, 4.0f).main()
        .tooltip("The wake's radius as a multiple of the owner's size."),
    storedFloat("rippleFrequency", "Ripples", 2.5f, 0.0f, 12.0f, 0.0f, 8.0f).main()
        .tooltip("How many ripples cross the wake from its centre to its edge. 0 is a smooth lens."),
    storedFloat("rippleSpeed", "Ripple speed", 1.5f, 0.0f, 20.0f, 0.0f, 6.0f).main().fmt("%.2f Hz")
        .tooltip("How fast the ripples run outward from the path."),
    storedFloat("chroma", "Chroma", 0.1f, 0.0f, 1.0f, 0.0f, 1.0f).main(),
    storedFloat("minSpeed", "Starts at", 2.0f, 0.0f, 500.0f, 0.0f, 20.0f).fmt("%.1f m/s").sec("Speed")
        .tooltip("Below this speed the owner leaves no wake."),
    storedFloat("speedForFull", "Full at", 20.0f, 0.1f, 1000.0f, 1.0f, 80.0f).fmt("%.1f m/s").floorAt(0.1f)
        .tooltip("The speed at which the wake reaches full strength."),
    storedFloat("edgeSoftness", "Edge softness", 0.4f, 0.0f, 1.0f, 0.0f, 1.0f).sec("Shape"),
};

constexpr kinds::StoredRows kRows{"velocityDistortion", kFields};

struct Look {
    const char* name;
    float strength, persistence, width, frequency, rippleSpeed, chroma;
};

constexpr Look kLooks[] = {
    // A jet's heat trail: short, tight and busy.
    {"Jet Wake", 1.2f, 0.9f, 0.8f, 3.5f, 2.5f, 0.06f},
    // A long, broad, slowly rippling contrail with a colour split.
    {"Warp Contrail", 0.8f, 1.8f, 1.6f, 1.4f, 0.8f, 0.25f},
};

void applyLook(E& e, const Look& l) {
    kRows.set(e, "strength", l.strength);
    kRows.set(e, "persistence", l.persistence);
    kRows.set(e, "width", l.width);
    kRows.set(e, "rippleFrequency", l.frequency);
    kRows.set(e, "rippleSpeed", l.rippleSpeed);
    kRows.set(e, "chroma", l.chroma);
    e.style = l.name;
}
void style0(E& e) { applyLook(e, kLooks[0]); }
void style1(E& e) { applyLook(e, kLooks[1]); }

constexpr EffectStyle kStyles[] = {
    {kLooks[0].name, style0},
    {kLooks[1].name, style1},
};

// The catalog's default is the owner's speed; per metre per second, because the signal is raw (the
// lesson Space Warp's route learned: 0.8 per m/s pinned a dashing saucer's warp at its maximum).
// The level breathes it a little on top, so a loud passage makes the same flight shimmer harder.
constexpr EffectRoute kRoutes[] = {
    {"owner.speed", "strength", 0.02f, 80.0f, 600.0f},
    {"audio.rms", "strength", 0.3f, 30.0f, 500.0f},
};

E make(std::string name) {
    E e;
    e.name = std::move(name);
    e.kind = EffectKind::VelocityDistortion;
    e.activation = Activation::Always;
    applyLook(e, kLooks[0]);
    e.style.clear();
    kRows.set(e, "strength", 1.0f);
    kRows.set(e, "persistence", 1.0f);
    kRows.set(e, "width", 1.0f);
    kRows.set(e, "rippleFrequency", 2.5f);
    kRows.set(e, "rippleSpeed", 1.5f);
    kRows.set(e, "chroma", 0.1f);
    return e;
}

std::size_t segments(const E&) { return kSegments; }

EffectSchema buildSchema() {
    EffectSchema s;
    s.kind = EffectKind::VelocityDistortion;
    s.key = "velocityDistortion";
    s.enumName = "VelocityDistortion";
    s.displayName = "Velocity Distortion";
    s.description =
        "A refracting wake behind a fast owner: space ripples along the path it has just flown, "
        "strongest right behind it and calming with age. It follows the path as drawn, and a slow owner "
        "leaves none.";
    s.performance = PerformanceClass::Medium;
    s.primaryCost = CostFragment | CostBandwidth | CostExtraPass;
    s.targets = targetBit(EffectTarget::Entity);
    s.category = EffectCategory::Distortion;
    s.stage = RenderStage::ScreenSpace;
    s.priority = 2;
    s.addLabel = "Velocity Distortion";
    s.addTip = "A rippling heat-trail wake behind a fast-moving entity.";
    s.fields = kFields;
    s.styles = kStyles;
    s.routes = kRoutes;
    s.beatLeaf = "strength";
    s.factory = make;
    s.resolve.bucket = EffectBucket::Distortion;
    s.resolve.records = distortionRecords;
    s.resolve.count = segments;
    return s;
}

} // namespace

float velocityDistortionHistorySeconds(const EffectInstance& e) {
    return std::clamp(kRows.f(e, "persistence"), 0.1f, 6.0f) + 0.1f;
}

std::size_t velocityDistortionProxies(const EffectInstance& e, const EffectContext& ctx, float envelope,
                                      std::span<DistortionProxy> out) {
    if (out.empty() || envelope <= 0.0f || e.owner.kind != EffectTarget::Entity || ctx.scene == nullptr) {
        return 0;
    }
    glm::vec3 centre(0.0f);
    float ownerRadius = 0.0f;
    NodeView view;
    if (!kinds::ownerCentre(e, ctx, glm::vec3(0.0f), centre, ownerRadius, view)) {
        return 0;
    }
    glm::vec3 velocity(0.0f);
    if (!ctx.scene->nodeVelocity(e.owner.name, velocity) || !kinds::finite3(velocity)) {
        velocity = glm::vec3(0.0f);
    }
    const float speed = glm::length(velocity);
    const float minSpeed = std::max(kRows.f(e, "minSpeed"), 0.0f);
    const float full = std::max(kRows.f(e, "speedForFull"), minSpeed + 0.1f);
    const float speedWeight = smooth01(minSpeed, full, speed);
    if (speedWeight <= 1e-4f) {
        return 0; // not moving fast enough to leave a wake
    }

    // The path: the drawn centre at kSegments + 1 instants back over the persistence. HIST gives the
    // drawn origin; today's origin-to-centre offset carries it to the middle of the bounds. With no
    // history yet (the first frames, or a scene that cannot answer), the path is extrapolated back
    // along the velocity -- a straight wake rather than none.
    const float persistence = std::clamp(kRows.f(e, "persistence"), 0.1f, 6.0f);
    const glm::vec3 originNow(view.world[3]);
    std::array<glm::vec3, kSegments + 2> path{};
    path[0] = centre;
    for (std::size_t k = 1; k < path.size(); ++k) {
        const double back = static_cast<double>(persistence) * static_cast<double>(k) / static_cast<double>(kSegments);
        glm::vec3 then;
        if (ctx.scene->nodeDrawnPosition(e.owner.name, ctx.seconds - back, then) && kinds::finite3(then)) {
            path[k] = then + (centre - originNow);
        } else {
            path[k] = centre - velocity * static_cast<float>(back);
        }
    }

    const float size = std::max(ownerRadius, 0.25f);
    const float width = std::clamp(kRows.f(e, "width"), 0.1f, 8.0f) * size;
    const float strength = std::max(kRows.f(e, "strength"), 0.0f);
    const float cycles = std::clamp(kRows.f(e, "rippleFrequency"), 0.0f, 12.0f);
    const double phase = std::fmod(ctx.seconds * static_cast<double>(std::max(kRows.f(e, "rippleSpeed"), 0.0f)), 4096.0);
    const float chroma = std::clamp(kRows.f(e, "chroma"), 0.0f, 1.0f);
    const float softness = std::clamp(kRows.f(e, "edgeSoftness"), 0.0f, 1.0f);
    const float seed = kinds::seedOf(e.id);

    std::size_t made = 0;
    for (std::size_t k = 1; k <= kSegments && made < out.size(); ++k) {
        const glm::vec3 p = path[k];
        // Nothing inside the owner: the wake starts at its skin.
        if (glm::length(p - centre) < ownerRadius) {
            continue;
        }
        const glm::vec3 tangent = path[k - 1] - path[k + 1];
        const float len = glm::length(tangent);
        if (!(len > 1e-4f)) {
            continue; // the owner hovered here: no direction, no wake
        }
        // Reach to both neighbours (half the span between them each way), so cos^2 windows overlap by
        // half and sum to one along the path.
        const float halfLength = 0.5f * len;
        glm::vec3 a0, a1, a2;
        distortionBasis(tangent / len, a0, a1, a2);
        // Older is calmer: linear in age, squared, so the wake tapers to nothing at its end.
        const float age = static_cast<float>(k) / static_cast<float>(kSegments + 1);
        const float fade = (1.0f - age) * (1.0f - age);
        // The tube widens a little as it relaxes, as a real wake spreads.
        const float r = width * (0.8f + 0.5f * age);

        DistortionProxy& q = out[made++];
        q.centre = glm::vec4(p, 0.0f);
        q.axis0 = glm::vec4(a0 * std::max(halfLength, 0.05f), static_cast<float>(DistortionShape::Ellipsoid));
        q.axis1 = glm::vec4(a1 * r, static_cast<float>(DistortionField::Wake));
        q.axis2 = glm::vec4(a2 * r, std::max(0.5f * r, 0.05f));
        q.terms = glm::vec4(1.0f, 0.0f, 0.0f, strength * 0.4f * r * speedWeight * fade * envelope);
        q.motion = glm::vec4(0.0f);
        q.shape = glm::vec4(cycles, softness, 0.0f, 0.0f);
        q.noise = glm::vec4(static_cast<float>(phase), chroma, 0.0f, seed);
        q.rim = glm::vec4(0.0f);
    }
    return made;
}

const EffectSchema& velocityDistortionSchema() {
    static const EffectSchema kSchema = buildSchema();
    return kSchema;
}

} // namespace avgen::world
