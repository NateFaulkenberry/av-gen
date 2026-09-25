// Spiral (Effect Library Wave 2, catalog-motion.md "Spiral"): the owner corkscrews up or down around
// its own position -- an ascension, a vortex drift, a seed falling.
//
// XFORM: an orbit whose radius and height move with the phase s of each period,
//
//     r(s) = mix(r0, r1, s),  h(s) = mix(h0, h1, s),  angle = turns 2 pi s,
//
// with s = fract(t / period + phase) (Loop) or its ping-pong (the path is retraced on the way back,
// so the motion never jumps). A pure function of the transport second.

#include "world/effects/kinds/xform_rows.hpp"

namespace avgen::world {
namespace {

using E = EffectInstance;
using namespace xform_rows;

constexpr const char* const kLoopNames[] = {"Loop", "Ping-pong"};
constexpr const char* const kFaceNames[] = {"Keep heading", "Turn with spiral"};

constexpr EffectField kFields[] = {
    storedFloat("r0", "Start radius", 0.5f, 0.0f, 500.0f, 0.0f, 20.0f).main().fmt("%.2f m").sec("Path"),
    storedFloat("r1", "End radius", 2.5f, 0.0f, 500.0f, 0.0f, 20.0f).main().fmt("%.2f m"),
    storedFloat("h0", "Start height", 0.0f, -500.0f, 500.0f, -20.0f, 20.0f).main().fmt("%.2f m"),
    storedFloat("h1", "End height", 5.0f, -500.0f, 500.0f, -20.0f, 20.0f).main().fmt("%.2f m")
        .tooltip("Above the owner's own position at the end of the path. Below the start height,\n"
                 "the spiral falls instead."),
    storedFloat("turns", "Turns", 3.0f, 0.0f, 64.0f, 0.0f, 12.0f).main()
        .tooltip("Laps around the axis from the start of the path to its end."),
    storedFloat("period", "Period", 10.0f, 0.1f, 600.0f, 0.5f, 60.0f).main().fmt("%.1f s").floorAt(0.1f)
        .tooltip("Seconds from the start of the path to its end."),
    storedChoice("loopMode", "Repeat", 1, kLoopNames).main()
        .tooltip("Loop: starts again from the beginning each period -- the owner jumps back, so hide it\n"
                 "with a cut or a fade. Ping-pong: runs back down the same path, with no jump."),
    storedFloat("phase", "Phase", 0.0f, 0.0f, 1.0f, 0.0f, 1.0f).fmt("%.2f").sec("Timing"),
    storedBool("reverse", "Clockwise", false),
    storedChoice("face", "Facing", 0, kFaceNames),
};

constexpr Rows kRows{"spiral", kFields};

struct Look {
    const char* name;
    float r0, r1, h0, h1, turns, period;
    int loop;
};

constexpr Look kLooks[] = {
    {"Ascension", 0.3f, 2.5f, 0.0f, 6.0f, 3.0f, 12.0f, 1},
    {"Vortex Drift", 3.0f, 0.4f, 0.0f, 2.0f, 5.0f, 9.0f, 1},
    {"Seed Fall", 0.4f, 0.6f, 4.0f, 0.0f, 4.0f, 7.0f, 1},
};

void applyLook(E& e, const Look& l) {
    kRows.put(e, "r0", l.r0);
    kRows.put(e, "r1", l.r1);
    kRows.put(e, "h0", l.h0);
    kRows.put(e, "h1", l.h1);
    kRows.put(e, "turns", l.turns);
    kRows.put(e, "period", l.period);
    kRows.put(e, "loopMode", static_cast<float>(l.loop));
    e.style = l.name;
}
void style0(E& e) { applyLook(e, kLooks[0]); }
void style1(E& e) { applyLook(e, kLooks[1]); }
void style2(E& e) { applyLook(e, kLooks[2]); }

constexpr EffectStyle kStyles[] = {{kLooks[0].name, style0}, {kLooks[1].name, style1}, {kLooks[2].name, style2}};

constexpr EffectRoute kRoutes[] = {
    {"audio.rms", "r1", 1.2f, 80.0f, 600.0f},
};

E make(std::string name) {
    E e;
    e.name = std::move(name);
    e.kind = EffectKind::Spiral;
    e.activation = Activation::Always;
    applyLook(e, kLooks[0]);
    return e;
}

EffectSchema buildSchema() {
    EffectSchema s;
    s.kind = EffectKind::Spiral;
    s.key = "spiral";
    s.enumName = "Spiral";
    s.displayName = "Spiral";
    s.description =
        "The owner corkscrews around its own position, rising or falling while its circle widens or "
        "narrows -- an ascension, a vortex drift, a falling seed. It can loop or run back down the same "
        "path. Visual only: children and effects follow it, its simulation does not.";
    s.performance = PerformanceClass::VeryLow;
    s.primaryCost = CostCpu;
    s.targets = targetBit(EffectTarget::Entity);
    s.category = EffectCategory::Motion;
    s.stage = RenderStage::Geometry;
    s.priority = 0;
    s.addLabel = "Spiral";
    s.addTip = "The owner corkscrews up or down around its own position (visual only).";
    s.fields = kFields;
    s.styles = kStyles;
    s.routes = kRoutes;
    s.beatLeaf = "r1";
    s.factory = make;
    s.resolve.bucket = EffectBucket::Transform;
    s.resolve.records = transformRecords;
    return s;
}

} // namespace

bool spiralTransform(const EffectInstance& e, const EffectContext& ctx, double /*age*/, TransformContribution& out) {
    double s = cycles(ctx.seconds, kRows.get(e, "period"), kRows.get(e, "phase"));
    if (kRows.choice(e, "loopMode") == 1) {
        s = 1.0 - std::abs(2.0 * s - 1.0);
    }
    const auto u = static_cast<float>(s);
    const float r = std::max(glm::mix(kRows.get(e, "r0"), kRows.get(e, "r1"), u), 0.0f);
    const float h = glm::mix(kRows.get(e, "h0"), kRows.get(e, "h1"), u);
    const float dir = kRows.flag(e, "reverse") ? -1.0f : 1.0f;
    const float angle = kRows.get(e, "turns") * 2.0f * kPi * u * dir;
    out.translation = glm::vec3(std::cos(angle) * r, h, -std::sin(angle) * r);
    if (kRows.choice(e, "face") == 1) {
        out.rotation = glm::angleAxis(angle, glm::vec3(0.0f, 1.0f, 0.0f));
    }
    return true;
}

const EffectSchema& spiralSchema() {
    static const EffectSchema kSchema = buildSchema();
    return kSchema;
}

} // namespace avgen::world
