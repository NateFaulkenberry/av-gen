// Breathing (Effect Library Wave 2, catalog-organic.md): a slow periodic swelling of the owner.
//
// **What it is.** FXL's inflate sub-block (world/effects/entity_fx.hpp): each vertex moves along its
// normal by `amplitude * breath(t) * region(h)`, where `breath` is one asymmetric cycle (a quick
// inhale, a long exhale) and `region` an optional band along the owner's height (a chest, the middle
// of a pod). Real geometry: it runs in the vertex stage of the lit pass, the depth prepass and every
// shadow view, so the silhouette, the shadow and the depth all breathe together; and at the previous
// frame's time for the velocity target, so motion blur and temporal filtering see the swell.
//
// **Stateless.** The breath is a function of the transport second (`frame.params.x` in the shader),
// so second N is the same played or scrubbed (ADR-091). A modulated rate jumps the phase, as Pulse's
// does; route to Amplitude instead.
//
// **Stacking.** One inflate per owner (a second Breathing is Dropped by name); it SUMS with an
// Organic Pulsation's travelling bulge and a Motion Smear on the same owner (§7: displacements sum,
// one slot per mode).

#include "world/effects/effect_registry.hpp"
#include "world/effects/entity_fx.hpp"

#include <algorithm>
#include <string>

namespace avgen::world {
namespace {

using E = EffectInstance;

constexpr float kAmplitude = 0.04f;
constexpr float kRate = 0.22f;
constexpr float kAsymmetry = 0.35f;
constexpr float kRegionCenter = 0.5f;
constexpr float kRegionWidth = 0.0f;
constexpr float kPhase = 0.0f;

constexpr EffectField kFields[] = {
    storedFloat("amplitude", "Amplitude", kAmplitude, -2.0f, 2.0f, 0.0f, 0.3f).fmt("%.3f m").main()
        .tooltip("How far the surface swells at the top of a breath, in metres along its\n"
                 "normal. Negative breathes in (a contraction)."),
    storedFloat("rate", "Rate", kRate, 0.0f, 10.0f, 0.02f, 2.0f).fmt("%.2f Hz").main()
        .tooltip("Breaths per second."),
    storedFloat("asymmetry", "Asymmetry", kAsymmetry, -0.9f, 0.9f, -0.9f, 0.9f).main()
        .tooltip("Above 0: a quick inhale and a long exhale. Below 0: the reverse. 0 is even."),
    storedFloat("regionCenter", "Region centre", kRegionCenter, 0.0f, 1.0f, 0.0f, 1.0f).sec("Region")
        .tooltip("Where along the entity's height the swell is strongest (0 bottom, 1 top)."),
    storedFloat("regionWidth", "Region width", kRegionWidth, 0.0f, 2.0f, 0.0f, 1.0f)
        .tooltip("How much of the body swells. 0 is all of it evenly; small is a narrow band."),
    storedFloat("phase", "Phase", kPhase, 0.0f, 1.0f, 0.0f, 1.0f)
        .tooltip("Where in its breath it starts. Give two creatures different phases so they\n"
                 "do not breathe in step."),
};

void set(E& e, float amplitude, float rate, float asymmetry, float centre, float width) {
    e.values.setFloat("breathing/amplitude", amplitude);
    e.values.setFloat("breathing/rate", rate);
    e.values.setFloat("breathing/asymmetry", asymmetry);
    e.values.setFloat("breathing/regionCenter", centre);
    e.values.setFloat("breathing/regionWidth", width);
}

constexpr const char* kStyleNames[] = {"Sleeping Creature", "Living Pod", "Pulsing Rock"};

void sleepingCreature(E& e) {
    set(e, 0.03f, 0.18f, 0.4f, 0.55f, 0.35f);
    e.style = kStyleNames[0];
}
void livingPod(E& e) {
    set(e, 0.08f, 0.3f, 0.2f, kRegionCenter, kRegionWidth);
    e.style = kStyleNames[1];
}
void pulsingRock(E& e) {
    set(e, 0.02f, 0.5f, 0.6f, kRegionCenter, kRegionWidth);
    e.style = kStyleNames[2];
}

constexpr EffectStyle kStyles[] = {
    {kStyleNames[0], sleepingCreature},
    {kStyleNames[1], livingPod},
    {kStyleNames[2], pulsingRock},
};

// Louder music, deeper breaths.
constexpr EffectRoute kRoutes[] = {
    {"audio.rms", "amplitude", 0.06f, 120.0f, 900.0f},
};

E make(std::string name) {
    E e;
    e.name = std::move(name);
    e.kind = EffectKind::Breathing;
    e.activation = Activation::Always;
    e.timing.fadeIn = 0.5;
    e.timing.fadeOut = 0.5;
    set(e, kAmplitude, kRate, kAsymmetry, kRegionCenter, kRegionWidth);
    e.values.setFloat("breathing/phase", kPhase);
    return e;
}

bool lanes(const E& e, const EffectContext&, const NodeView&, double, EntityLaneContribution& c) {
    const EffectValueStore& v = e.values;
    c.hasInflate = true;
    c.inflateAmp = std::clamp(v.getFloat("breathing/amplitude", kAmplitude), -2.0f, 2.0f);
    c.inflateRate = std::max(v.getFloat("breathing/rate", kRate), 0.0f);
    c.inflateAsym = std::clamp(v.getFloat("breathing/asymmetry", kAsymmetry), -0.9f, 0.9f);
    c.inflatePhase = v.getFloat("breathing/phase", kPhase);
    c.regionCentre = std::clamp(v.getFloat("breathing/regionCenter", kRegionCenter), 0.0f, 1.0f);
    c.regionWidth = std::max(v.getFloat("breathing/regionWidth", kRegionWidth), 0.0f);
    return true;
}

EffectSchema buildSchema() {
    EffectSchema s;
    s.kind = EffectKind::Breathing;
    s.key = "breathing";
    s.enumName = "Breathing";
    s.displayName = "Breathing";
    s.description = "The entity slowly swells and settles, like a chest or a pod: its surface moves along its "
                    "normals, evenly or in a band. Real geometry -- its shadow breathes too.";
    s.performance = PerformanceClass::VeryLow;
    s.primaryCost = CostVertex;
    s.addLabel = "Breathing";
    s.addTip = "This entity slowly swells and settles, as if breathing. The shape itself\n"
               "moves, so its outline and its shadow breathe too.";
    s.targets = targetBit(EffectTarget::Entity);
    s.category = EffectCategory::Motion;
    s.stage = RenderStage::Material;
    s.fields = kFields;
    s.styles = kStyles;
    s.routes = kRoutes;
    s.beatLeaf = "amplitude";
    s.factory = make;
    s.resolve.bucket = EffectBucket::EntityLanes;
    s.resolve.records = entityLaneRecords;
    s.resolve.lanes = lanes;
    return s;
}

} // namespace

const EffectSchema& breathingSchema() {
    static const EffectSchema kSchema = buildSchema();
    return kSchema;
}

} // namespace avgen::world
