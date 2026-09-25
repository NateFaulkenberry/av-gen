// Organic Pulsation (Effect Library Wave 2, catalog-organic.md): a peristaltic bulge travelling along
// the owner, like a swallowing vine or a pulsing vessel.
//
// **What it is.** FXL's travelling-bulge sub-block (world/effects/entity_fx.hpp): each vertex moves
// along its normal by `amplitude * exp(-(d / width)^2)`, where d is its distance, along the owner's
// height, to the nearest bulge centre; the centres sit `interval` apart and move at `speed`. Breathing
// is a standing swell; this is a TRAVELLING one -- ADR-376's band algebra on positions instead of
// emission, so pairing it with a Pulsing Veins of the same speed makes the light ride the bulge.
//
// Real geometry, in every pass (lit, depth prepass, shadow, and the previous frame's time for the
// velocity target). On a procedural owner the bulge also bends the normal, because that path takes
// its normal by finite differences of the displaced surface.
//
// **Stateless**: a function of the transport second (ADR-091).
//
// **Stacking.** One travelling bulge per owner (the second is Dropped by name); it sums with a
// Breathing and a Motion Smear.

#include "world/effects/effect_registry.hpp"
#include "world/effects/entity_fx.hpp"

#include <algorithm>
#include <string>

namespace avgen::world {
namespace {

using E = EffectInstance;

constexpr const char* const kDirectionNames[] = {"Root to tip", "Tip to root"};

constexpr float kAmplitude = 0.06f;
constexpr float kBandWidth = 0.07f;
constexpr float kSpeed = 0.35f;
constexpr float kInterval = 0.6f;

constexpr EffectField kFields[] = {
    storedFloat("amplitude", "Amplitude", kAmplitude, -2.0f, 2.0f, 0.0f, 0.3f).fmt("%.3f m").main()
        .tooltip("How far each bulge pushes the surface out, in metres along its normal.\n"
                 "Negative is a travelling constriction."),
    storedFloat("speed", "Speed", kSpeed, -10.0f, 10.0f, 0.0f, 2.0f).fmt("%.2f /s").main()
        .tooltip("How fast the bulges travel, in body lengths per second."),
    storedFloat("bandWidth", "Bulge width", kBandWidth, 0.005f, 1.0f, 0.02f, 0.3f).fmt("%.2f").main()
        .tooltip("How long each bulge is, as a fraction of the body's length."),
    storedFloat("interval", "Spacing", kInterval, 0.05f, 4.0f, 0.1f, 2.0f).fmt("%.2f")
        .tooltip("The distance between bulges, in body lengths. Above 1, one bulge at a time\n"
                 "with a rest between."),
    storedChoice("direction", "Direction", 0, kDirectionNames),
};

void set(E& e, float amplitude, float speed, float width, float interval, int direction) {
    e.values.setFloat("organicPulsation/amplitude", amplitude);
    e.values.setFloat("organicPulsation/speed", speed);
    e.values.setFloat("organicPulsation/bandWidth", width);
    e.values.setFloat("organicPulsation/interval", interval);
    e.values.setFloat("organicPulsation/direction", static_cast<float>(direction));
}

constexpr const char* kStyleNames[] = {"Swallow", "Heart Vessel", "Pod Pulse"};

void swallow(E& e) {
    set(e, 0.08f, 0.3f, 0.09f, 1.6f, 0);
    e.style = kStyleNames[0];
}
void heartVessel(E& e) {
    set(e, 0.03f, 1.1f, 0.05f, 0.35f, 0);
    e.style = kStyleNames[1];
}
void podPulse(E& e) {
    set(e, kAmplitude, kSpeed, kBandWidth, kInterval, 0);
    e.style = kStyleNames[2];
}

constexpr EffectStyle kStyles[] = {
    {kStyleNames[0], swallow},
    {kStyleNames[1], heartVessel},
    {kStyleNames[2], podPulse},
};

// The bass pushes the bulges out.
constexpr EffectRoute kRoutes[] = {
    {"audio.bass", "amplitude", 0.08f, 30.0f, 400.0f},
};

E make(std::string name) {
    E e;
    e.name = std::move(name);
    e.kind = EffectKind::OrganicPulsation;
    e.activation = Activation::Always;
    e.timing.fadeIn = 0.5;
    e.timing.fadeOut = 0.5;
    set(e, kAmplitude, kSpeed, kBandWidth, kInterval, 0);
    return e;
}

bool lanes(const E& e, const EffectContext&, const NodeView&, double, EntityLaneContribution& c) {
    const EffectValueStore& v = e.values;
    const float width = std::max(v.getFloat("organicPulsation/bandWidth", kBandWidth), 0.005f);
    c.hasTravel = true;
    c.travelAmp = std::clamp(v.getFloat("organicPulsation/amplitude", kAmplitude), -2.0f, 2.0f);
    c.travelSpeed = v.getFloat("organicPulsation/speed", kSpeed);
    c.travelWidth = width;
    // Never closer than two widths apart, so neighbouring bulges stay two bulges.
    c.travelInterval = std::max(v.getFloat("organicPulsation/interval", kInterval), 2.0f * width);
    c.travelDirection = v.getFloat("organicPulsation/direction", 0.0f) > 0.5f ? -1.0f : 1.0f;
    return true;
}

EffectSchema buildSchema() {
    EffectSchema s;
    s.kind = EffectKind::OrganicPulsation;
    s.key = "organicPulsation";
    s.enumName = "OrganicPulsation";
    s.displayName = "Organic Pulsation";
    s.description = "Bulges travel along the entity from root to tip, like something being swallowed down a "
                    "vine or blood through a vessel. Real geometry -- its outline and shadow move too.";
    s.performance = PerformanceClass::VeryLow;
    s.primaryCost = CostVertex;
    s.addLabel = "Organic Pulsation";
    s.addTip = "Bulges roll along this entity, root to tip, like a swallowing vine. The shape\n"
               "itself moves; pair it with Pulsing Veins at the same speed for light that rides it.";
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

const EffectSchema& organicPulsationSchema() {
    static const EffectSchema kSchema = buildSchema();
    return kSchema;
}

} // namespace avgen::world
