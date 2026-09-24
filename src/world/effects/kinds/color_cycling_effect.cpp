// Color Cycling (Effect Library Wave 2, catalog-stylization.md): the owner's hue animates over time.
//
// **What it is.** FXL's hue sub-block (the extension record, entity_fx.hpp): the lit shader turns the
// hue of the owner's base colour, its emission, or both, about the grey axis (so brightness is nearly
// kept), by `speed * t + spatialFrequency * (q . axis) + phase` turns. A range of a whole turn or more
// runs round the wheel -- with a spatial frequency, a rainbow band travels across the owner, Mark
// Ferrari's palette cycling on a mesh; less than a turn swings back and forth within the range.
// Emission includes whatever lane light the owner carries (a Glow, a Bioluminescence), so a cycling
// glow cycles too.
//
// Entity-scoped: a material program's `HueShift` op does this for every user of a material, and the
// Camera form is a route onto `post/hueShift`; neither is this type's business.
//
// **Stateless**, a function of the transport second.

#include "world/effects/effect_registry.hpp"
#include "world/effects/entity_fx.hpp"

#include <algorithm>
#include <string>

namespace avgen::world {
namespace {

using E = EffectInstance;

constexpr const char* const kAxisNames[] = {"Up", "Forward", "Right"};
constexpr const char* const kChannelNames[] = {"Base colour", "Emission", "Both"};

constexpr float kSpeed = 0.1f;
constexpr float kRange = 1.0f;
constexpr float kFrequency = 0.0f;
constexpr float kPhase = 0.0f;

constexpr EffectField kFields[] = {
    storedFloat("speed", "Speed", kSpeed, -20.0f, 20.0f, -2.0f, 2.0f).fmt("%.2f turns/s").main()
        .tooltip("How fast the hue turns, in trips round the colour wheel per second."),
    storedFloat("range", "Range", kRange, 0.0f, 1.0f, 0.0f, 1.0f).fmt("%.2f").main()
        .tooltip("1: all the way round the wheel. Less: the hue swings back and forth that\n"
                 "far either side of the entity's own colours."),
    storedFloat("spatialFrequency", "Rainbow bands", kFrequency, 0.0f, 50.0f, 0.0f, 6.0f).fmt("%.1f").main()
        .tooltip("How many hue cycles fit across the entity. 0 changes it all at once; more\n"
                 "sends rainbow bands travelling across it."),
    storedChoice("channel", "Applies to", 2, kChannelNames),
    storedChoice("axis", "Band direction", 0, kAxisNames).sec("Bands")
        .tooltip("Which of the entity's own axes the bands run along."),
    storedFloat("phase", "Phase", kPhase, 0.0f, 1.0f, 0.0f, 1.0f)
        .tooltip("An offset round the wheel. Route beat.phase here to lock the cycle to the beat."),
};

void set(E& e, float speed, float range, float frequency, int channel) {
    e.values.setFloat("colorCycling/speed", speed);
    e.values.setFloat("colorCycling/range", range);
    e.values.setFloat("colorCycling/spatialFrequency", frequency);
    e.values.setFloat("colorCycling/channel", static_cast<float>(channel));
}

constexpr const char* kStyleNames[] = {"Rainbow Wave", "Psychedelic", "Slow Drift"};

void rainbowWave(E& e) {
    set(e, 0.25f, 1.0f, 1.0f, 2);
    e.style = kStyleNames[0];
}
void psychedelic(E& e) {
    set(e, 0.7f, 1.0f, 3.0f, 2);
    e.style = kStyleNames[1];
}
void slowDrift(E& e) {
    set(e, 0.03f, 0.15f, 0.0f, 0);
    e.style = kStyleNames[2];
}

constexpr EffectStyle kStyles[] = {
    {kStyleNames[0], rainbowWave},
    {kStyleNames[1], psychedelic},
    {kStyleNames[2], slowDrift},
};

// Brighter music, faster colours.
constexpr EffectRoute kRoutes[] = {
    {"audio.treble", "speed", 0.4f, 60.0f, 600.0f},
};

E make(std::string name) {
    E e;
    e.name = std::move(name);
    e.kind = EffectKind::ColorCycling;
    e.activation = Activation::Always;
    e.timing.fadeIn = 0.5;
    e.timing.fadeOut = 0.5;
    set(e, kSpeed, kRange, kFrequency, 2);
    e.values.setFloat("colorCycling/phase", kPhase);
    return e;
}

bool lanes(const E& e, const EffectContext&, const NodeView&, double, EntityLaneContribution& c) {
    const EffectValueStore& v = e.values;
    const int axis = std::clamp(static_cast<int>(v.getFloat("colorCycling/axis", 0.0f) + 0.5f), 0, 2);
    c.hasHue = true;
    c.hueSpeed = v.getFloat("colorCycling/speed", kSpeed);
    c.hueRange = std::clamp(v.getFloat("colorCycling/range", kRange), 0.0f, 1.0f);
    c.hueFrequency = std::max(v.getFloat("colorCycling/spatialFrequency", kFrequency), 0.0f);
    c.hueChannel = std::clamp(v.getFloat("colorCycling/channel", 2.0f), 0.0f, 2.0f);
    // The owner's own axes, in its space q (Forward is the engine's -Z).
    c.hueAxis = axis == 0 ? glm::vec3(0.0f, 1.0f, 0.0f) : axis == 1 ? glm::vec3(0.0f, 0.0f, -1.0f)
                                                                     : glm::vec3(1.0f, 0.0f, 0.0f);
    c.huePhase = v.getFloat("colorCycling/phase", kPhase);
    return true;
}

EffectSchema buildSchema() {
    EffectSchema s;
    s.kind = EffectKind::ColorCycling;
    s.key = "colorCycling";
    s.enumName = "ColorCycling";
    s.displayName = "Color Cycling";
    s.description = "The entity's colours turn round the colour wheel over time -- all at once, swinging within "
                    "a range, or as rainbow bands travelling across it. Base colour, emission, or both.";
    s.performance = PerformanceClass::VeryLow;
    s.primaryCost = CostFragment;
    s.addLabel = "Color Cycling";
    s.addTip = "This entity's colours cycle through the rainbow, or drift within a range,\n"
               "optionally as bands travelling across it.";
    s.targets = targetBit(EffectTarget::Entity);
    s.category = EffectCategory::Lighting;
    s.stage = RenderStage::Material;
    s.fields = kFields;
    s.styles = kStyles;
    s.routes = kRoutes;
    s.beatLeaf = "speed";
    s.factory = make;
    s.resolve.bucket = EffectBucket::EntityLanes;
    s.resolve.records = entityLaneRecords;
    s.resolve.lanes = lanes;
    return s;
}

} // namespace

const EffectSchema& colorCyclingSchema() {
    static const EffectSchema kSchema = buildSchema();
    return kSchema;
}

} // namespace avgen::world
