// Shake (Effect Library Wave 2, catalog-motion.md "Shake", Entity owner): jittery displacement of an
// object -- a rumble, an impact, nerves.
//
// XFORM, after Eiserloh (GDC 2016, "Juicing Your Cameras With Math"): smooth noise, never random per
// frame, with amplitude proportional to trauma squared,
//
//     offset = trauma^2 * decay(age) * noise(seed, t * frequency) * amplitude   (and the same for rotation)
//
// where decay(age) = exp(-decay * age) and `age = t - t0` is the seconds since the instance's current
// activation pass began. Decay 0 is the continuous mode (a rumble). With a decay, each pass of the
// activation -- a Window's start, or every `repeatSeconds` -- is an impact that settles. TRIGGER (a
// parallel slice) will supply `t0` from events instead; only where `age` comes from changes.
//
// The noise is a C2-continuous 1-D gradient noise, two octaves per channel, six channels (three of
// translation, three of rotation), seeded from the instance id: a shake at 30 fps and at 120 fps
// traces the same curve, and a scrub lands on the same displacement.
//
// Camera Shake is NOT this type: ADR-098's camera shake exists and is the camera's authority.

#include "world/effects/kinds/xform_rows.hpp"

namespace avgen::world {
namespace {

using E = EffectInstance;
using namespace xform_rows;

constexpr EffectField kFields[] = {
    storedFloat("trauma", "Trauma", 0.6f, 0.0f, 1.0f, 0.0f, 1.0f).main().sec("Shake")
        .tooltip("How hard it shakes, 0 to 1. The displacement grows with its square, so small values\n"
                 "stay subtle and 1 is violent. The default route adds the bass."),
    storedFloat("amplitude", "Distance", 0.15f, 0.0f, 20.0f, 0.0f, 1.0f).main().fmt("%.2f m")
        .tooltip("The furthest the owner moves at full trauma, in metres."),
    storedFloat("rotation", "Rotation", 2.5f, 0.0f, 45.0f, 0.0f, 15.0f).main().fmt("%.1f deg")
        .tooltip("The furthest it turns at full trauma, in degrees."),
    storedFloat("frequency", "Frequency", 9.0f, 0.1f, 60.0f, 0.5f, 30.0f).main().fmt("%.1f Hz").floorAt(0.1f)
        .tooltip("How fast it jitters. Low is a lurch, high is a buzz."),
    storedFloat("decay", "Decay", 0.0f, 0.0f, 40.0f, 0.0f, 12.0f).main().fmt("%.1f /s")
        .tooltip("0 shakes for as long as the effect is active (a rumble).\n"
                 "Above 0, each activation is an impact that settles: set Activation to Window, or a\n"
                 "Repeat, and the shake starts at each and dies away at this rate."),
    storedFloat("seed", "Seed", 0.0f, 0.0f, 1000.0f, 0.0f, 100.0f).fmt("%.0f").sec("Noise"),
};

constexpr Rows kRows{"shake", kFields};

struct Look {
    const char* name;
    float trauma, amplitude, rotation, frequency, decay;
};

constexpr Look kLooks[] = {
    {"Rumble", 0.6f, 0.15f, 2.5f, 9.0f, 0.0f},
    {"Impact", 1.0f, 0.35f, 6.0f, 14.0f, 4.0f},
    {"Nervous", 0.5f, 0.03f, 1.5f, 22.0f, 0.0f},
};

void applyLook(E& e, const Look& l) {
    kRows.put(e, "trauma", l.trauma);
    kRows.put(e, "amplitude", l.amplitude);
    kRows.put(e, "rotation", l.rotation);
    kRows.put(e, "frequency", l.frequency);
    kRows.put(e, "decay", l.decay);
    e.style = l.name;
}
void style0(E& e) { applyLook(e, kLooks[0]); }
void style1(E& e) { applyLook(e, kLooks[1]); }
void style2(E& e) { applyLook(e, kLooks[2]); }

constexpr EffectStyle kStyles[] = {{kLooks[0].name, style0}, {kLooks[1].name, style1}, {kLooks[2].name, style2}};

// The catalog's Rumble: the bass adds trauma. Squared downstream, so quiet passages stay still.
constexpr EffectRoute kRoutes[] = {
    {"audio.bass", "trauma", 0.5f, 30.0f, 350.0f},
};

E make(std::string name) {
    E e;
    e.name = std::move(name);
    e.kind = EffectKind::Shake;
    e.activation = Activation::Always;
    applyLook(e, kLooks[0]);
    return e;
}

EffectSchema buildSchema() {
    EffectSchema s;
    s.kind = EffectKind::Shake;
    s.key = "shake";
    s.enumName = "Shake";
    s.displayName = "Shake";
    s.description =
        "The owner shakes: a smooth jitter in position and rotation that grows with the square of its "
        "trauma, continuous (a rumble) or settling after each activation (an impact). Visual only: "
        "children and effects shake with it, its simulation and anything framing it do not.";
    s.performance = PerformanceClass::VeryLow;
    s.primaryCost = CostCpu;
    s.targets = targetBit(EffectTarget::Entity);
    s.category = EffectCategory::Motion;
    s.stage = RenderStage::Geometry;
    s.priority = 0;
    s.addLabel = "Shake";
    s.addTip = "A smooth jitter, continuous or settling after each activation (visual only).";
    s.fields = kFields;
    s.styles = kStyles;
    s.routes = kRoutes;
    s.beatLeaf = "trauma";
    s.factory = make;
    s.resolve.bucket = EffectBucket::Transform;
    s.resolve.records = transformRecords;
    return s;
}

float twoOctaves(double x, std::uint32_t channel, float seed) {
    return (motionNoise(x, channel, seed) + 0.5f * motionNoise(x * 2.13, channel + 16u, seed)) / 1.5f;
}

} // namespace

bool shakeTransform(const EffectInstance& e, const EffectContext& ctx, double age, TransformContribution& out) {
    const float trauma = std::clamp(kRows.get(e, "trauma"), 0.0f, 1.0f);
    const float decay = std::max(kRows.get(e, "decay"), 0.0f);
    const float envelope = trauma * trauma * static_cast<float>(std::exp(-static_cast<double>(decay) * std::max(age, 0.0)));
    if (!(envelope > 1e-5f)) {
        return false;
    }
    const float seed = transformSeed(e.id, kRows.get(e, "seed"));
    const double x = ctx.seconds * static_cast<double>(std::max(kRows.get(e, "frequency"), 0.1f));
    const float move = kRows.get(e, "amplitude") * envelope;
    const float turn = kRows.get(e, "rotation") * kDeg * envelope;
    out.translation = move * glm::vec3(twoOctaves(x, 0u, seed), twoOctaves(x, 1u, seed), twoOctaves(x, 2u, seed));
    out.rotation = yawPitchRoll(turn * twoOctaves(x, 3u, seed), turn * twoOctaves(x, 4u, seed),
                                turn * twoOctaves(x, 5u, seed));
    return true;
}

const EffectSchema& shakeSchema() {
    static const EffectSchema kSchema = buildSchema();
    return kSchema;
}

} // namespace avgen::world
