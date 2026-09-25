// Pulsing Veins (Effect Library Wave 2, catalog-organic.md): light travelling along a network of
// veins on the owner.
//
// **What it is.** FXL's vein sub-block (the extension record, entity_fx.hpp): the edges of a
// domain-warped Worley field -- where F2 - F1 is small, `worleyF1F2` in shaders/noise.wgsl -- are the
// channels, in the owner's own space so they ride it. They thin away from the source (the owner's
// base, or its node origin), shade from a near colour to a far one, and carry pulses of light outward
// at `Pulse speed`, `Pulse spacing` apart, over a dim steady glow. Added emission before fog, into the
// emission target (it blooms); a Pulse on the owner multiplies it and a Glow sums with it.
//
// **Relation to tree energy (ADR-376).** The catalog intends this to supersede tree energy's special
// case, migrating `nodes/<n>/energy/*` to `fx/<id>/*`. That migration is NOT done here: the Tree of
// Life's tuned energy is part of the Glowmere look this slice was told not to re-tune, and this effect
// composes with it rather than replacing it. The migration is its own change.
//
// **Stateless**, a function of the transport second.

#include "world/effects/effect_registry.hpp"
#include "world/effects/entity_fx.hpp"

#include <algorithm>
#include <string>

namespace avgen::world {
namespace {

using E = EffectInstance;

constexpr const char* const kCoordinateNames[] = {"Up the height", "Out from the origin"};

constexpr float kScale = 9.0f;
constexpr float kWidth = 0.045f;
constexpr float kNoise = 0.35f;
constexpr glm::vec3 kNear{0.25f, 1.0f, 0.85f};
constexpr glm::vec3 kFar{0.65f, 0.35f, 1.0f};
constexpr float kIntensity = 6.0f;
constexpr float kPulseSpeed = 0.35f;
constexpr float kPulseWidth = 0.06f;
constexpr float kPulseInterval = 0.45f;
constexpr float kBaseline = 0.15f;

constexpr EffectField kFields[] = {
    storedColor("color", "Colour", kNear).main(),
    storedColor("colorFar", "Far colour", kFar).main(),
    storedFloat("intensity", "Brightness", kIntensity, 0.0f, 500.0f, 0.0f, 40.0f).main().floorAt(0.0f)
        .tooltip("The veins' light at the crest of a pulse. Blooms as a light source."),
    storedFloat("pulseSpeed", "Pulse speed", kPulseSpeed, -10.0f, 10.0f, 0.0f, 2.0f).fmt("%.2f /s").main()
        .tooltip("How fast pulses travel out along the veins, in body lengths per second.\n"
                 "Negative flows back to the source."),
    storedFloat("scale", "Vein density", kScale, 0.25f, 64.0f, 1.0f, 20.0f).fmt("%.1f")
        .sec("Network").tooltip("How many vein cells across the entity."),
    storedFloat("width", "Vein width", kWidth, 0.002f, 0.5f, 0.01f, 0.2f).fmt("%.3f")
        .tooltip("How thick the channels are at the source; they thin towards the tips."),
    storedFloat("noise", "Organic wander", kNoise, 0.0f, 2.0f, 0.0f, 1.0f)
        .tooltip("How much the network meanders. 0 is straight-edged, like a circuit."),
    storedChoice("coordinate", "Flows", 0, kCoordinateNames)
        .tooltip("Which way the light travels: up the entity's height, or out from its origin."),
    storedFloat("pulseWidth", "Pulse length", kPulseWidth, 0.005f, 1.0f, 0.01f, 0.3f).fmt("%.2f").sec("Pulses")
        .tooltip("How long each pulse is, as a fraction of the body."),
    storedFloat("pulseInterval", "Pulse spacing", kPulseInterval, 0.02f, 4.0f, 0.05f, 2.0f).fmt("%.2f")
        .tooltip("The distance between pulses, in body lengths."),
    storedFloat("baseline", "Glow between pulses", kBaseline, 0.0f, 1.0f, 0.0f, 1.0f)
        .tooltip("How bright the veins are between pulses, as a fraction of the crest."),
};

void set(E& e, glm::vec3 nearColor, glm::vec3 farColor, float intensity, float speed, float scale, float width, float noise,
         float pulseWidth, float interval, float baseline) {
    e.values.setColor("pulsingVeins/color", nearColor);
    e.values.setColor("pulsingVeins/colorFar", farColor);
    e.values.setFloat("pulsingVeins/intensity", intensity);
    e.values.setFloat("pulsingVeins/pulseSpeed", speed);
    e.values.setFloat("pulsingVeins/scale", scale);
    e.values.setFloat("pulsingVeins/width", width);
    e.values.setFloat("pulsingVeins/noise", noise);
    e.values.setFloat("pulsingVeins/pulseWidth", pulseWidth);
    e.values.setFloat("pulsingVeins/pulseInterval", interval);
    e.values.setFloat("pulsingVeins/baseline", baseline);
}

constexpr const char* kStyleNames[] = {"Tree of Life", "Alien Veins", "Circuit"};

void treeOfLife(E& e) {
    set(e, kNear, kFar, kIntensity, kPulseSpeed, kScale, kWidth, kNoise, kPulseWidth, kPulseInterval, kBaseline);
    e.style = kStyleNames[0];
}
void alienVeins(E& e) {
    set(e, {1.0f, 0.2f, 0.45f}, {0.55f, 0.1f, 1.0f}, 8.0f, 0.6f, 14.0f, 0.035f, 0.8f, 0.05f, 0.3f, 0.25f);
    e.style = kStyleNames[1];
}
void circuit(E& e) {
    set(e, {0.2f, 0.85f, 1.0f}, {0.2f, 1.0f, 0.5f}, 10.0f, 0.9f, 11.0f, 0.02f, 0.0f, 0.03f, 0.25f, 0.08f);
    e.style = kStyleNames[2];
}

constexpr EffectStyle kStyles[] = {
    {kStyleNames[0], treeOfLife},
    {kStyleNames[1], alienVeins},
    {kStyleNames[2], circuit},
};

// The bass drives the flow's brightness.
constexpr EffectRoute kRoutes[] = {
    {"audio.bass", "intensity", 8.0f, 20.0f, 350.0f},
};

E make(std::string name) {
    E e;
    e.name = std::move(name);
    e.kind = EffectKind::PulsingVeins;
    e.activation = Activation::Always;
    e.timing.fadeIn = 0.5;
    e.timing.fadeOut = 1.0;
    set(e, kNear, kFar, kIntensity, kPulseSpeed, kScale, kWidth, kNoise, kPulseWidth, kPulseInterval, kBaseline);
    return e;
}

bool lanes(const E& e, const EffectContext&, const NodeView&, double, EntityLaneContribution& c) {
    const EffectValueStore& v = e.values;
    const float intensity = std::max(v.getFloat("pulsingVeins/intensity", kIntensity), 0.0f);
    c.hasVeins = true;
    c.veinsNear = glm::max(v.getColor("pulsingVeins/color", kNear), glm::vec3(0.0f)) * intensity;
    c.veinsFar = glm::max(v.getColor("pulsingVeins/colorFar", kFar), glm::vec3(0.0f)) * intensity;
    c.veinsPulseSpeed = v.getFloat("pulsingVeins/pulseSpeed", kPulseSpeed);
    c.veinsScale = std::max(v.getFloat("pulsingVeins/scale", kScale), 0.01f);
    c.veinsWidth = std::max(v.getFloat("pulsingVeins/width", kWidth), 0.001f);
    c.veinsNoise = std::max(v.getFloat("pulsingVeins/noise", kNoise), 0.0f);
    c.veinsCoordinate = v.getFloat("pulsingVeins/coordinate", 0.0f) > 0.5f ? 1.0f : 0.0f;
    c.veinsPulseWidth = std::max(v.getFloat("pulsingVeins/pulseWidth", kPulseWidth), 0.001f);
    c.veinsPulseInterval = std::max(v.getFloat("pulsingVeins/pulseInterval", kPulseInterval), 0.01f);
    c.veinsBaseline = std::clamp(v.getFloat("pulsingVeins/baseline", kBaseline), 0.0f, 1.0f);
    return true;
}

EffectSchema buildSchema() {
    EffectSchema s;
    s.kind = EffectKind::PulsingVeins;
    s.key = "pulsingVeins";
    s.enumName = "PulsingVeins";
    s.displayName = "Pulsing Veins";
    s.description = "A network of glowing veins over the entity, thinning towards its tips, with pulses of light "
                    "flowing outward along them from the source. Blooms as light.";
    s.performance = PerformanceClass::Low;
    s.primaryCost = CostFragment;
    s.addLabel = "Pulsing Veins";
    s.addTip = "Glowing veins spread over this entity, with light pulsing out along them from\n"
               "its root -- the Tree of Life's energy, on anything.";
    s.targets = targetBit(EffectTarget::Entity);
    s.category = EffectCategory::Lighting;
    s.stage = RenderStage::Material;
    s.fields = kFields;
    s.styles = kStyles;
    s.routes = kRoutes;
    s.beatLeaf = "intensity";
    s.factory = make;
    s.resolve.bucket = EffectBucket::EntityLanes;
    s.resolve.records = entityLaneRecords;
    s.resolve.lanes = lanes;
    return s;
}

} // namespace

const EffectSchema& pulsingVeinsSchema() {
    static const EffectSchema kSchema = buildSchema();
    return kSchema;
}

} // namespace avgen::world
