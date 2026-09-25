// Orbit (Effect Library Wave 2, catalog-motion.md "Orbit"): the owner circles its own authored
// position -- a moon, a satellite, a guardian orb, a firefly on a loop.
//
// XFORM (transform_frame.hpp): a parent-space translation on a circle or ellipse, optionally tilted
// out of the horizontal, plus an optional local turn so the same side keeps facing the centre, and a
// bank into the turn. A pure function of the transport second:
//
//     theta = 2 pi (t / period + phase),   offset = R_tilt (cos theta r, 0, sin theta r ecc)
//
// Visual-only: the owner's simulation, its hero anchor and anything framing it read the un-orbited
// position; its children, lights, emitters and effects follow the drawn one.
//
// Not yet: a pivot on ANOTHER entity (the catalog's `EffectEndpoint` pivot). That needs the other
// node's position before the flatten, which is the last step's entity state -- allowed, but not
// wired in this wave. The pivot is the owner's own position.

#include "world/effects/kinds/xform_rows.hpp"

namespace avgen::world {
namespace {

using E = EffectInstance;
using namespace xform_rows;

constexpr const char* const kFaceNames[] = {"Keep heading", "Turn with orbit"};

constexpr EffectField kFields[] = {
    storedFloat("radius", "Radius", 3.0f, 0.0f, 500.0f, 0.0f, 30.0f).main().fmt("%.2f m").sec("Orbit")
        .tooltip("How far from its own position the owner circles, in metres."),
    storedFloat("period", "Period", 8.0f, 0.1f, 600.0f, 0.5f, 60.0f).main().fmt("%.1f s").floorAt(0.1f)
        .tooltip("Seconds per lap. The angle is a pure function of the timeline, so a scrub lands\n"
                 "exactly where a play does -- which is also why no route should drive it."),
    storedFloat("phase", "Phase", 0.0f, 0.0f, 1.0f, 0.0f, 1.0f).main()
        .tooltip("Where on the lap the owner is at t = 0, as a fraction of a lap."),
    storedFloat("eccentricity", "Roundness", 1.0f, 0.0f, 1.0f, 0.05f, 1.0f).main()
        .tooltip("1 is a circle; lower squeezes it into an ellipse, as wide but shallower."),
    storedBool("reverse", "Clockwise", false).main(),
    storedChoice("face", "Facing", 0, kFaceNames).main()
        .tooltip("Keep heading: the owner slides round the circle without turning.\n"
                 "Turn with orbit: it turns once per lap, so the same side always faces the centre."),
    storedFloat("tilt", "Tilt", 0.0f, -90.0f, 90.0f, -60.0f, 60.0f).fmt("%.0f deg").sec("Plane")
        .tooltip("Tilts the orbit out of the horizontal."),
    storedFloat("tiltHeading", "Tilt direction", 0.0f, -360.0f, 360.0f, 0.0f, 360.0f).fmt("%.0f deg")
        .tooltip("Which way the tilted orbit leans, around the vertical."),
    storedFloat("bank", "Bank", 0.0f, -60.0f, 60.0f, -45.0f, 45.0f).fmt("%.0f deg")
        .tooltip("Rolls the owner into the turn. Most natural with Facing: Turn with orbit."),
};

constexpr Rows kRows{"orbit", kFields};

struct Look {
    const char* name;
    float radius, period, ecc, tilt, bank;
    int face;
};

constexpr Look kLooks[] = {
    {"Moon", 6.0f, 24.0f, 1.0f, 8.0f, 0.0f, 1},
    {"Satellite", 3.0f, 6.0f, 1.0f, 35.0f, 0.0f, 1},
    {"Firefly Circle", 0.8f, 2.5f, 0.7f, 12.0f, 0.0f, 0},
    {"Guardian Orb", 1.6f, 4.0f, 1.0f, 0.0f, 20.0f, 1},
};

void applyLook(E& e, const Look& l) {
    kRows.put(e, "radius", l.radius);
    kRows.put(e, "period", l.period);
    kRows.put(e, "eccentricity", l.ecc);
    kRows.put(e, "tilt", l.tilt);
    kRows.put(e, "bank", l.bank);
    kRows.put(e, "face", static_cast<float>(l.face));
    e.style = l.name;
}
void style0(E& e) { applyLook(e, kLooks[0]); }
void style1(E& e) { applyLook(e, kLooks[1]); }
void style2(E& e) { applyLook(e, kLooks[2]); }
void style3(E& e) { applyLook(e, kLooks[3]); }

constexpr EffectStyle kStyles[] = {
    {kLooks[0].name, style0}, {kLooks[1].name, style1}, {kLooks[2].name, style2}, {kLooks[3].name, style3}};

// The catalog's route: the bass swells the orbit. Not the period -- a modulated rate would make the
// angle an integral of the music, which a scrub cannot reproduce.
constexpr EffectRoute kRoutes[] = {
    {"audio.bass", "radius", 0.6f, 90.0f, 620.0f},
};

E make(std::string name) {
    E e;
    e.name = std::move(name);
    e.kind = EffectKind::Orbit;
    e.activation = Activation::Always;
    applyLook(e, kLooks[1]);
    return e;
}

EffectSchema buildSchema() {
    EffectSchema s;
    s.kind = EffectKind::Orbit;
    s.key = "orbit";
    s.enumName = "Orbit";
    s.displayName = "Orbit";
    s.description =
        "The owner circles its own position -- a moon, a satellite, a guardian orb. The path can be "
        "tilted and squeezed into an ellipse, and the owner can turn with it and bank into the turn. "
        "Its children, lights, emitters and effects go with it; its simulation and anything following "
        "it do not.";
    s.performance = PerformanceClass::VeryLow;
    s.primaryCost = CostCpu;
    s.targets = targetBit(EffectTarget::Entity);
    s.category = EffectCategory::Motion;
    s.stage = RenderStage::Geometry;
    s.priority = 0;
    s.addLabel = "Orbit";
    s.addTip = "The owner circles its own position (visual only).";
    s.fields = kFields;
    s.styles = kStyles;
    s.routes = kRoutes;
    s.beatLeaf = "radius";
    s.factory = make;
    s.resolve.bucket = EffectBucket::Transform;
    s.resolve.records = transformRecords;
    return s;
}

} // namespace

bool orbitTransform(const EffectInstance& e, const EffectContext& ctx, double /*age*/, TransformContribution& out) {
    const float radius = std::max(kRows.get(e, "radius"), 0.0f);
    if (!(radius > 1e-5f)) {
        return false;
    }
    const float dir = kRows.flag(e, "reverse") ? -1.0f : 1.0f;
    const auto theta = static_cast<float>(2.0 * 3.14159265358979323846 *
                                          cycles(ctx.seconds, kRows.get(e, "period"), kRows.get(e, "phase"))) *
                       dir;
    const float ecc = std::clamp(kRows.get(e, "eccentricity"), 0.0f, 1.0f);
    // +theta runs +X towards -Z (counter-clockwise seen from above, the right-hand turn about +Y).
    const glm::vec3 flat(std::cos(theta) * radius, 0.0f, -std::sin(theta) * radius * ecc);
    const glm::quat plane = glm::angleAxis(kRows.get(e, "tiltHeading") * kDeg, glm::vec3(0.0f, 1.0f, 0.0f)) *
                            glm::angleAxis(kRows.get(e, "tilt") * kDeg, glm::vec3(1.0f, 0.0f, 0.0f));
    out.translation = plane * flat;
    const float yaw = kRows.choice(e, "face") == 1 ? theta : 0.0f;
    out.rotation = yawPitchRoll(yaw, 0.0f, kRows.get(e, "bank") * kDeg * dir);
    return true;
}

const EffectSchema& orbitSchema() {
    static const EffectSchema kSchema = buildSchema();
    return kSchema;
}

} // namespace avgen::world
