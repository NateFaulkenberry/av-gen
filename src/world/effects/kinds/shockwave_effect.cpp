// Shockwave (Effect Library Wave 2, catalog-distortion.md "Shockwave"): an expanding pressure or
// energy front released by an event -- a bass drop, an impact, a landing, a spell going off.
//
// **What it is.** One DF producer, one proxy per LIVE FRONT. A front is a sphere of radius
// `R(age) = maxRadius * ease(age / duration)`; the offset pass evaluates, at the view ray's closest
// approach to the centre (the projected ring coordinate), a signed derivative-of-Gaussian band around
// R -- compression outside, rarefaction inside -- so the background shows a thin refracting ring that
// sweeps outward. Its amplitude falls as R grows (energy spread over the front, `radiusDecay` the
// exponent) and fades over the last part of the life; an optional emissive leading edge feeds bloom.
// The Energy Blast / Explosion presets are this type with a bright rim (the catalog merges the
// Energy-family Shockwave into this one).
//
// **When.** TRIGGER: the default activation is `trigger` on every fourth beat. The producer asks
// `effectEventTimes` for the last few event times and draws a front for each one still inside its
// `duration` -- up to `maxConcurrent` overlapping rings, every one a pure function of `age = t - t0`.
// No state anywhere, so a scrub to second N draws the fronts a play to N draws (tested:
// `[shockwave][seek]`). Any other activation releases a front at its window's start (after the
// delay) and every `repeatSeconds`.
//
// **Where.** A World owner: a point (`offset`). An Entity owner: the owner's centre AS IT WAS WHEN THE
// FRONT WAS RELEASED -- read from HIST through `nodeDrawnPosition` -- so a wave a flying saucer
// releases stays where it was released instead of being dragged along with the craft. (A front whose
// release instant is older than the recorded history falls back to the owner's centre now.)
//
// **Not here.** The flash light (LIGHTMOD) and the dust ring (EMIT) the catalog couples through
// presets: the flash needs a request into the Material stage's light pool, which Wave 2 leaves to the
// lead's integration; a dust ring is a Particle Emitter on the same trigger.

#include "world/effects/distortion_frame.hpp"
#include "world/effects/effect_registry.hpp"
#include "world/effects/effect_trigger.hpp"
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

constexpr const char* kEases[] = {"outCubic", "outQuad", "outExpo", "linear"};
enum class Ease : int { OutCubic = 0, OutQuad = 1, OutExpo = 2, Linear = 3 };

constexpr EffectField kFields[] = {
    // ---- Main ------------------------------------------------------------------------------------
    storedFloat("strength", "Strength", 1.0f, 0.0f, 6.0f, 0.0f, 3.0f).main().sec("Front")
        .tooltip("How far the front bends what is behind it. 1 shifts the background by about half\n"
                 "the band's thickness where the band is born."),
    storedFloat("maxRadius", "Reach", 18.0f, 0.2f, 5000.0f, 1.0f, 150.0f).main().fmt("%.1f m").log().floorAt(0.2f)
        .tooltip("How far the front travels before it is gone."),
    storedFloat("duration", "Duration", 1.4f, 0.05f, 30.0f, 0.2f, 6.0f).main().fmt("%.2f s").floorAt(0.05f)
        .tooltip("How long one front takes to reach its full size and fade."),
    storedFloat("thickness", "Thickness", 1.4f, 0.02f, 200.0f, 0.1f, 10.0f).main().fmt("%.2f m").log().floorAt(0.02f)
        .tooltip("The width of the refracting band."),
    storedChoice("ease", "Expansion", 0, kEases).main()
        .tooltip("How the front slows as it spreads: out-cubic is a blast, linear is a sound wave."),
    storedColor("rimColor", "Edge glow colour", glm::vec3(1.0f, 0.62f, 0.28f)).main().sec("Edge glow"),
    storedFloat("rimEmission", "Edge glow", 0.0f, 0.0f, 80.0f, 0.0f, 12.0f).main()
        .tooltip("An emissive leading edge, in HDR: it reaches the bloom. 0 is a pure refraction."),
    storedFloat("chroma", "Chroma", 0.15f, 0.0f, 1.0f, 0.0f, 1.0f).main()
        .tooltip("Splits the colours across the band."),
    // ---- Advanced --------------------------------------------------------------------------------
    storedFloat("radiusDecay", "Weakens with size", 0.7f, 0.0f, 2.0f, 0.0f, 2.0f).sec("Shape")
        .tooltip("How fast the front weakens as it grows (the exponent on 1/R). 0 keeps full strength\n"
                 "to the edge; 2 is energy spread over a sphere."),
    storedFloat("maxConcurrent", "Overlapping fronts", 3.0f, 1.0f, 4.0f, 1.0f, 4.0f).fmt("%.0f")
        .tooltip("How many fronts may be in the air at once when triggers come faster than a front\n"
                 "lives. The oldest is the one left out."),
    storedFloat("offsetX", "Offset X", 0.0f, -100000.0f, 100000.0f, -50.0f, 50.0f).fmt("%.2f m").sec("Placement")
        .tooltip("On the World: where the front is released. On an entity: an offset from the centre\n"
                 "of the owner's bounds, in world metres."),
    storedFloat("offsetY", "Offset Y", 0.0f, -100000.0f, 100000.0f, -50.0f, 50.0f).fmt("%.2f m"),
    storedFloat("offsetZ", "Offset Z", 0.0f, -100000.0f, 100000.0f, -50.0f, 50.0f).fmt("%.2f m"),
};

constexpr kinds::StoredRows kRows{"shockwave", kFields};

float ease(Ease kind, float x) {
    x = std::clamp(x, 0.0f, 1.0f);
    switch (kind) {
    case Ease::OutCubic: return 1.0f - (1.0f - x) * (1.0f - x) * (1.0f - x);
    case Ease::OutQuad: return 1.0f - (1.0f - x) * (1.0f - x);
    case Ease::OutExpo: return x >= 1.0f ? 1.0f : (1.0f - std::exp2(-10.0f * x)) / (1.0f - std::exp2(-10.0f));
    case Ease::Linear: return x;
    }
    return x;
}

// ---- styles ---------------------------------------------------------------------------------------

struct Look {
    const char* name;
    float strength, maxRadius, duration, thickness;
    Ease ease;
    glm::vec3 rimColor;
    float rimEmission, chroma, radiusDecay;
    Trigger trigger;
};

Trigger beats(int everyN) {
    Trigger t;
    t.source = TriggerSource::Beat;
    t.everyN = everyN;
    return t;
}
Trigger music(const char* name) {
    Trigger t;
    t.source = TriggerSource::MusicEvent;
    t.name = name;
    return t;
}
Trigger onsets(float threshold) {
    Trigger t;
    t.source = TriggerSource::Onset;
    t.threshold = threshold;
    return t;
}

// The catalog's four presets. Each is a complete look including what fires it, so applying one never
// depends on what the instance held before.
const Look kLooks[] = {
    // Emissive leading edge, a hot orange; released on strong onsets.
    {"Explosion", 1.3f, 22.0f, 1.3f, 1.8f, Ease::OutCubic, {1.0f, 0.55f, 0.22f}, 2.5f, 0.12f, 0.8f, onsets(1.6f)},
    // Refraction only, fast and linear: a sound barrier giving way.
    {"Sonic Boom", 1.1f, 30.0f, 0.9f, 1.2f, Ease::Linear, {1.0f, 1.0f, 1.0f}, 0.0f, 0.06f, 0.5f, beats(4)},
    // A cyan energy front: a thin glowing line with a clear bend behind it. The band is thin (0.4 m)
    // and the edge modest (1.5), so the bloom softens it into a filament instead of saturating a
    // metre-wide bar across the frame (at 1.0 m and 4.0 it read as a solid bright band on the UFO
    // film); the strength is raised so the thinner band still bends what is behind it visibly.
    {"Energy Blast", 1.3f, 16.0f, 1.1f, 0.4f, Ease::OutExpo, {0.35f, 0.9f, 1.0f}, 1.5f, 0.25f, 0.9f, beats(4)},
    // Large and subtle, on the music's drops.
    {"Bass Drop", 0.7f, 60.0f, 2.6f, 4.0f, Ease::OutQuad, {0.6f, 0.7f, 1.0f}, 0.0f, 0.1f, 0.4f, music("drop")},
};

void applyLook(E& e, const Look& l) {
    kRows.set(e, "strength", l.strength);
    kRows.set(e, "maxRadius", l.maxRadius);
    kRows.set(e, "duration", l.duration);
    kRows.set(e, "thickness", l.thickness);
    kRows.set(e, "ease", static_cast<float>(l.ease));
    kRows.setRgb(e, "rimColor", l.rimColor);
    kRows.set(e, "rimEmission", l.rimEmission);
    kRows.set(e, "chroma", l.chroma);
    kRows.set(e, "radiusDecay", l.radiusDecay);
    e.activation = Activation::Trigger;
    e.timing.trigger = l.trigger;
    e.style = l.name;
}
void style0(E& e) { applyLook(e, kLooks[0]); }
void style1(E& e) { applyLook(e, kLooks[1]); }
void style2(E& e) { applyLook(e, kLooks[2]); }
void style3(E& e) { applyLook(e, kLooks[3]); }

constexpr EffectStyle kStyles[] = {
    {"Explosion", style0},
    {"Sonic Boom", style1},
    {"Energy Blast", style2},
    {"Bass Drop", style3},
};

// What fires a front is the trigger; how hard it hits is the level. The route is on the strength,
// with a slow release so a front released at a loud moment keeps its weight through its life.
constexpr EffectRoute kRoutes[] = {
    {"audio.rms", "strength", 0.8f, 20.0f, 700.0f},
};

E make(std::string name) {
    E e;
    e.name = std::move(name);
    e.kind = EffectKind::Shockwave;
    e.timing.fadeIn = 0.0;
    e.timing.fadeOut = 0.0;
    applyLook(e, kLooks[1]);
    e.style.clear();
    // The plain type: an emissive-free front on every fourth beat (one per bar in 4/4).
    kRows.set(e, "maxRadius", 18.0f);
    kRows.set(e, "duration", 1.4f);
    kRows.set(e, "thickness", 1.4f);
    kRows.set(e, "ease", static_cast<float>(Ease::OutCubic));
    kRows.set(e, "chroma", 0.15f);
    kRows.set(e, "radiusDecay", 0.7f);
    kRows.set(e, "strength", 1.0f);
    return e;
}

EffectSchema buildSchema() {
    EffectSchema s;
    s.kind = EffectKind::Shockwave;
    s.key = "shockwave";
    s.enumName = "Shockwave";
    s.displayName = "Shockwave";
    s.description =
        "An expanding front released by an event -- a beat, an onset, a drop, a marker, or the owner "
        "coming near something: a thin refracting ring that sweeps outward and weakens as it spreads, "
        "with an optional glowing leading edge. Several fronts can be in the air at once. On an entity "
        "it is released where the entity was at that moment.";
    s.performance = PerformanceClass::Medium;
    s.primaryCost = CostFragment | CostBandwidth | CostExtraPass;
    s.targets = targetBit(EffectTarget::Entity) | targetBit(EffectTarget::World);
    s.category = EffectCategory::Distortion;
    s.stage = RenderStage::ScreenSpace;
    s.priority = 1;
    s.addLabel = "Shockwave";
    s.addTip = "An expanding refracting front, released on the beat, a drop, an impact or a marker.";
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

float shockwaveHistorySeconds(const EffectInstance& e) {
    return std::max(kRows.f(e, "duration"), 0.05f) + static_cast<float>(e.timing.delay) + 0.1f;
}

// The producer (declared in distortion_frame.cpp's table). One sphere per live front.
std::size_t shockwaveProxies(const EffectInstance& e, const EffectContext& ctx, float envelope,
                             std::span<DistortionProxy> out) {
    if (out.empty() || envelope <= 0.0f) {
        return 0;
    }
    std::array<double, 4> starts{};
    const std::size_t want = std::min<std::size_t>(
        {out.size(), starts.size(), static_cast<std::size_t>(std::clamp(kRows.f(e, "maxConcurrent"), 1.0f, 4.0f) + 0.5f)});
    const std::size_t fronts = effectEventTimes(e, ctx, std::span(starts).first(want));
    if (fronts == 0) {
        return 0;
    }
    const glm::vec3 offset(kRows.f(e, "offsetX"), kRows.f(e, "offsetY"), kRows.f(e, "offsetZ"));
    glm::vec3 centreNow = offset;
    glm::vec3 originNow(0.0f);
    float ownerRadius = 0.0f;
    const bool entity = e.owner.kind == EffectTarget::Entity;
    if (entity) {
        NodeView view;
        if (!kinds::ownerCentre(e, ctx, offset, centreNow, ownerRadius, view)) {
            return 0;
        }
        originNow = glm::vec3(view.world[3]);
    } else if (e.owner.kind != EffectTarget::World) {
        return 0;
    }

    const float duration = std::max(kRows.f(e, "duration"), 0.05f);
    const float maxRadius = std::max(kRows.f(e, "maxRadius"), 0.2f);
    const float thickness = std::clamp(kRows.f(e, "thickness"), 0.02f, maxRadius);
    const float strength = std::max(kRows.f(e, "strength"), 0.0f);
    const float decay = std::clamp(kRows.f(e, "radiusDecay"), 0.0f, 2.0f);
    const auto easeKind = static_cast<Ease>(kRows.choice(e, "ease"));
    const glm::vec3 rim = kRows.rgb(e, "rimColor") * std::max(kRows.f(e, "rimEmission"), 0.0f);
    const float chroma = std::clamp(kRows.f(e, "chroma"), 0.0f, 1.0f);
    const float seed = kinds::seedOf(e.id);
    // The radius the amplitude is referenced to: a front is at full strength until it is this big.
    const float refRadius = std::max(0.12f * maxRadius, thickness);

    std::size_t made = 0;
    for (std::size_t i = 0; i < fronts && made < out.size(); ++i) {
        const double age = ctx.seconds - starts[i];
        if (age < 0.0 || age >= static_cast<double>(duration)) {
            continue;
        }
        const float x = static_cast<float>(age / static_cast<double>(duration));
        const float radius = std::max(maxRadius * ease(easeKind, x), 1e-3f);
        // Born in a few hundredths of the life (no pop), gone over the last third.
        const float life = smooth01(0.0f, 0.03f, x) * (1.0f - smooth01(0.68f, 1.0f, x));
        if (life <= 1e-4f) {
            continue;
        }
        glm::vec3 centre = centreNow;
        if (entity && ctx.scene != nullptr) {
            // Released where the owner WAS: its drawn origin at the release instant, carried to the
            // centre of its bounds by today's origin-to-centre offset.
            glm::vec3 originThen;
            if (ctx.scene->nodeDrawnPosition(e.owner.name, starts[i], originThen) && kinds::finite3(originThen)) {
                centre = originThen + (centreNow - originNow);
            }
        }
        const float outer = radius + 3.0f * thickness; // the band's Gaussian is gone by 3 widths
        const float amplitude = strength * 0.5f * thickness *
                                std::pow(refRadius / std::max(radius, refRadius), decay) * life * envelope;

        DistortionProxy& p = out[made++];
        // The owner stays crisp while the front is still inside it; a front released away from where
        // the owner now is has nothing to exclude.
        const float exclusion = entity && glm::length(centre - centreNow) < 0.5f * ownerRadius + 1e-3f &&
                                        radius < ownerRadius
                                    ? ownerRadius
                                    : 0.0f;
        p.centre = glm::vec4(centre, exclusion);
        p.axis0 = glm::vec4(outer, 0.0f, 0.0f, static_cast<float>(DistortionShape::Ellipsoid));
        p.axis1 = glm::vec4(0.0f, outer, 0.0f, static_cast<float>(DistortionField::Shock));
        // The depth band the bend fades in over behind the lens plane: the band's own thickness, so a
        // ground plane cutting the sphere has no seam.
        p.axis2 = glm::vec4(0.0f, 0.0f, outer, std::max(thickness, 0.05f));
        p.terms = glm::vec4(1.0f, 0.0f, 0.0f, amplitude);
        p.motion = glm::vec4(0.0f);
        p.shape = glm::vec4(radius / outer, thickness / outer, 0.0f, 0.0f);
        p.noise = glm::vec4(0.0f, chroma, thickness / outer, seed);
        // The edge dims as the front spreads, faster than the bend does: a flash, then a ring.
        const float rimFade = life * (1.0f - x) * (1.0f - x) * envelope;
        p.rim = glm::vec4(rim * rimFade, 0.0f);
    }
    return made;
}

const EffectSchema& shockwaveSchema() {
    static const EffectSchema kSchema = buildSchema();
    return kSchema;
}

} // namespace avgen::world
