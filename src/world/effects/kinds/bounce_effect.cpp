// Bounce (Effect Library Wave 2, catalog-motion.md "Bounce"): a springy hop with squash and stretch,
// continuous (hopping) or after each activation (a recoil that settles).
//
// XFORM: a vertical translation and a non-uniform scale about the owner's base (the Pivot height).
//
//   Continuous:  y = H sin(pi s),                 s = fract(t / period)
//   Triggered:   y = H e^(-zeta w a) |sin(w_d a)|, a = t - t0, w = 2 pi f, w_d = w sqrt(1 - zeta^2)
//
// In both, the hop's own phase says where the body is in its arc: squashed at contact, stretched on
// the way up and down, round at the apex. Volume is preserved: s_xz = 1 / sqrt(s_y).
//
// `t0` is the start of the instance's current activation pass (a Window, or each Repeat). TRIGGER (a
// parallel slice) will supply it from events -- a beat, an impact -- and only where `age` comes from
// changes.

#include "world/effects/kinds/xform_rows.hpp"

namespace avgen::world {
namespace {

using E = EffectInstance;
using namespace xform_rows;

constexpr const char* const kModeNames[] = {"Continuous", "Triggered"};

constexpr EffectField kFields[] = {
    storedChoice("mode", "Mode", 0, kModeNames).main().sec("Bounce")
        .tooltip("Continuous: hops for as long as the effect is active.\n"
                 "Triggered: one springy recoil from each activation (a Window, or each Repeat),\n"
                 "settling at the Damping rate."),
    storedFloat("height", "Height", 0.5f, 0.0f, 50.0f, 0.0f, 3.0f).main().fmt("%.2f m")
        .tooltip("How high each hop goes, in metres (the first hop, when Triggered)."),
    storedFloat("period", "Hop time", 0.6f, 0.05f, 20.0f, 0.1f, 3.0f).main().fmt("%.2f s").floorAt(0.05f)
        .tooltip("Continuous: seconds from one landing to the next."),
    storedFloat("squash", "Squash", 0.25f, 0.0f, 0.9f, 0.0f, 0.6f).main()
        .tooltip("How much the body flattens on landing (0.25 = a quarter shorter)."),
    storedFloat("stretch", "Stretch", 0.15f, 0.0f, 2.0f, 0.0f, 0.6f).main()
        .tooltip("How much it lengthens while it moves fastest, leaving and approaching the ground."),
    storedFloat("frequency", "Spring rate", 2.5f, 0.1f, 20.0f, 0.5f, 8.0f).fmt("%.1f Hz").floorAt(0.1f)
        .sec("Triggered")
        .tooltip("Triggered: how fast the recoil oscillates."),
    storedFloat("damping", "Damping", 0.18f, 0.0f, 0.95f, 0.0f, 0.8f).ceilAt(0.95f)
        .tooltip("Triggered: how quickly the recoil settles. 0 never settles."),
    storedFloat("pivotHeight", "Pivot height", 0.0f, -100.0f, 100.0f, -5.0f, 5.0f).fmt("%.2f m").sec("Shape")
        .tooltip("Where the squash is anchored, in the owner's own metres above its origin. The\n"
                 "default is the origin, which for most assets is where they stand."),
};

constexpr Rows kRows{"bounce", kFields};

struct Look {
    const char* name;
    int mode;
    float height, period, squash, stretch, frequency, damping;
};

constexpr Look kLooks[] = {
    {"Beat Hop", 0, 0.5f, 0.5f, 0.25f, 0.15f, 2.5f, 0.18f},
    {"Jelly", 0, 0.2f, 0.9f, 0.4f, 0.3f, 2.5f, 0.18f},
    {"Landing Recoil", 1, 0.35f, 0.6f, 0.3f, 0.12f, 3.0f, 0.25f},
};

void applyLook(E& e, const Look& l) {
    kRows.put(e, "mode", static_cast<float>(l.mode));
    kRows.put(e, "height", l.height);
    kRows.put(e, "period", l.period);
    kRows.put(e, "squash", l.squash);
    kRows.put(e, "stretch", l.stretch);
    kRows.put(e, "frequency", l.frequency);
    kRows.put(e, "damping", l.damping);
    e.style = l.name;
}
void style0(E& e) { applyLook(e, kLooks[0]); }
void style1(E& e) { applyLook(e, kLooks[1]); }
void style2(E& e) { applyLook(e, kLooks[2]); }

constexpr EffectStyle kStyles[] = {{kLooks[0].name, style0}, {kLooks[1].name, style1}, {kLooks[2].name, style2}};

// The catalog's beat trigger arrives with TRIGGER; until then each beat lifts the hop a little.
constexpr EffectRoute kRoutes[] = {
    {"beat.pulse", "height", 0.3f, 10.0f, 260.0f},
};

E make(std::string name) {
    E e;
    e.name = std::move(name);
    e.kind = EffectKind::Bounce;
    e.activation = Activation::Always;
    applyLook(e, kLooks[0]);
    return e;
}

EffectSchema buildSchema() {
    EffectSchema s;
    s.kind = EffectKind::Bounce;
    s.key = "bounce";
    s.enumName = "Bounce";
    s.displayName = "Bounce";
    s.description =
        "The owner hops with squash and stretch -- flattening as it lands, lengthening as it leaves -- "
        "either continuously or as a springy recoil that settles after each activation. Volume is "
        "preserved, and shadows squash with it. Visual only: its simulation does not hop.";
    s.performance = PerformanceClass::VeryLow;
    s.primaryCost = CostCpu;
    s.targets = targetBit(EffectTarget::Entity);
    s.category = EffectCategory::Motion;
    s.stage = RenderStage::Geometry;
    s.priority = 0;
    s.addLabel = "Bounce";
    s.addTip = "A squash-and-stretch hop, continuous or a recoil (visual only).";
    s.fields = kFields;
    s.styles = kStyles;
    s.routes = kRoutes;
    s.beatLeaf = "height";
    s.factory = make;
    s.resolve.bucket = EffectBucket::Transform;
    s.resolve.records = transformRecords;
    return s;
}

} // namespace

bool bounceTransform(const EffectInstance& e, const EffectContext& ctx, double age, TransformContribution& out) {
    const float height = std::max(kRows.get(e, "height"), 0.0f);
    double hop = 0.0;   // phase within the current hop, [0, 1): 0 and 1 are contact
    float amount = 1.0f; // how much of the full hop is left (Triggered decays)
    if (kRows.choice(e, "mode") == 1) {
        const double w = 2.0 * 3.14159265358979323846 * static_cast<double>(std::max(kRows.get(e, "frequency"), 0.1f));
        const double zeta = std::clamp(static_cast<double>(kRows.get(e, "damping")), 0.0, 0.95);
        const double wd = w * std::sqrt(1.0 - zeta * zeta);
        const double a = std::max(age, 0.0);
        amount = static_cast<float>(std::exp(-zeta * w * a));
        const double c = wd * a / 3.14159265358979323846;
        hop = c - std::floor(c);
    } else {
        hop = cycles(ctx.seconds, kRows.get(e, "period"), 0.0f);
    }
    if (!(amount > 1e-4f)) {
        return false;
    }
    const auto s = static_cast<float>(hop);
    const float arc = std::sin(kPi * s);        // 0 at contact, 1 at the apex
    const float speed = std::abs(std::cos(kPi * s)); // 1 at contact, 0 at the apex
    // Contact: the last and first 12% of the hop, eased, so the squash is a landing and not a flicker.
    const float edge = std::min(s, 1.0f - s) / 0.12f;
    const float contact = edge < 1.0f ? (1.0f - edge) * (1.0f - edge) * (3.0f - 2.0f * (1.0f - edge)) : 0.0f;
    const float sy = std::max(1.0f + amount * (kRows.get(e, "stretch") * speed * (1.0f - contact) -
                                               kRows.get(e, "squash") * contact),
                              0.05f);
    const float sxz = 1.0f / std::sqrt(sy);
    out.translation = glm::vec3(0.0f, height * amount * arc, 0.0f);
    out.scale = glm::vec3(sxz, sy, sxz);
    out.pivot = glm::vec3(0.0f, kRows.get(e, "pivotHeight"), 0.0f);
    return height > 0.0f || kRows.get(e, "squash") > 0.0f || kRows.get(e, "stretch") > 0.0f;
}

const EffectSchema& bounceSchema() {
    static const EffectSchema kSchema = buildSchema();
    return kSchema;
}

} // namespace avgen::world
