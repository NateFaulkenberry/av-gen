// Float (Effect Library Wave 2, catalog-motion.md "Float"): gentle buoyant idle motion -- a slow bob,
// a slight wobble and a lazy yaw drift, never exactly periodic.
//
// XFORM: each channel is a pair of sines at incommensurate frequency ratios,
//
//     y = A (sin(w t + p1) + 0.35 sin(2.31 w t + p2)) / 1.35,
//
// the tilt and sway channels at other ratios (0.73/1.93, 0.61/1.67, 0.53/1.41), and the yaw at 0.29.
// The phases are seeded from the instance id and the Seed row, so two floaters side by side never
// bob together unless their seeds say so. A pure function of the transport second.
//
// Not to be confused with `src/scene/floaters.*` (ADR-099 §13): procedural instances carried along a
// water surface. Same principle (no integration), different thing.
//
// Not yet: the catalog's `sync` (pull the phase toward the beat). The context has no beat phase
// before the scene, and a phase that followed a tempo change would stop being a pure function of t.

#include "world/effects/kinds/xform_rows.hpp"

namespace avgen::world {
namespace {

using E = EffectInstance;
using namespace xform_rows;

constexpr EffectField kFields[] = {
    storedFloat("height", "Bob height", 0.35f, 0.0f, 50.0f, 0.0f, 3.0f).main().fmt("%.2f m").sec("Float")
        .tooltip("How far up and down the owner bobs, in metres (the peak of the slow wave)."),
    storedFloat("period", "Period", 4.0f, 0.2f, 120.0f, 0.5f, 20.0f).main().fmt("%.1f s").floorAt(0.2f)
        .tooltip("Seconds per slow bob. The motion never exactly repeats: a faster, smaller wave rides\n"
                 "on it at an unrelated rate."),
    storedFloat("tilt", "Wobble", 3.0f, 0.0f, 45.0f, 0.0f, 15.0f).main().fmt("%.1f deg")
        .tooltip("How far the owner rocks, forward-back and side to side."),
    storedFloat("yaw", "Yaw drift", 2.0f, 0.0f, 180.0f, 0.0f, 30.0f).main().fmt("%.1f deg")
        .tooltip("A slow turn back and forth about the vertical."),
    storedFloat("sway", "Sway", 0.06f, 0.0f, 20.0f, 0.0f, 1.0f).fmt("%.2f m").sec("Drift")
        .tooltip("A slow sideways drift, in metres."),
    storedFloat("seed", "Seed", 0.0f, 0.0f, 1000.0f, 0.0f, 100.0f).fmt("%.0f")
        .tooltip("Picks the phases. Two floaters with the same seed on the same owner name bob together."),
};

constexpr Rows kRows{"float", kFields};

struct Look {
    const char* name;
    float height, period, tilt, yaw, sway;
};

constexpr Look kLooks[] = {
    {"Hovering Craft", 0.35f, 4.0f, 3.0f, 2.0f, 0.06f},
    {"Buoy", 0.25f, 3.0f, 9.0f, 6.0f, 0.1f},
    {"Dream Float", 0.8f, 9.0f, 5.0f, 12.0f, 0.3f},
};

void applyLook(E& e, const Look& l) {
    kRows.put(e, "height", l.height);
    kRows.put(e, "period", l.period);
    kRows.put(e, "tilt", l.tilt);
    kRows.put(e, "yaw", l.yaw);
    kRows.put(e, "sway", l.sway);
    e.style = l.name;
}
void style0(E& e) { applyLook(e, kLooks[0]); }
void style1(E& e) { applyLook(e, kLooks[1]); }
void style2(E& e) { applyLook(e, kLooks[2]); }

constexpr EffectStyle kStyles[] = {{kLooks[0].name, style0}, {kLooks[1].name, style1}, {kLooks[2].name, style2}};

constexpr EffectRoute kRoutes[] = {
    {"audio.bass", "height", 0.25f, 120.0f, 800.0f},
};

E make(std::string name) {
    E e;
    e.name = std::move(name);
    e.kind = EffectKind::Float;
    e.activation = Activation::Always;
    applyLook(e, kLooks[0]);
    return e;
}

EffectSchema buildSchema() {
    EffectSchema s;
    s.kind = EffectKind::Float;
    s.key = "float";
    s.enumName = "Float";
    s.displayName = "Float";
    s.description =
        "Gentle buoyant idle motion: a slow bob with a slight wobble and a lazy turn, never exactly "
        "repeating, and never in step with another floater. A hovering craft, a buoy, a dream. Visual "
        "only: children and effects follow it, its simulation does not.";
    s.performance = PerformanceClass::VeryLow;
    s.primaryCost = CostCpu;
    s.targets = targetBit(EffectTarget::Entity);
    s.category = EffectCategory::Motion;
    s.stage = RenderStage::Geometry;
    s.priority = 0;
    s.addLabel = "Float";
    s.addTip = "A slow, never-repeating bob and wobble (visual only).";
    s.fields = kFields;
    s.styles = kStyles;
    s.routes = kRoutes;
    s.beatLeaf = "height";
    s.factory = make;
    s.resolve.bucket = EffectBucket::Transform;
    s.resolve.records = transformRecords;
    return s;
}

// Two sines at an incommensurate ratio, normalised to peak near 1.
float pair(double wt, double ratioA, double ratioB, float pa, float pb) {
    return static_cast<float>((std::sin(wt * ratioA + static_cast<double>(pa)) +
                               0.35 * std::sin(wt * ratioB + static_cast<double>(pb))) /
                              1.35);
}

} // namespace

bool floatTransform(const EffectInstance& e, const EffectContext& ctx, double /*age*/, TransformContribution& out) {
    const float seed = transformSeed(e.id, kRows.get(e, "seed"));
    // Eight phases from one seed, by the golden ratio, so they are spread and unrelated.
    const auto phase = [seed](int k) {
        const float v = seed + 0.6180339887f * static_cast<float>(k);
        return (v - std::floor(v)) * 2.0f * kPi;
    };
    // w t in double and every sine evaluated in double: the ratios are irrational, so there is no
    // common period to wrap on, and a double argument keeps sub-microradian precision for days.
    const double period = std::max(static_cast<double>(kRows.get(e, "period")), 0.2);
    const double wt = 2.0 * 3.14159265358979323846 * ctx.seconds / period;
    const float height = kRows.get(e, "height");
    const float tilt = kRows.get(e, "tilt") * kDeg;
    const float yaw = kRows.get(e, "yaw") * kDeg;
    const float sway = kRows.get(e, "sway");
    out.translation = glm::vec3(sway * pair(wt, 0.53, 1.41, phase(6), phase(7)),
                                height * pair(wt, 1.0, 2.31, phase(0), phase(1)),
                                sway * pair(wt, 0.47, 1.29, phase(7), phase(5)));
    const float pitch = tilt * pair(wt, 0.73, 1.93, phase(2), phase(3));
    const float roll = tilt * pair(wt, 0.61, 1.67, phase(4), phase(5));
    const float turn = yaw * static_cast<float>(std::sin(wt * 0.29 + static_cast<double>(phase(6))));
    out.rotation = yawPitchRoll(turn, pitch, roll);
    return height > 0.0f || tilt > 0.0f || yaw > 0.0f || sway > 0.0f;
}

const EffectSchema& floatSchema() {
    static const EffectSchema kSchema = buildSchema();
    return kSchema;
}

} // namespace avgen::world
