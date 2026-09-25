// Ripple (Effect Library Wave 2, catalog-distortion.md "Ripple", Membrane mode): a damped TRAIN of
// concentric waves in an invisible membrane -- a force wall that has been touched, a water orb, the
// air struck on the beat. Compare Shockwave, which is a single 3D front.
//
// **What it is.** One DF producer, one DISC proxy per live train. The offset pass finds where the view
// ray pierces the disc's plane and evaluates the classic water-drop wave
// `A sin(2 pi (r - c age) / lambda) e^(-r / decay)` inside the expanding front (nothing ahead of it),
// as a displacement IN the plane along the radius -- so a membrane seen face-on shows the background
// rippling outward in rings, and one seen at a slant shows them foreshortened, as a real surface would.
// Optional faint emissive crests feed bloom.
//
// The catalog's other mode, Surface (waves on scene surfaces), is Ground Pulse's `RadialWave` and needs
// no code here.
//
// **When.** TRIGGER: by default a train on every second beat; each trigger starts one, up to
// `maxConcurrent` at once, all pure functions of `age = t - t0` (see Shockwave).
//
// **Orientation.** `plane` picks the membrane's normal: facing the camera (the readable default for a
// struck-air ripple), horizontal (a pond surface hanging in the air), or the owner's own forward axis (a
// shield on the front of a craft).

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

constexpr const char* kPlanes[] = {"camera", "horizontal", "owner"};
enum class Plane : int { Camera = 0, Horizontal = 1, Owner = 2 };

constexpr EffectField kFields[] = {
    storedFloat("amplitude", "Strength", 0.3f, 0.0f, 20.0f, 0.0f, 1.0f).main().fmt("%.3f m").sec("Waves")
        .tooltip("How far the waves move what is behind the membrane, in metres at the membrane."),
    storedFloat("radius", "Size", 6.0f, 0.1f, 2000.0f, 0.5f, 60.0f).main().fmt("%.1f m").log().floorAt(0.1f)
        .tooltip("The membrane's radius: the waves die out at its edge."),
    storedFloat("wavelength", "Wavelength", 0.9f, 0.02f, 200.0f, 0.1f, 6.0f).main().fmt("%.2f m").log().floorAt(0.02f),
    storedFloat("speed", "Speed", 4.0f, 0.05f, 500.0f, 0.5f, 30.0f).main().fmt("%.1f m/s").floorAt(0.05f)
        .tooltip("How fast the rings travel outward."),
    storedChoice("plane", "Membrane faces", 0, kPlanes).main()
        .tooltip("Facing the camera, lying horizontal, or along the owner's forward axis."),
    storedColor("crestColor", "Crest glow colour", glm::vec3(0.55f, 0.85f, 1.0f)).main().sec("Crest glow"),
    storedFloat("crestEmission", "Crest glow", 0.0f, 0.0f, 40.0f, 0.0f, 6.0f).main()
        .tooltip("A faint emissive line on each crest, in HDR. 0 is a pure refraction."),
    storedFloat("chroma", "Chroma", 0.1f, 0.0f, 1.0f, 0.0f, 1.0f).main(),
    storedFloat("radialDecay", "Fades outward", 1.2f, 0.0f, 8.0f, 0.0f, 4.0f).sec("Damping")
        .tooltip("How much the waves weaken on the way to the edge (e-folds across the radius)."),
    storedFloat("ageDecay", "Settles in", 1.4f, 0.02f, 30.0f, 0.1f, 4.0f).fmt("%.2f s").floorAt(0.02f)
        .tooltip("The time the waves take to lose most of their strength."),
    storedFloat("duration", "Duration", 2.4f, 0.05f, 60.0f, 0.2f, 8.0f).fmt("%.2f s").floorAt(0.05f)
        .tooltip("How long one train lives."),
    storedFloat("maxConcurrent", "Overlapping trains", 3.0f, 1.0f, 4.0f, 1.0f, 4.0f).fmt("%.0f"),
    storedFloat("offsetX", "Offset X", 0.0f, -100000.0f, 100000.0f, -50.0f, 50.0f).fmt("%.2f m").sec("Placement")
        .tooltip("On the World: where the membrane stands. On an entity: an offset from the centre of\n"
                 "the owner's bounds, in world metres."),
    storedFloat("offsetY", "Offset Y", 0.0f, -100000.0f, 100000.0f, -50.0f, 50.0f).fmt("%.2f m"),
    storedFloat("offsetZ", "Offset Z", 0.0f, -100000.0f, 100000.0f, -50.0f, 50.0f).fmt("%.2f m"),
};

constexpr kinds::StoredRows kRows{"ripple", kFields};

struct Look {
    const char* name;
    float amplitude, radius, wavelength, speed;
    Plane plane;
    glm::vec3 crestColor;
    float crestEmission, chroma, radialDecay, ageDecay, duration;
    int everyN;
};

const Look kLooks[] = {
    // A force wall that has been touched: fine rings, a cyan crest, facing the viewer.
    {"Membrane Touch", 0.35f, 5.0f, 0.7f, 3.5f, Plane::Camera, {0.45f, 0.9f, 1.0f}, 1.5f, 0.15f, 1.0f, 1.5f, 2.5f, 4},
    // Air struck on every other beat: broad, slow, invisible but for the bend.
    {"Beat Ripple", 0.45f, 9.0f, 1.4f, 7.0f, Plane::Camera, {1.0f, 1.0f, 1.0f}, 0.0f, 0.08f, 0.8f, 1.4f, 2.0f, 2},
    // A pond surface hanging in the air.
    {"Water Membrane", 0.25f, 7.0f, 0.8f, 2.5f, Plane::Horizontal, {0.6f, 0.8f, 1.0f}, 0.0f, 0.05f, 1.6f, 1.6f, 3.5f, 4},
};

void applyLook(E& e, const Look& l) {
    kRows.set(e, "amplitude", l.amplitude);
    kRows.set(e, "radius", l.radius);
    kRows.set(e, "wavelength", l.wavelength);
    kRows.set(e, "speed", l.speed);
    kRows.set(e, "plane", static_cast<float>(l.plane));
    kRows.setRgb(e, "crestColor", l.crestColor);
    kRows.set(e, "crestEmission", l.crestEmission);
    kRows.set(e, "chroma", l.chroma);
    kRows.set(e, "radialDecay", l.radialDecay);
    kRows.set(e, "ageDecay", l.ageDecay);
    kRows.set(e, "duration", l.duration);
    e.activation = Activation::Trigger;
    e.timing.trigger = Trigger{};
    e.timing.trigger.source = TriggerSource::Beat;
    e.timing.trigger.everyN = l.everyN;
    e.style = l.name;
}
void style0(E& e) { applyLook(e, kLooks[0]); }
void style1(E& e) { applyLook(e, kLooks[1]); }
void style2(E& e) { applyLook(e, kLooks[2]); }

constexpr EffectStyle kStyles[] = {
    {"Membrane Touch", style0},
    {"Beat Ripple", style1},
    {"Water Membrane", style2},
};

// The catalog's route: the treble shimmers the waves' height.
constexpr EffectRoute kRoutes[] = {
    {"audio.treble", "amplitude", 0.2f, 20.0f, 400.0f},
};

E make(std::string name) {
    E e;
    e.name = std::move(name);
    e.kind = EffectKind::Ripple;
    e.timing.fadeIn = 0.0;
    e.timing.fadeOut = 0.0;
    applyLook(e, kLooks[1]);
    e.style.clear();
    return e;
}

EffectSchema buildSchema() {
    EffectSchema s;
    s.kind = EffectKind::Ripple;
    s.key = "ripple";
    s.enumName = "Ripple";
    s.displayName = "Ripple";
    s.description =
        "A damped train of concentric waves in an invisible membrane, started by an event: the view "
        "behind it ripples outward in rings that weaken with distance and settle, with optional glowing "
        "crests. The membrane faces the camera, lies flat, or follows its owner's forward axis.";
    s.performance = PerformanceClass::Medium;
    s.primaryCost = CostFragment | CostBandwidth | CostExtraPass;
    s.targets = targetBit(EffectTarget::Entity) | targetBit(EffectTarget::World);
    s.category = EffectCategory::Distortion;
    s.stage = RenderStage::ScreenSpace;
    s.priority = 1;
    s.addLabel = "Ripple";
    s.addTip = "Rings spreading through an invisible membrane, started on the beat or an event.";
    s.fields = kFields;
    s.styles = kStyles;
    s.routes = kRoutes;
    s.beatLeaf = "amplitude";
    s.factory = make;
    s.resolve.bucket = EffectBucket::Distortion;
    s.resolve.records = distortionRecords;
    return s;
}

} // namespace

std::size_t rippleProxies(const EffectInstance& e, const EffectContext& ctx, float envelope,
                          std::span<DistortionProxy> out) {
    if (out.empty() || envelope <= 0.0f) {
        return 0;
    }
    std::array<double, 4> starts{};
    const std::size_t want = std::min<std::size_t>(
        {out.size(), starts.size(), static_cast<std::size_t>(std::clamp(kRows.f(e, "maxConcurrent"), 1.0f, 4.0f) + 0.5f)});
    const std::size_t trains = effectEventTimes(e, ctx, std::span(starts).first(want));
    if (trains == 0) {
        return 0;
    }
    const glm::vec3 offset(kRows.f(e, "offsetX"), kRows.f(e, "offsetY"), kRows.f(e, "offsetZ"));
    glm::vec3 centre = offset;
    float ownerRadius = 0.0f;
    glm::vec3 ownerNormal(0.0f, 0.0f, 1.0f);
    if (e.owner.kind == EffectTarget::Entity) {
        NodeView view;
        if (!kinds::ownerCentre(e, ctx, offset, centre, ownerRadius, view)) {
            return 0;
        }
        const glm::vec3 z(view.world[2]);
        if (glm::length(z) > 1e-6f) {
            ownerNormal = glm::normalize(z);
        }
    } else if (e.owner.kind != EffectTarget::World) {
        return 0;
    }

    glm::vec3 normal(0.0f, 1.0f, 0.0f);
    switch (static_cast<Plane>(kRows.choice(e, "plane"))) {
    case Plane::Camera: {
        const glm::vec3 f = ctx.cameraForward;
        normal = glm::length(f) > 1e-6f ? -glm::normalize(f) : glm::vec3(0.0f, 0.0f, 1.0f);
        break;
    }
    case Plane::Horizontal: normal = glm::vec3(0.0f, 1.0f, 0.0f); break;
    case Plane::Owner: normal = ownerNormal; break;
    }
    glm::vec3 n, a0, a1;
    distortionBasis(normal, n, a0, a1);

    const float radius = std::max(kRows.f(e, "radius"), 0.1f);
    const float wavelength = std::max(kRows.f(e, "wavelength"), 0.02f);
    const float speed = std::max(kRows.f(e, "speed"), 0.05f);
    const float amplitude = std::max(kRows.f(e, "amplitude"), 0.0f);
    const float ageDecay = std::max(kRows.f(e, "ageDecay"), 0.02f);
    const float duration = std::max(kRows.f(e, "duration"), 0.05f);
    const float radialDecay = std::clamp(kRows.f(e, "radialDecay"), 0.0f, 8.0f);
    const float chroma = std::clamp(kRows.f(e, "chroma"), 0.0f, 1.0f);
    const glm::vec3 crest = kRows.rgb(e, "crestColor") * std::max(kRows.f(e, "crestEmission"), 0.0f);
    const float seed = kinds::seedOf(e.id);

    std::size_t made = 0;
    for (std::size_t i = 0; i < trains && made < out.size(); ++i) {
        const double age = ctx.seconds - starts[i];
        if (age < 0.0 || age >= static_cast<double>(duration)) {
            continue;
        }
        const float x = static_cast<float>(age / static_cast<double>(duration));
        const float life = smooth01(0.0f, 0.02f, x) * (1.0f - smooth01(0.75f, 1.0f, x)) *
                           std::exp(-static_cast<float>(age) / ageDecay);
        if (life <= 1e-4f) {
            continue;
        }
        const float travelled = speed * static_cast<float>(age);
        DistortionProxy& p = out[made++];
        p.centre = glm::vec4(centre, 0.0f);
        p.axis0 = glm::vec4(a0 * radius, static_cast<float>(DistortionShape::Disc));
        p.axis1 = glm::vec4(a1 * radius, static_cast<float>(DistortionField::Ripple));
        // The hull's thickness: enough to rasterise the disc at any angle short of edge-on. The band
        // the bend fades in over behind the membrane: a few wavelengths.
        p.axis2 = glm::vec4(n * std::max(0.02f * radius, 0.01f), std::max(2.0f * wavelength, 0.05f));
        p.terms = glm::vec4(1.0f, 0.0f, 0.0f, amplitude * life * envelope);
        p.motion = glm::vec4(travelled / radius, radialDecay, 0.0f, 0.0f);
        p.shape = glm::vec4(radius / wavelength, 0.12f, 0.0f, 0.0f);
        // Phase in cycles; wrapped so a long-lived train keeps float precision.
        p.noise = glm::vec4(std::fmod(travelled / wavelength, 4096.0f), chroma, 0.0f, seed);
        p.rim = glm::vec4(crest * life * envelope, 0.0f);
    }
    return made;
}

const EffectSchema& rippleSchema() {
    static const EffectSchema kSchema = buildSchema();
    return kSchema;
}

} // namespace avgen::world
